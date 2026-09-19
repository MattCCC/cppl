#include "cppl/driver/options.hpp"

#include <algorithm>
#include <array>

namespace cppl::driver {

namespace {

// Options whose value is a separate argument. Their value must never be
// mistaken for an input file.
constexpr std::array<std::string_view, 24> kValueOptions = {
    "-o",      "-I",    "-isystem",  "-iquote",  "-idirafter", "-include", "-imacros",       "-F",  "-framework", "-L",
    "-l",      "-D",    "-U",        "-x",       "-Xclang",    "-Xlinker", "-Xpreprocessor", "-MF", "-MT",        "-MQ",
    "-target", "-arch", "-isysroot", "--sysroot"};

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

Options parse(int argc, const char* const* argv) {
    Options options;
    options.clang = CPPL_DEFAULT_CLANG;

    bool skip_value = false;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];

        if (argument.starts_with("--cppl-")) {
            if (argument == "--cppl-trust-report") {
                options.trust_report = true;
            } else if (argument.starts_with("--cppl-clang=")) {
                options.clang = argument.substr(std::string_view("--cppl-clang=").size());
            } else if (argument.starts_with("--cppl-emit-projection=")) {
                options.emit_projection = argument.substr(std::string_view("--cppl-emit-projection=").size());
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
            if (argument.starts_with("-std=")) {
                options.standard = argument.substr(std::string_view("-std=").size());
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

    return options;
}

} // namespace cppl::driver
