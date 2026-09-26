#include "interface_io.hpp"

#include "cppl/artifact/interface.hpp"
#include "cppl/clang/bridge.hpp"
#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/driver/process.hpp"
#include "cppl/driver/scratch.hpp"
#include "cppl/kernel/version.hpp"
#include "cppl/obligations/interface.hpp"
#include "cppl/source/digest.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace cppl::driver::detail {
namespace {

void report(diagnostics::Engine& engine, std::string message, std::string note = {}) {
    diagnostics::Diagnostic diagnostic;
    diagnostic.severity = diagnostics::Severity::Error;
    diagnostic.category = diagnostics::Category::VerificationInterface;
    diagnostic.message = std::move(message);
    if (!note.empty()) {
        diagnostic.notes.push_back(diagnostics::Note{std::move(note), {}});
    }
    engine.report(std::move(diagnostic));
}

// The file this process was started from. The compiler's own build is part of
// what a verification interface is bound to, and a version string alone does
// not change when the compiler does (TRUST.md TCB-VERSION-004).
std::optional<std::filesystem::path> executable_path() {
#ifdef _WIN32
    std::array<wchar_t, 32768> buffer{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return std::nullopt;
    }
    return std::filesystem::path(std::wstring(buffer.data(), length));
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (size == 0 || _NSGetExecutablePath(buffer.data(), &size) != 0) {
        return std::nullopt;
    }
    buffer.resize(buffer.find('\0'));
    return std::filesystem::path(buffer);
#else
    return std::filesystem::path("/proc/self/exe");
#endif
}

// The largest file a unit may be recorded as preprocessed from. A path an
// imported interface names is chosen by whoever wrote the file, so reading it
// is bounded like any other hostile input.
constexpr std::uintmax_t kMaxSourceBytes = std::uintmax_t{1} << 30U;

// The size of a regular file, or why it is not one. A device, a FIFO or a
// directory is never read: an interface naming one could make a read that
// never ends, or never starts.
std::expected<std::uintmax_t, std::string> regular_file_size(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        return std::unexpected(error ? error.message() : std::string("it is not a regular file"));
    }
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error) {
        return std::unexpected(error.message());
    }
    return size;
}

