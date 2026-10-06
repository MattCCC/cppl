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

// Every option the driver of Clang 22 (`clang`, `clang++`) reads with its value
// in the argument after it, as its option table states: each `Separate`
// option, and each `JoinedOrSeparate` one written with nothing joined to it,
// in every spelling the driver accepts. Such a value is never an input, and
// never an option of its own: left out where its option stays, it would leave
// the option to take the next argument as its value instead, so that `-o x.o`
// or an option the analysis needs is read as something else in one command
// and not in another (SPEC.md ARITH-014).
constexpr auto kValueOptions = std::to_array<std::string_view>({
    "-A",
    "-B",
    "-D",
    "-F",
    "-G",
    "-I",
    "-L",
    "-MF",
    "-MJ",
    "-MQ",
    "-MT",
    "-T",
    "-U",
    "-V",
    "-Xanalyzer",
    "-Xarch_device",
    "-Xarch_host",
    "-Xassembler",
    "-Xclang",
    "-Xclangas",
    "-Xcuda-fatbinary",
    "-Xcuda-ptxas",
    "-Xlinker",
    "-Xmicrosoft-visualc-tools-root",
    "-Xmicrosoft-visualc-tools-version",
    "-Xmicrosoft-windows-sdk-root",
    "-Xmicrosoft-windows-sdk-version",
    "-Xmicrosoft-windows-sys-root",
    "-Xopenmp-target",
    "-Xpreprocessor",
    "-Zlinker-input",
    "-alias_list",
    "-allowable_client",
    "-arch",
    "-arch_only",
    "-b",
    "-bundle_loader",
    "-ccc-gcc-name",
    "-ccc-install-dir",
    "-client_name",
    "-compatibility_version",
    "-current_version",
    "-cxx-isystem",
    "-darwin-target-variant",
    "-darwin-target-variant-triple",
    "-dependency-dot",
    "-dependency-file",
    "-dsym-dir",
    "-dumpdir",
    "-dylib_file",
    "-dylinker_install_name",
    "-e",
    "-exported_symbols_list",
    "-fdebug-compilation-dir",
    "-fexperimental-openacc-macro-override",
    "-filelist",
    "-fmodule-implementation-of",
    "-fmodules-user-build-path",
    "-fnew-alignment",
    "-force_load",
    "-framework",
    "-ftrapv-handler",
    "-gen-cdb-fragment-path",
    "-hlsl-entry",
    "-iapinotes-modules",
    "-idirafter",
    "-iframework",
    "-iframeworkwithsysroot",
    "-image_base",
    "-imacros",
    "-imultilib",
    "-include",
    "-include-pch",
    "-init",
    "-install_name",
    "-interface-stub-version=",
    "-iprefix",
    "-iquote",
    "-isysroot",
    "-isystem",
    "-isystem-after",
    "-ivfsoverlay",
    "-iwithprefix",
    "-iwithprefixbefore",
    "-iwithsysroot",
    "-l",
    "-lazy_framework",
    "-lazy_library",
    "-meabi",
    "-mllvm",
    "-mmlir",
    "-module-dependency-dir",
    "-mthread-model",
    "-multiply_defined",
    "-multiply_defined_unused",
    "-o",
    "-object-file-name",
    "-pagezero_size",
    "-read_only_relocs",
    "-reexport_framework",
    "-reexport_library",
    "-resource-dir",
    "-rpath",
    "-seg1addr",
    "-seg_addr_table",
    "-seg_addr_table_filename",
    "-segs_read_only_addr",
    "-segs_read_write_addr",
    "-serialize-diagnostics",
    "-specs",
    "-stdlib++-isystem",
    "-sub_library",
    "-sub_umbrella",
    "-target",
    "-u",
    "-umbrella",
    "-undefined",
    "-unexported_symbols_list",
    "-validator-version",
    "-vfsoverlay",
    "-weak_framework",
    "-weak_library",
    "-weak_reference_mismatches",
    "-working-directory",
    "-x",
    "-z",
    "--CLASSPATH",
    "--analyzer-output",
    "--assert",
    "--bootclasspath",
    "--classpath",
    "--config",
    "--define-macro",
    "--dyld-prefix",
    "--encoding",
    "--extdirs",
    "--for-linker",
    "--force-link",
    "--imacros",
    "--include",
    "--include-directory",
    "--include-directory-after",
    "--include-prefix",
    "--include-with-prefix",
    "--include-with-prefix-after",
    "--include-with-prefix-before",
    "--language",
    "--library-directory",
    "--mhwdiv",
    "--no-system-header-prefix",
    "--output",
    "--output-class-directory",
    "--param",
    "--prefix",
    "--print-file-name",
    "--print-prog-name",
    "--resource",
    "--rtlib",
    "--serialize-diagnostics",
    "--specs",
    "--std",
    "--stdlib",
    "--sysroot",
    "--system-header-prefix",
    "--undefine-macro",
    "--vfsoverlay",
});

