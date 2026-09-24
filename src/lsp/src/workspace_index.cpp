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
#include <tuple>
#include <utility>
#include <vector>

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

std::string lowered(std::string_view text) {
    std::string out(text);
    std::ranges::transform(out, out.begin(),
                           [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return out;
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
    const std::string written = lowered(name);
    const std::string wanted = lowered(query);
    if (written == wanted) {
        return 4;
    }
    if (written.starts_with(wanted)) {
        return 3;
    }
    if (written.find(wanted) != std::string::npos) {
        return 2;
    }
    std::size_t at = 0;
    for (const char character : written) {
        if (at < wanted.size() && character == wanted[at]) {
            ++at;
        }
    }
    return at == wanted.size() ? 1 : 0;
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

WorkspaceIndex::Entry WorkspaceIndex::read(const std::filesystem::path& file, std::filesystem::file_time_type written) {
    Entry entry;
    entry.written = written;
    const std::optional<std::string> text = read_file(file.string());
    if (!text.has_value()) {
        return entry;
    }
    const std::string path = file.string();
    const std::string uri = path_to_uri(path);
    std::vector<std::string> arguments = commands_.flags_for(path);
    arguments.insert(arguments.end(), options_.clang_arguments.begin(), options_.clang_arguments.end());

    // What Clang reads of it, as an open document is read.
    EditorView view;
    EditorView::Options view_options;
    view_options.driver = options_.driver;
    view_options.arguments = arguments;
    if (view.refresh(OpenBuffer{uri, path, &*text}, {}, view_options)) {
        entry.symbols = declared(view.outline(), uri);
        for (EditorView::Named& named : view.all_mentions()) {
            entry.mentions.push_back(
                Mention{std::move(named.usr), std::move(named.mention.location), named.mention.role});
        }
        for (EditorView::Named& named : view.unwritten(nullptr)) {
            entry.unwritten.push_back(
                Mention{std::move(named.usr), std::move(named.mention.location), named.mention.role});
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
        if (progress_) {
            progress_(0, stale.size());
        }
        for (std::size_t done = 0; done < stale.size(); ++done) {
            Entry entry = read(stale[done].first, stale[done].second);
            lock.lock();
            entries_.insert_or_assign(stale[done].first, std::move(entry));
            const bool stop = stopping_;
            lock.unlock();
            if (progress_) {
                progress_(done + 1, stale.size());
            }
            if (stop) {
                break;
            }
        }
        lock.lock();
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