// The SHA-256 of a regular file's content, read in bounded pieces and never
// past the size it had when it was examined, so a file that changes while it
// is read is not read without end.
std::optional<source::Digest> digest_of_file(const std::filesystem::path& path) {
    const std::expected<std::uintmax_t, std::string> size = regular_file_size(path);
    if (!size || *size > kMaxSourceBytes) {
        return std::nullopt;
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    source::Hasher hasher;
    std::array<char, std::size_t{1} << 16U> buffer{};
    std::uintmax_t remaining = *size;
    while (remaining > 0) {
        const std::size_t wanted = static_cast<std::size_t>(std::min<std::uintmax_t>(remaining, buffer.size()));
        stream.read(buffer.data(), static_cast<std::streamsize>(wanted));
        const std::streamsize read = stream.gcount();
        if (read <= 0) {
            return std::nullopt; // shorter than it was
        }
        hasher.update(buffer.data(), static_cast<std::size_t>(read));
        remaining -= static_cast<std::uintmax_t>(read);
    }
    // A byte past the size it had means it grew while it was read.
    if (stream.bad() || stream.peek() != std::ifstream::traits_type::eof()) {
        return std::nullopt;
    }
    return hasher.finish();
}

// A file's content, refused unread when it is larger than an interface may be.
std::expected<std::string, std::string> read_bounded(const std::filesystem::path& path) {
    const std::expected<std::uintmax_t, std::string> size = regular_file_size(path);
    if (!size) {
        return std::unexpected("it cannot be read: " + size.error());
    }
    if (*size > artifact::kMaxBytes) {
        return std::unexpected("it is larger than the " + std::to_string(artifact::kMaxBytes) +
                               " bytes an interface may have");
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::unexpected("it cannot be opened");
    }
    // One byte past the size is asked for, so a file that grew since its size
    // was taken is refused rather than read in part.
    std::string text(static_cast<std::size_t>(*size) + 1, '\0');
    stream.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (stream.bad()) {
        return std::unexpected("it cannot be read");
    }
    text.resize(static_cast<std::size_t>(stream.gcount()));
    if (text.size() != *size) {
        return std::unexpected("it changed while it was read");
    }
    return text;
}

// Every file's digest is taken once per compile, however many interfaces name
// it: interfaces of one project share most of their headers.
class SourceDigests {
  public:
    std::optional<source::Digest> of(const std::string& path) {
        const auto [entry, added] = known_.try_emplace(path);
        if (added) {
            entry->second = digest_of_file(path);
        }
        return entry->second;
    }

  private:
    std::map<std::string, std::optional<source::Digest>> known_;
};

// The first field in which two configurations differ, described, or nothing.
std::optional<std::string> configuration_difference(const artifact::Configuration& recorded,
                                                    const artifact::Configuration& current) {
    const auto differs = [](std::string_view what, const std::string& there, const std::string& here) {
        return std::string(what) + ": it records '" + there + "', and this compile uses '" + here + "'";
    };
    if (recorded.compiler != current.compiler) {
        return differs("it was produced by another C++L compiler version", recorded.compiler, current.compiler);
    }
    if (!(recorded.build == current.build)) {
        return "it was produced by another build of the C++L compiler, whose verification semantics this build "
               "cannot vouch for";
    }
    if (recorded.kernel != current.kernel) {
        return differs("it was checked by another proof kernel", recorded.kernel, current.kernel);
    }
    if (recorded.core != current.core) {
        return differs("it was stated in another formal core", recorded.core, current.core);
    }
    if (recorded.clang != current.clang) {
        return differs("its C++ semantics were resolved by another Clang", recorded.clang, current.clang);
    }
    if (recorded.language != current.language) {
        return differs("it was produced in another C++ language mode", recorded.language, current.language);
    }
    if (recorded.target != current.target) {
        return differs("it was produced for another target", recorded.target, current.target);
    }
    return std::nullopt;
}

// Whether a file an interface was produced from is still what it was.
std::optional<std::string> stale_source(const artifact::Interface& recorded, SourceDigests& digests) {
    for (const artifact::SourceFile& file : recorded.sources) {
        const std::optional<source::Digest> now = digests.of(file.path);
        if (!now.has_value()) {
            return "'" + file.path + "', which it was produced from, can no longer be read as a source file";
        }
        if (!(*now == file.digest)) {
            return "'" + file.path + "' has changed since it was produced";
        }
    }
    return std::nullopt;
}

} // namespace

std::expected<artifact::Configuration, std::string> current_configuration(const std::string& clang,
                                                                          const std::vector<std::string>& arguments,
                                                                          const std::string& standard) {
    artifact::Configuration configuration;
    configuration.compiler = CPPL_VERSION;
    const std::optional<std::filesystem::path> self = executable_path();
    const std::optional<source::Digest> build = self.has_value() ? digest_of_file(*self) : std::nullopt;
    if (!build.has_value()) {
        return std::unexpected("this compiler cannot identify its own build, which a verification interface is "
                               "bound to");
    }
    configuration.build = *build;
    configuration.kernel = kernel::kKernelVersion;
    configuration.core = kernel::kFormalCoreVersion;
    configuration.clang = clangbridge::clang_version();
    configuration.language = standard.empty() ? "default" : standard;

    // The triple the same Clang selects for the same arguments, which is the
    // target the unit is analysed and compiled for.
    const ScratchDirectory scratch;
    if (scratch.path().empty()) {
        return std::unexpected("could not create a directory to ask Clang for its target");
    }
    const std::filesystem::path printed = scratch.path() / "target";
    std::vector<std::string> query = arguments;
    query.emplace_back("-print-target-triple");
    const ProcessResult result = run_capturing_stdout(clang, query, printed);
    if (!result.started || result.signaled || result.exit_code != 0) {
        return std::unexpected("could not ask '" + clang + "' for the target triple" +
                               (result.error.empty() ? std::string{} : ": " + result.error));
    }
    std::ifstream stream(printed, std::ios::binary);
    std::string triple;
    std::getline(stream, triple);
    while (!triple.empty() && (triple.back() == '\r' || triple.back() == ' ')) {
        triple.pop_back();
    }
    if (triple.empty() || triple.size() > artifact::kMaxLineBytes) {
        return std::unexpected("'" + clang + "' reported no target triple");
    }
    configuration.target = std::move(triple);

    // Recorded for audit; not compared. What a contract means to a consumer is
    // rebuilt from its own declaration, and what a body means is fixed by the
    // producer's own compile, which these options drove (RFC 0017).
    configuration.flags = arguments;
    return configuration;
}

