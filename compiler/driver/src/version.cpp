#include "version.hpp"

#include "cppl/artifact/interface.hpp"
#include "cppl/clang/bridge.hpp"
#include "cppl/driver/scratch.hpp"
#include "cppl/driver/source_identity.hpp"
#include "cppl/driver/trust_report.hpp"
#include "cppl/kernel/version.hpp"
#include "cppl/obligations/interface.hpp"
#include "cppl/source/digest.hpp"
#include "cppl/verifier_semantics.hpp"
#include "target.hpp"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cppl::driver::detail {

namespace {

std::string first_line(std::string_view text) {
    const std::size_t end = text.find_first_of("\r\n");
    std::string line(text.substr(0, end));
    while (!line.empty() && line.back() == ' ') {
        line.pop_back();
    }
    return line;
}

// The value a `-dM` dump gives `name`, or nothing when it defines none.
std::optional<std::string> macro(std::string_view dump, std::string_view name) {
    const std::string wanted = "#define " + std::string(name) + " ";
    const std::string bare = "#define " + std::string(name);
    std::size_t start = 0;
    while (start < dump.size()) {
        std::size_t end = dump.find('\n', start);
        if (end == std::string_view::npos) {
            end = dump.size();
        }
        const std::string_view line = dump.substr(start, end - start);
        if (line.starts_with(wanted)) {
            return std::string(line.substr(wanted.size()));
        }
        if (line == bare) {
            return std::string{};
        }
        start = end + 1;
    }
    return std::nullopt;
}

// The language mode a macro dump shows: the standard `__cplusplus` names, and
// whether GNU extensions are on, which Clang says by leaving `__STRICT_ANSI__`
// undefined.
std::string language_mode(std::string_view dump) {
    const std::optional<std::string> value = macro(dump, "__cplusplus");
    if (!value.has_value()) {
        return "not C++";
    }
    std::string digits;
    for (const char character : *value) {
        if (character < '0' || character > '9') {
            break;
        }
        digits.push_back(character);
    }
    std::string standard;
    if (digits == "199711") {
        standard = "++98";
    } else if (digits == "201103") {
        standard = "++11";
    } else if (digits == "201402") {
        standard = "++14";
    } else if (digits == "201703") {
        standard = "++17";
    } else if (digits == "202002") {
        standard = "++20";
    } else if (digits == "202302") {
        standard = "++23";
    } else if (digits.size() == 6 && digits > "202302") {
        standard = "++2c";
    } else {
        return "__cplusplus " + *value;
    }
    const bool gnu = !macro(dump, "__STRICT_ANSI__").has_value();
    return (gnu ? "gnu" : "c") + standard;
}

// The C++ standard library a dump of `<cstddef>` shows, which decides the
// layout and mangled names of every standard type a program passes.
std::string standard_library(std::string_view dump) {
    if (const std::optional<std::string> version = macro(dump, "_LIBCPP_VERSION")) {
        return "libc++ " + *version;
    }
    if (const std::optional<std::string> release = macro(dump, "_GLIBCXX_RELEASE")) {
        const std::optional<std::string> date = macro(dump, "__GLIBCXX__");
        return "libstdc++ " + *release + (date.has_value() ? " (" + *date + ")" : std::string{});
    }
    if (const std::optional<std::string> version = macro(dump, "_MSVC_STL_VERSION")) {
        return "Microsoft STL " + *version;
    }
    return "none found for this configuration";
}

// The compiler this `cppl` was itself built with, which is part of the trusted
// base every verdict it reports rests on.
std::string built_with() {
#if defined(__clang__)
    return first_line(std::string("Clang ") + __clang_version__);
#elif defined(__GNUC__)
    return first_line(std::string("GCC ") + __VERSION__);
#elif defined(_MSC_FULL_VER)
    return "MSVC " + std::to_string(_MSC_FULL_VER);
#else
    return "not identified";
#endif
}

void line(std::string_view label, std::string_view value) {
    constexpr std::size_t kWidth = 29;
    std::cout << label;
    for (std::size_t column = label.size(); column < kWidth; ++column) {
        std::cout << ' ';
    }
    std::cout << value << "\n";
}

} // namespace

