#include "target.hpp"

#include "cppl/driver/options.hpp"
#include "cppl/driver/process.hpp"
#include "cppl/driver/scratch.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cppl::driver::detail {

namespace {

// The most a Clang answer to one question is read of. A macro dump of one
// standard header is a few hundred KiB; anything larger is not an answer to the
// question asked.
constexpr std::size_t kMaxAnswerBytes = std::size_t{4} << 20U;

// The longest triple believed. Clang's are a few dozen bytes.
constexpr std::size_t kMaxTripleBytes = 256;

std::optional<std::string> read_bounded(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    std::string text(kMaxAnswerBytes + 1, '\0');
    stream.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (stream.bad()) {
        return std::nullopt;
    }
    text.resize(static_cast<std::size_t>(stream.gcount()));
    if (text.size() > kMaxAnswerBytes) {
        return std::nullopt;
    }
    return text;
}

// The single line a `-print-*` question is answered with, or nothing when the
// answer is not one plain triple.
std::optional<std::string> triple_answer(const std::optional<std::string>& answer) {
    if (!answer.has_value()) {
        return std::nullopt;
    }
    std::string triple = *answer;
    while (!triple.empty() && (triple.back() == '\n' || triple.back() == '\r' || triple.back() == ' ')) {
        triple.pop_back();
    }
    const bool plain = std::ranges::all_of(triple, [](char character) {
        return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
               (character >= '0' && character <= '9') || character == '-' || character == '_' || character == '.';
    });
    if (triple.empty() || triple.size() > kMaxTripleBytes || !plain) {
        return std::nullopt;
    }
    return triple;
}

// The configuration files a driver's `--version` says it read, in order.
std::vector<std::string> configuration_files(std::string_view version) {
    constexpr std::string_view kPrefix = "Configuration file: ";
    std::vector<std::string> files;
    std::size_t start = 0;
    while (start < version.size()) {
        std::size_t end = version.find('\n', start);
        if (end == std::string_view::npos) {
            end = version.size();
        }
        std::string_view line = version.substr(start, end - start);
        if (line.ends_with('\r')) {
            line.remove_suffix(1);
        }
        if (line.starts_with(kPrefix)) {
            files.emplace_back(line.substr(kPrefix.size()));
        }
        start = end + 1;
    }
    return files;
}

} // namespace

std::optional<std::string> ask_driver(const std::string& clang, const std::vector<std::string>& arguments,
                                      const std::filesystem::path& answer) {
    const ProcessResult result = run_capturing_stdout(clang, arguments, answer);
    if (!result.started || result.signaled || result.exit_code != 0) {
        return std::nullopt;
    }
    return read_bounded(answer);
}

std::expected<CompileTarget, std::string> compile_target(const std::string& clang,
                                                         const std::vector<std::string>& arguments) {
    const ScratchDirectory scratch;
    if (scratch.path().empty()) {
        return std::unexpected("could not create a directory to ask the Clang driver for its target");
    }
    const auto query = [&](std::string_view option) {
        std::vector<std::string> asked = arguments;
        asked.emplace_back(option);
        return ask_driver(clang, asked, scratch.path() / "answer");
    };

    CompileTarget target;
    const std::optional<std::string> triple = triple_answer(query("-print-target-triple"));
    const std::optional<std::string> effective = triple_answer(query("-print-effective-triple"));
    const std::optional<std::string> version = query("--version");
    if (!triple.has_value() || !effective.has_value() || !version.has_value()) {
        return std::unexpected("the Clang driver '" + clang +
                               "' could not be asked which target it compiles the program for");
    }
    target.triple = *triple;
    target.effective = *effective;
    target.configuration_files = configuration_files(*version);
    return target;
}

std::optional<std::string> argument_editing_environment() {
    constexpr const char* kVariable = "CCC_OVERRIDE_OPTIONS";
    // Set at all, even empty: it is the driver's to read, not this program's.
    // NOLINTNEXTLINE(concurrency-mt-unsafe): nothing in this program writes the environment.
    if (std::getenv(kVariable) != nullptr) {
        return std::string(kVariable);
    }
    return std::nullopt;
}

std::vector<std::string> analysis_arguments(const std::vector<std::string>& arguments, const CompileTarget& target) {
    // A configuration file the arguments name is read as the driver found it,
    // below, and not looked for again: libclang, which runs as a `clang` of its
    // own, does not search the driver's directory for a name without one, and
    // would read another file of that name, or none. Each option is left out
    // with its value, so no other option takes it (SPEC.md ARITH-014).
    std::vector<std::string> given;
    for (std::size_t index = 0; index < arguments.size();) {
        const std::size_t end = std::min(arguments.size(), index + 1 + separate_values(arguments[index]));
        const bool names_configuration = arguments[index] == "--config" || arguments[index].starts_with("--config=");
        if (!names_configuration) {
            for (std::size_t kept = index; kept < end; ++kept) {
                given.push_back(arguments[kept]);
            }
        }
        index = end;
    }
    // The configuration files the driver read, and none the analysis would
    // find by defaults of its own: a configuration file may add any option.
    given.emplace_back("--no-default-config");
    for (const std::string& file : target.configuration_files) {
        given.push_back("--config=" + file);
    }
    // The target the driver selects, whatever named it: an option, the prefix
    // of the driver's own name, or a configuration file.
    given.push_back("--target=" + target.triple);
    return given;
}

} // namespace cppl::driver::detail
