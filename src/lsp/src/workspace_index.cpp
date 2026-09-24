#include "cppl/lsp/workspace_index.hpp"

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/driver/buffer_compile.hpp"
#include "cppl/elaboration/elaborate.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/lsp/compile_commands.hpp"
#include "cppl/lsp/editor_view.hpp"
#include "cppl/lsp/position.hpp"
#include "cppl/lsp/proof_names.hpp"
#include "cppl/lsp/protocol.hpp"
#include "cppl/lsp/uri.hpp"
#include "cppl/source/location.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iterator>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#if defined(__APPLE__)
#include <pthread/qos.h>
#include <sys/qos.h>
#elif defined(__linux__)
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace cppl::lsp {

namespace {

// Bounds on the walk, so that a root that is not a project -- a home
// directory, a drive -- costs a bounded amount.
constexpr std::size_t kMostFiles = 20000;
constexpr int kDeepest = 32;

bool is_source(const std::filesystem::path& file) {
    std::string extension = file.extension().string();
    std::ranges::transform(extension, extension.begin(),
                           [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    static const std::set<std::string, std::less<>> kSources = {".cppl", ".cpp", ".cc",  ".cxx", ".c++", ".h",
                                                                ".hh",   ".hpp", ".hxx", ".h++", ".ipp", ".inl"};
    return kSources.contains(extension);
}

// A directory the walk passes by: a hidden one, a package cache, a build tree
// (it holds a CMake cache), or a repository nested in the workspace, whose
// files are that repository's.
bool passed_by(const std::filesystem::path& directory) {
    const std::string name = directory.filename().string();
    if (name.starts_with('.') || name == "node_modules") {
        return true;
    }
    std::error_code error;
    return std::filesystem::exists(directory / "CMakeCache.txt", error) ||
           std::filesystem::exists(directory / ".git", error);
}

void flatten(const std::vector<DocumentSymbol>& symbols, const std::string& uri, const std::string& container,
             std::vector<WorkspaceIndex::Symbol>& into) {
    for (const DocumentSymbol& symbol : symbols) {
        into.push_back(WorkspaceIndex::Symbol{symbol.name, container, symbol.kind, Location{uri, symbol.selection}});
        flatten(symbol.children, uri, container.empty() ? symbol.name : container + "::" + symbol.name, into);
    }
}

// How many files are read at once: `threads`, or when it is 0, half the
// processors, so that the editor's own requests and compiles keep the rest.
std::size_t readers(std::size_t threads) {
    if (threads != 0) {
        return threads;
    }
    return std::max<std::size_t>(1, std::thread::hardware_concurrency() / 2);
}

// Reading the workspace yields to the editor: a thread that indexes runs at a
// lower priority than the thread answering requests.
void yield_to_the_editor() {
#if defined(__APPLE__)
    static_cast<void>(pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0));
#elif defined(__linux__)
    constexpr int kNicer = 10;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): the thread's own id has no other interface.
    const auto thread = static_cast<id_t>(syscall(SYS_gettid));
    static_cast<void>(setpriority(PRIO_PROCESS, thread, kNicer));
#endif
}

using Start = std::tuple<std::string, std::uint32_t, std::uint32_t>;

Start start_of(const Location& location) {
    return {location.uri, location.range.start.line, location.range.start.character};
}

} // namespace

std::vector<WorkspaceIndex::Symbol> WorkspaceIndex::declared(const std::vector<DocumentSymbol>& outline,
                                                             const std::string& uri) {
    std::vector<Symbol> symbols;
    flatten(outline, uri, std::string(), symbols);
    return symbols;
}

int WorkspaceIndex::match(std::string_view name, std::string_view query) {
    if (query.empty()) {
        return 1;
    }
    // Compared a character at a time, ignoring case, so that a search over
    // every name of a workspace allocates nothing.
    const auto same = [](char lhs, char rhs) {
        return std::tolower(static_cast<unsigned char>(lhs)) == std::tolower(static_cast<unsigned char>(rhs));
    };
    const auto spelled_at = [&](std::size_t from) {
        return std::ranges::equal(name.substr(from, query.size()), query, same);
    };
    if (name.size() >= query.size()) {
        if (spelled_at(0)) {
            return name.size() == query.size() ? 4 : 3;
        }
        for (std::size_t from = 1; from + query.size() <= name.size(); ++from) {
            if (spelled_at(from)) {
                return 2;
            }
        }
    }
    std::size_t at = 0;
    for (const char character : name) {
        if (at < query.size() && same(character, query[at])) {
            ++at;
        }
    }
    return at == query.size() ? 1 : 0;
}

void WorkspaceIndex::rank(std::vector<Symbol>& symbols, std::string_view query) {
    std::vector<std::pair<int, Symbol>> scored;
    scored.reserve(symbols.size());
    for (Symbol& symbol : symbols) {
        const int score = match(symbol.name, query);
        scored.emplace_back(score, std::move(symbol));
    }
    std::ranges::stable_sort(scored, [](const auto& lhs, const auto& rhs) {
        return std::tie(rhs.first, lhs.second.name) < std::tie(lhs.first, rhs.second.name);
    });
    symbols.clear();
    std::set<std::tuple<std::string, std::string, std::uint32_t, std::uint32_t>> seen;
    for (auto& [score, symbol] : scored) {
        const Position& start = symbol.location.range.start;
        if (seen.emplace(symbol.name, symbol.location.uri, start.line, start.character).second) {
            symbols.push_back(std::move(symbol));
        }
    }
}

WorkspaceIndex::WorkspaceIndex(std::vector<std::filesystem::path> roots, Options options, Progress progress)
    : roots_(std::move(roots)),
      options_(std::move(options)),
      progress_(std::move(progress)),
      thread_([this] { run(); }) {}

WorkspaceIndex::~WorkspaceIndex() {
    {
        const std::scoped_lock lock(mutex_);
        stopping_ = true;
    }
    changed_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

std::vector<std::filesystem::path> WorkspaceIndex::discover() {
    std::vector<std::filesystem::path> files;
    std::set<std::filesystem::path> seen;
    const auto add = [&](const std::filesystem::path& file) {
        if (files.size() < kMostFiles && seen.insert(file.lexically_normal()).second) {
            files.push_back(file.lexically_normal());
        }
    };
    for (const std::filesystem::path& root : roots_) {
        std::error_code error;
        std::filesystem::recursive_directory_iterator walk(
            root, std::filesystem::directory_options::skip_permission_denied, error);
        for (; !error && walk != std::filesystem::recursive_directory_iterator(); walk.increment(error)) {
            const std::filesystem::directory_entry& entry = *walk;
            std::error_code kind_error;
            if (entry.is_directory(kind_error)) {
                if (walk.depth() >= kDeepest || passed_by(entry.path())) {
                    walk.disable_recursion_pending();
                }
                continue;
            }
            if (entry.is_regular_file(kind_error) && is_source(entry.path())) {
                add(entry.path());
            }
        }
        // A file the build compiles is indexed wherever it lives.
        for (const std::filesystem::path& listed : commands_.listed_under(root)) {
            std::error_code exists_error;
            if (std::filesystem::is_regular_file(listed, exists_error)) {
                add(listed);
            }
        }
    }
    return files;
}

WorkspaceIndex::Entry WorkspaceIndex::read(const std::filesystem::path& file, std::filesystem::file_time_type written,
                                           std::vector<std::string> arguments,
                                           const std::set<std::filesystem::path>& indexed) const {
    Entry entry;
    entry.written = written;
    const std::optional<std::string> text = read_file(file.string());
    if (!text.has_value()) {
        return entry;
    }
    const std::string path = file.string();
    const std::string uri = path_to_uri(path);

    // What Clang reads of it, as an open document is read. What it finds in a
    // header the index reads on its own is that header's to record, so that
    // no place is kept once per file including it.
    const auto kept = [&](const Location& location) {
        const std::optional<std::string> written_in = uri_to_path(location.uri);
        if (!written_in.has_value()) {
            return true;
        }
        const std::filesystem::path place = std::filesystem::path(*written_in).lexically_normal();
        return place == file || !indexed.contains(place);
    };
    EditorView view;
    EditorView::Options view_options;
    view_options.driver = options_.driver;
    view_options.arguments = arguments;
    view_options.once = true;
    if (view.refresh(OpenBuffer{uri, path, &*text}, {}, view_options)) {
        entry.symbols = declared(view.outline(), uri);
        for (EditorView::Named& named : view.all_mentions()) {
            if (kept(named.mention.location)) {
                entry.mentions.push_back(
                    Mention{std::move(named.usr), std::move(named.mention.location), named.mention.role});
            }
        }
        for (EditorView::Named& named : view.unwritten(nullptr)) {
            if (kept(named.mention.location)) {
                entry.unwritten.push_back(
                    Mention{std::move(named.usr), std::move(named.mention.location), named.mention.role});
            }
        }
    }

    // What the names its proof statements use resolve to, which only the
    // compile says; a file with no C++L uses none.
    {
        const frontend::TokenStream stream = frontend::lex(*text, path);
        diagnostics::Engine unreported;
        if (frontend::recognize(stream, unreported).empty()) {
            return entry;
        }
    }
    driver::BufferCompileRequest request;
    request.virtual_path = path;
    request.text = *text;
    request.clang = options_.clang;
    request.clang_arguments = std::move(arguments);
    request.stop_after_elaboration = true;
    diagnostics::Engine engine;
    const driver::BufferCompileOutcome outcome = driver::compile_buffer(request, engine);
    const PositionMapper mapper(*text);
    for (const elaboration::ResolvedName& name : outcome.names) {
        if (normal_path(name.at.file) != normal_path(path)) {
            continue;
        }
        if (const std::optional<std::size_t> start = spelled_at(*text, name.at, name.name)) {
            entry.uses.push_back(
                ProofNameUse{name.name, name.declaration,
                             Location{uri, Range{mapper.byte_offset_to_position(*start),
                                                 mapper.byte_offset_to_position(*start + name.name.size())}}});
        }
    }
    return entry;
}

void WorkspaceIndex::run() {
    std::unique_lock lock(mutex_);
    while (!stopping_) {
        const std::uint64_t look = ++looks_;
        look_now_ = false;
        lock.unlock();
        const std::vector<std::filesystem::path> files = discover();
        const std::set<std::filesystem::path> present(files.begin(), files.end());
        lock.lock();

        // What is gone is forgotten; what is new, or edited since it was
        // read, is read again.
        std::erase_if(entries_, [&](const auto& entry) { return !present.contains(entry.first); });
        std::vector<std::pair<std::filesystem::path, std::filesystem::file_time_type>> stale;
        for (const std::filesystem::path& file : files) {
            std::error_code error;
            const std::filesystem::file_time_type written = std::filesystem::last_write_time(file, error);
            if (error) {
                continue;
            }
            const auto known = entries_.find(file);
            if (known == entries_.end() || known->second.written != written) {
                stale.emplace_back(file, written);
            }
        }
        if (stale.empty()) {
            current_ = look;
            changed_.notify_all();
            changed_.wait_for(lock, options_.poll, [this] { return stopping_ || look_now_; });
            continue;
        }

        lock.unlock();
        read_all(stale, present);
        lock.lock();
    }
}

void WorkspaceIndex::read_all(
    const std::vector<std::pair<std::filesystem::path, std::filesystem::file_time_type>>& stale,
    const std::set<std::filesystem::path>& indexed) {
    // Each file's flags are found here: the compilation database is read by
    // this thread alone.
    std::vector<std::vector<std::string>> arguments;
    arguments.reserve(stale.size());
    for (const auto& [file, written] : stale) {
        std::vector<std::string> flags = commands_.flags_for(file.string());
        flags.insert(flags.end(), options_.clang_arguments.begin(), options_.clang_arguments.end());
        arguments.push_back(std::move(flags));
    }
    std::mutex reporting;
    std::size_t done = 0;
    if (progress_) {
        progress_(0, stale.size());
    }
    std::atomic<std::size_t> next = 0;
    const auto work = [&] {
        yield_to_the_editor();
        for (std::size_t at = next++; at < stale.size(); at = next++) {
            {
                const std::scoped_lock lock(mutex_);
                if (stopping_) {
                    return;
                }
            }
            Entry entry = read(stale[at].first, stale[at].second, arguments[at], indexed);
            {
                const std::scoped_lock lock(mutex_);
                entries_.insert_or_assign(stale[at].first, std::move(entry));
            }
            // One report at a time, each counting one more file.
            const std::scoped_lock lock(reporting);
            ++done;
            if (progress_) {
                progress_(done, stale.size());
            }
        }
    };
    const std::size_t helpers = std::min(readers(options_.threads), stale.size()) - 1;
    std::vector<std::thread> workers;
    workers.reserve(helpers);
    for (std::size_t helper = 0; helper < helpers; ++helper) {
        workers.emplace_back(work);
    }
    work();
    for (std::thread& worker : workers) {
        worker.join();
    }
}

std::vector<WorkspaceIndex::Symbol> WorkspaceIndex::symbols(std::string_view query, std::size_t limit) const {
    std::vector<Symbol> found;
    {
        const std::scoped_lock lock(mutex_);
        for (const auto& [file, entry] : entries_) {
            std::ranges::copy_if(entry.symbols, std::back_inserter(found),
                                 [&](const Symbol& symbol) { return match(symbol.name, query) > 0; });
        }
    }
    rank(found, query);
    if (found.size() > limit) {
        found.resize(limit);
    }
    return found;
}

std::vector<WorkspaceIndex::Mention> WorkspaceIndex::mentions(const std::vector<std::string>& usrs) const {
    const std::set<std::string, std::less<>> wanted(usrs.begin(), usrs.end());
    std::vector<Mention> found;
    std::set<Start> seen;
    const std::scoped_lock lock(mutex_);
    for (const auto& [file, entry] : entries_) {
        for (const Mention& mention : entry.mentions) {
            if (wanted.contains(mention.usr) && seen.insert(start_of(mention.location)).second) {
                found.push_back(mention);
            }
        }
    }
    return found;
}

std::vector<Location> WorkspaceIndex::unwritten(const std::vector<std::string>& usrs) const {
    const std::set<std::string, std::less<>> wanted(usrs.begin(), usrs.end());
    std::vector<Location> found;
    std::set<Start> seen;
    const std::scoped_lock lock(mutex_);
    for (const auto& [file, entry] : entries_) {
        for (const Mention& use : entry.unwritten) {
            if (wanted.contains(use.usr) && seen.insert(start_of(use.location)).second) {
                found.push_back(use.location);
            }
        }
    }
    return found;
}

bool WorkspaceIndex::holds(const std::string& path) const {
    const std::filesystem::path file = std::filesystem::path(path).lexically_normal();
    const std::scoped_lock lock(mutex_);
    return entries_.contains(file);
}

std::vector<Location> WorkspaceIndex::proof_name_uses(const std::string& name,
                                                      const source::SourceLocation& declaration) const {
    std::vector<Location> uses;
    std::set<Start> seen;
    const std::scoped_lock lock(mutex_);
    for (const auto& [file, entry] : entries_) {
        for (const ProofNameUse& use : entry.uses) {
            if (use.name == name && same_place(use.declaration, declaration) && seen.insert(start_of(use.use)).second) {
                uses.push_back(use.use);
            }
        }
    }
    return uses;
}

bool WorkspaceIndex::wait_until_current(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    const std::uint64_t asked = looks_;
    look_now_ = true;
    changed_.notify_all();
    return changed_.wait_for(lock, timeout, [&] { return current_ > asked; });
}

} // namespace cppl::lsp