obligations::Imports read_imports(const std::vector<std::string>& paths, const artifact::Configuration& configuration,
                                  diagnostics::Engine& engine) {
    obligations::Imports imports;
    SourceDigests digests;
    std::map<std::string, obligations::ImportedEntry> accepted;
    std::map<std::string, obligations::RefusedEntry> refused;
    const auto refuse = [&refused](const std::string& symbol, const std::string& origin, const std::string& reason) {
        refused.try_emplace(symbol, obligations::RefusedEntry{symbol, origin, reason});
    };

    for (const std::string& path : paths) {
        const std::string named = "cannot use verification interface '" + path + "'";
        std::expected<std::string, std::string> text = read_bounded(path);
        if (!text) {
            report(engine, named + ": " + text.error());
            continue;
        }
        const std::expected<artifact::Interface, artifact::ParseError> recorded = artifact::parse(*text);
        if (!recorded) {
            report(engine,
                   named + ": " + recorded.error().reason +
                       (recorded.error().line != 0 ? " (line " + std::to_string(recorded.error().line) + ")"
                                                   : std::string{}),
                   "an interface is read only as the compiler wrote it; rebuild the unit it describes (SPEC.md "
                   "TUBOUND-005)");
            continue;
        }
        std::optional<std::string> unusable = configuration_difference(recorded->configuration, configuration);
        if (!unusable.has_value()) {
            if (std::optional<std::string> stale = stale_source(*recorded, digests)) {
                unusable = "it is stale: " + *stale;
            }
        }
        if (unusable.has_value()) {
            report(engine, named + ": " + *unusable,
                   "rebuild '" + recorded->unit +
                       "' with this compiler to produce a current interface (SPEC.md "
                       "TUBOUND-005)");
            for (const artifact::Entry& entry : recorded->entries) {
                refuse(entry.symbol, path, *unusable);
            }
            continue;
        }
        for (const artifact::Entry& entry : recorded->entries) {
            obligations::ImportedEntry imported{path, entry, artifact::identify(entry)};
            if (refused.contains(entry.symbol)) {
                continue;
            }
            const auto [existing, added] = accepted.try_emplace(entry.symbol, imported);
            if (added || existing->second.identity == imported.identity) {
                continue;
            }
            // Two records of one function's contract that disagree cannot both
            // describe the program being built (SPEC.md TUBOUND-009).
            const std::string reason =
                "'" + existing->second.origin + "' and '" + path + "' record different contracts for it";
            report(engine,
                   "verification interfaces '" + existing->second.origin + "' and '" + path +
                       "' record different contracts for '" + entry.name + "'",
                   "a function has one contract; rebuild the interfaces so that one unit records it (SPEC.md "
                   "TUBOUND-009)");
            refuse(entry.symbol, path, reason);
            accepted.erase(existing);
        }
    }

    // An entry is usable only while every contract of another unit it was
    // proven through is itself imported, as the very record it was proven
    // against (SPEC.md TUBOUND-006, TUBOUND-009). Refusing one entry can break
    // another that rests on it, so what rests on a refused entry is examined
    // again. An entry is examined once, and once more when one of its
    // dependencies is refused, at which point it is refused itself, so the work
    // is linear in the recorded dependencies in whatever order they come.
    std::map<std::string, std::vector<std::string>> dependents;
    std::vector<std::string> pending;
    for (const auto& [symbol, imported] : accepted) {
        pending.push_back(symbol);
        for (const artifact::Dependency& dependency : imported.entry.depends) {
            dependents[dependency.symbol].push_back(symbol);
        }
    }
    while (!pending.empty()) {
        const std::string symbol = std::move(pending.back());
        pending.pop_back();
        const auto entry = accepted.find(symbol);
        if (entry == accepted.end()) {
            continue;
        }
        const auto broken = std::ranges::find_if(entry->second.entry.depends, [&](const artifact::Dependency& d) {
            const auto found = accepted.find(d.symbol);
            return found == accepted.end() || !(found->second.identity == d.entry);
        });
        if (broken == entry->second.entry.depends.end()) {
            continue;
        }
        refuse(symbol, entry->second.origin,
               "it was proven through the contract of '" + broken->symbol +
                   "', and no imported interface records that contract as it was when this one was proven");
        accepted.erase(entry);
        if (const auto affected = dependents.find(symbol); affected != dependents.end()) {
            pending.insert(pending.end(), affected->second.begin(), affected->second.end());
        }
    }

    for (auto& [symbol, entry] : accepted) {
        imports.entries.push_back(std::move(entry));
    }
    for (auto& [symbol, entry] : refused) {
        imports.refused.push_back(std::move(entry));
    }
    return imports;
}

