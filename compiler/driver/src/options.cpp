#include "cppl/driver/options.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cppl::driver {

namespace {

// Options whose value is a separate argument. Their value must never be
// mistaken for an input file.
constexpr std::array<std::string_view, 25> kValueOptions = {"-o",
                                                            "-I",
                                                            "-isystem",
                                                            "-iquote",
                                                            "-idirafter",
                                                            "-include",
                                                            "-imacros",
                                                            "-F",
                                                            "-framework",
                                                            "-L",
                                                            "-l",
                                                            "-D",
                                                            "-U",
                                                            "-x",
                                                            "-Xclang",
                                                            "-Xlinker",
                                                            "-Xpreprocessor",
                                                            "-MF",
                                                            "-MT",
                                                            "-MQ",
                                                            "-target",
                                                            "-arch",
                                                            "-isysroot",
                                                            "--sysroot",
                                                            "--std"};

// The language standard one argument selects with its value joined, as Clang
// accepts it: `-std=c++20` or `--std=c++20`.
std::optional<std::string> joined_standard(std::string_view argument) {
    for (const std::string_view option : {std::string_view("-std="), std::string_view("--std=")}) {
        if (argument.starts_with(option)) {
            return std::string(argument.substr(option.size()));
        }
    }
    return std::nullopt;
}

// Commands that do not produce a compilation C++L could verify.
constexpr std::array<std::string_view, 12> kPassthroughOptions = {"-E",
                                                                  "-M",
                                                                  "-MM",
                                                                  "--version",
                                                                  "-v",
                                                                  "--help",
                                                                  "-###",
                                                                  "-dumpversion",
                                                                  "-dumpmachine",
                                                                  "-print-search-dirs",
                                                                  "-print-prog-name",
                                                                  "-print-file-name"};

bool has_extension(std::string_view path, std::string_view extension) {
    return path.size() > extension.size() && path.ends_with(extension);
}

} // namespace

bool is_source_path(std::string_view path) {
    return has_extension(path, ".cpp") || has_extension(path, ".cc") || has_extension(path, ".cxx") ||
           has_extension(path, ".c++") || has_extension(path, ".cppl") || has_extension(path, ".C");
}

bool is_header_path(std::string_view path) {
    return has_extension(path, ".h") || has_extension(path, ".hpp") || has_extension(path, ".hh") ||
           has_extension(path, ".hxx");
}

std::string selected_standard(const std::vector<std::string>& arguments) {
    std::string standard;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string& argument = arguments[index];
        if (argument == "--std") {
            if (index + 1 < arguments.size()) {
                standard = arguments[index + 1];
            }
            ++index;
            continue;
        }
        // Another option's value is never read as an option of its own.
        if (std::ranges::find(kValueOptions, argument) != kValueOptions.end()) {
            ++index;
            continue;
        }
        if (std::optional<std::string> joined = joined_standard(argument)) {
            standard = std::move(*joined);
        }
    }
    return standard;
}

Options parse(int argc, const char* const* argv) {
    Options options;
    options.clang = CPPL_DEFAULT_CLANG;

    bool skip_value = false;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];

        if (argument.starts_with("--cppl-")) {
            if (argument == "--cppl-trust-report") {
                options.trust_report = true;
            } else if (argument == "--cppl-version") {
                options.version = true;
            } else if (argument.starts_with("--cppl-clang=")) {
                options.clang = argument.substr(std::string_view("--cppl-clang=").size());
            } else if (argument.starts_with("--cppl-emit-projection=")) {
                options.emit_projection = argument.substr(std::string_view("--cppl-emit-projection=").size());
            } else if (argument.starts_with("--cppl-emit-interface=")) {
                const std::string path = argument.substr(std::string_view("--cppl-emit-interface=").size());
                if (path.empty()) {
                    options.errors.emplace_back("'--cppl-emit-interface=' names no file");
                } else if (!options.emit_interface.empty()) {
                    options.errors.emplace_back("'--cppl-emit-interface' is given more than once");
                } else {
                    options.emit_interface = path;
                }
            } else if (argument.starts_with("--cppl-emit-trust-report=")) {
                const std::string path = argument.substr(std::string_view("--cppl-emit-trust-report=").size());
                if (path.empty()) {
                    options.errors.emplace_back("'--cppl-emit-trust-report=' names no file");
                } else if (!options.emit_trust_report.empty()) {
                    options.errors.emplace_back("'--cppl-emit-trust-report' is given more than once");
                } else {
                    options.emit_trust_report = path;
                }
            } else if (argument.starts_with("--cppl-import-interface=")) {
                const std::string path = argument.substr(std::string_view("--cppl-import-interface=").size());
                if (path.empty()) {
                    options.errors.emplace_back("'--cppl-import-interface=' names no file");
                } else {
                    options.import_interfaces.push_back(path);
                }
            } else {
                options.errors.push_back("unknown C++L option '" + argument + "'");
            }
            continue;
        }

        options.arguments.push_back(argument);

        if (skip_value) {
            skip_value = false;
            continue;
        }

        if (argument.starts_with("-")) {
            if (std::ranges::find(kValueOptions, argument) != kValueOptions.end()) {
                skip_value = true;
            }
            if (std::ranges::find(kPassthroughOptions, argument) != kPassthroughOptions.end()) {
                options.passthrough = true;
            }
            if (argument == "-x") {
                options.explicit_language = true;
            }
            continue;
        }

        if (is_source_path(argument) || is_header_path(argument)) {
            options.inputs.push_back(Input{argument, options.arguments.size() - 1, is_header_path(argument)});
        }
    }

    // The mode an interface is bound to and a trust report names, whichever
    // spelling Clang was given it in (SPEC.md TUBOUND-005).
    options.standard = selected_standard(options.arguments);
    return options;
}

} // namespace cppl::driver