std::expected<BuildRecord, std::string> build_record(const artifact::Configuration& configuration,
                                                     const std::string& clang) {
    const ScratchDirectory scratch;
    if (scratch.path().empty()) {
        return std::unexpected("could not create a directory to ask the Clang driver its version");
    }
    const std::optional<std::string> version = ask_driver(clang, {"--version"}, scratch.path() / "version");
    const std::string driver = version.has_value() ? first_line(*version) : std::string{};
    if (driver.empty()) {
        return std::unexpected("the Clang driver '" + clang + "' did not report its version");
    }
    BuildRecord build;
    build.compiler = configuration.compiler;
    build.source_revision = kSourceRevision;
    build.source_tag = kSourceTag;
    build.source_tree = kSourceTree;
    build.compiler_build = configuration.build.to_hex();
    build.verification_semantics = configuration.semantics;
    build.verifier_semantics_digest = configuration.verifier.to_hex();
    build.kernel = configuration.kernel;
    build.formal_core = configuration.core;
    build.interface_format = artifact::kFormatVersion;
    build.clang = configuration.clang;
    build.runtime_compiler = driver;
    build.language = configuration.language;
    build.target = configuration.target;
    build.semantic_flags = configuration.flags;
    return build;
}

int print_version(const std::string& clang, const std::vector<std::string>& arguments, const std::string& standard) {
    line("C++L compiler:", CPPL_VERSION);
    line("Source revision:", kSourceRevision);
    line("Source tag:", kSourceTag);
    line("Source tree:", kSourceTree);
    line("Built with:", built_with());
    // The same two identities every interface this compiler writes records and
    // every one it reads is compared by (SPEC.md TUBOUND-005, TRUST.md
    // TCB-XTU-008).
    line("Verification semantics:", obligations::kVerificationSemanticsVersion);
    source::Digest verifier;
    verifier.bytes = kVerifierSemanticsDigest;
    line("Verifier-semantics digest:", verifier.to_hex());
    line("Kernel version:", kernel::kKernelVersion);
    line("Formal core version:", kernel::kFormalCoreVersion);
    line("Interface format version:", std::to_string(artifact::kFormatVersion));
    // The Clang that resolves the C++ semantics of what is verified: the
    // libclang this process loaded, whatever it was built against.
    const std::string analysing = clangbridge::clang_version();
    line("Clang:", analysing);

    // What the Clang driver that preprocesses and compiles selects. Its path is
    // not printed: it names where a toolchain is installed, not what it is.
    const ScratchDirectory scratch;
    if (scratch.path().empty()) {
        std::cerr << "cppl: error: could not create a directory to ask the Clang driver what it selects\n";
        return 1;
    }
    bool answered = true;
    const auto unavailable = [&answered](std::string_view what) {
        answered = false;
        return "unavailable: the Clang driver did not report " + std::string(what);
    };

    const std::optional<std::string> version = ask_driver(clang, {"--version"}, scratch.path() / "version");
    const std::string driver = version.has_value() ? first_line(*version) : std::string{};
    line("Clang driver:", driver.empty() ? unavailable("its version") : driver);

    std::vector<std::string> query = arguments;
    query.emplace_back("-print-target-triple");
    const std::optional<std::string> triple = ask_driver(clang, query, scratch.path() / "target");
    const std::string target = triple.has_value() ? first_line(*triple) : std::string{};
    line("Target:", target.empty() ? unavailable("its target") : target);

    // The macros of a unit that includes `<cstddef>`, where the configuration
    // has one, say which language mode and which standard library the
    // arguments select. A cross target without a standard library installed
    // has none to report, and that leaves the rest of the record intact.
    const std::filesystem::path probe = scratch.path() / "probe.cpp";
    std::optional<std::string> macros;
    if (write_scratch_file(probe, "#if __has_include(<cstddef>)\n#include <cstddef>\n#endif\n")) {
        std::vector<std::string> dump = arguments;
        dump.insert(dump.end(), {"-x", "c++", "-E", "-dM", probe.string()});
        macros = ask_driver(clang, dump, scratch.path() / "probe.macros");
    }
    if (macros.has_value()) {
        line("C++ mode:", language_mode(*macros) +
                              (standard.empty() ? std::string(" (the Clang driver's default)") : std::string{}));
        line("C++ standard library:", standard_library(*macros));
    } else {
        line("C++ mode:", unavailable("the language mode it selects"));
        line("C++ standard library:", unavailable("its standard library"));
    }

    if (!answered) {
        std::cerr << "cppl: error: the Clang driver '" << clang << "' could not be asked what it selects\n";
        return 1;
    }
    // The analysis and the program compiled from it must be one Clang's; two
    // releases may resolve the same C++ differently, and nothing downstream
    // would notice (TRUST.md TCB-CLANG-001, TCB-RUNTIME-001).
    if (driver != analysing) {
        std::cerr << "cppl: warning: the Clang driver is '" << driver << "' and the Clang that analyses is '"
                  << analysing << "'; C++L supports only one Clang release for both\n";
    }
    return 0;
}

} // namespace cppl::driver::detail