// The options that read more than one argument after them (`MultiArg`), with
// how many each reads.
struct MultipleValues {
    std::string_view option;
    std::size_t values;
};
constexpr auto kMultipleValueOptions = std::to_array<MultipleValues>({
    {"-sectalign", 3},
    {"-sectcreate", 3},
    {"-sectobjectsymbols", 2},
    {"-segaddr", 2},
    {"-segcreate", 3},
    {"-segprot", 3},
    {"-sectorder", 3},
});

// The options that name what they apply to joined to them and read the
// argument after them as well (`JoinedAndSeparate`): `-Xarch_arm64 -O2`.
constexpr auto kJoinedAndSeparatePrefixes =
    std::to_array<std::string_view>({"-Xarch_", "-Xopenmp-target=", "-Xoffload-linker"});

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
    // Every extension Clang reads as C++ source, in each spelling it accepts.
    constexpr auto kExtensions = std::to_array<std::string_view>(
        {".cpp", ".CPP", ".cc", ".CC", ".cp", ".cxx", ".CXX", ".c++", ".C++", ".C", ".cppl"});
    return std::ranges::any_of(kExtensions,
                               [path](std::string_view extension) { return has_extension(path, extension); });
}

bool is_header_path(std::string_view path) {
    return has_extension(path, ".h") || has_extension(path, ".hpp") || has_extension(path, ".hh") ||
           has_extension(path, ".hxx");
}

std::size_t separate_values(std::string_view argument) {
    if (std::ranges::find(kValueOptions, argument) != kValueOptions.end()) {
        return 1;
    }
    if (const auto multiple = std::ranges::find(kMultipleValueOptions, argument, &MultipleValues::option);
        multiple != kMultipleValueOptions.end()) {
        return multiple->values;
    }
    if (std::ranges::any_of(kJoinedAndSeparatePrefixes,
                            [argument](std::string_view prefix) { return argument.starts_with(prefix); })) {
        return 1;
    }
    return 0;
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
        if (const std::size_t values = separate_values(argument); values != 0) {
            index += values;
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

    // How many of the arguments still to come are values of the last option.
    std::size_t values_left = 0;
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
        options.option_value.push_back(values_left != 0);

        if (values_left != 0) {
            --values_left;
            const std::size_t before = options.arguments.size() - 2;
            if (options.arguments[before] == "-o" && !options.option_value[before]) {
                options.output = argument;
            }
            continue;
        }

        if (argument.starts_with("-")) {
            values_left = separate_values(argument);
            if (std::ranges::find(kPassthroughOptions, argument) != kPassthroughOptions.end()) {
                options.passthrough = true;
            }
            if (argument == "-x") {
                options.explicit_language = true;
            }
            if (argument == "-c" || argument == "-S" || argument == "-fsyntax-only") {
                options.compile_only = true;
            }
            continue;
        }

        // Every input, whatever it is: a source, an object, a library.
        options.positional.push_back(options.arguments.size() - 1);
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