std::expected<std::vector<artifact::SourceFile>, std::string> source_files(const std::vector<std::string>& files) {
    std::vector<artifact::SourceFile> sources;
    std::set<std::string> seen;
    for (const std::string& name : files) {
        if (name.empty() || name.front() == '<') {
            continue;
        }
        std::error_code error;
        const std::filesystem::path absolute = std::filesystem::absolute(name, error).lexically_normal();
        if (error) {
            return std::unexpected("'" + name + "' has no absolute path");
        }
        if (!seen.insert(absolute.string()).second) {
            continue;
        }
        const std::optional<source::Digest> digest = digest_of_file(absolute);
        if (!digest.has_value()) {
            return std::unexpected("'" + absolute.string() + "', which the unit was preprocessed from, cannot be read");
        }
        sources.push_back(artifact::SourceFile{absolute.string(), *digest});
    }
    return sources;
}

std::expected<void, std::string> write_interface(const std::string& path, const artifact::Interface& recorded) {
    const std::expected<std::string, std::string> text = artifact::serialize(recorded);
    if (!text) {
        return std::unexpected(text.error());
    }
    // Named by its content, so two compiles writing the same interface at once
    // write the same bytes to the same partial file, and different interfaces
    // never share one.
    const std::filesystem::path destination(path);
    std::filesystem::path partial = destination;
    partial += ".partial-" + source::hash_bytes(*text).to_short_hex(16);
    if (!write_scratch_file(partial, *text)) {
        return std::unexpected("could not write '" + partial.string() + "'");
    }
    std::error_code error;
    std::filesystem::rename(partial, destination, error);
    if (error) {
        std::filesystem::remove(partial, error);
        return std::unexpected("could not replace '" + path + "'");
    }
    return {};
}

void withdraw_interface(const std::string& path) {
    // Only a file that is an interface is removed: the option names a path the
    // build chose, and a mistyped one must not cost the file it names.
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return;
    }
    std::string first(artifact::kMagic.size() + 1, '\0');
    stream.read(first.data(), static_cast<std::streamsize>(first.size()));
    first.resize(static_cast<std::size_t>(stream.gcount()));
    stream.close();
    if (first != std::string(artifact::kMagic) + " ") {
        return;
    }
    std::error_code error;
    std::filesystem::remove(path, error);
}

} // namespace cppl::driver::detail
