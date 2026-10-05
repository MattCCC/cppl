// Verification interfaces in the editor: a document is verified against the
// contracts other units proved, from the interfaces its build imports into it
// (`--cppl-import-interface=` in its `compile_commands.json` entry), each read
// and checked exactly as the CLI checks it (SPEC.md TUBOUND-003 to TUBOUND-006,
// TUBOUND-014; src/lsp/include/cppl/lsp/compile_commands.hpp).
//
// The interfaces are written by the compiler itself, as a build writes them.

#include "cppl/driver/process.hpp"
#include "cppl/driver/scratch.hpp"
#include "cppl/lsp/position.hpp"
#include "cppl/lsp/protocol.hpp"
#include "cppl/lsp/server.hpp"
#include "cppl/lsp/uri.hpp"
#include "cppl/testing/test.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace cppl::lsp;

namespace {

constexpr const char* kHeader = R"(#pragma once

verified unsigned step(unsigned x)
    expects (x < 100u)
    ensures (result == x + 1u);
)";

constexpr const char* kLibrary = R"(#include "counter.hpp"

unsigned step(unsigned x) {
    return x + 1u;
}
)";

constexpr const char* kClient = R"(#include "../lib/counter.hpp"

verified unsigned twice(unsigned x)
    expects (x < 50u)
    ensures (result == x + 2u)
{
    return step(step(x));
}
)";

void write(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

// A library unit and a client of it, the library compiled by the CLI into its
// object and its verification interface, in `standard` when one is given and
// with `flags`.
struct Project {
    std::filesystem::path root;
    std::filesystem::path client;
    std::filesystem::path interface;

    explicit Project(const std::filesystem::path& under, const std::string& standard,
                     const std::vector<std::string>& flags = {})
        : root(under / "project"),
          client(root / "src" / "client.cpp"),
          interface(root / "build" / "counter.cppli") {
        write(root / "lib" / "counter.hpp", kHeader);
        write(root / "lib" / "counter.cpp", kLibrary);
        write(client, kClient);
        std::filesystem::create_directories(root / "build");
        std::vector<std::string> arguments = flags;
        if (!standard.empty()) {
            arguments.push_back("-std=" + standard);
        }
        arguments.insert(arguments.end(),
                         {"-c", (root / "lib" / "counter.cpp").string(), "-o", (root / "build" / "counter.o").string(),
                          "--cppl-emit-interface=" + interface.string()});
        const cppl::driver::ProcessResult built = cppl::driver::run(CPPL_TEST_COMPILER, arguments);
        CPPL_CHECK(built.started);
        CPPL_CHECK(!built.signaled);
        CPPL_CHECK_EQ(built.exit_code, 0);
        CPPL_CHECK(std::filesystem::is_regular_file(interface));
    }

    // A database compiling the client with `flags`, as a build would record it.
    void compiled_with(const std::string& flags) const {
        write(root / "build" / "compile_commands.json", R"([{"directory":")" + (root / "build").string() +
                                                            R"(","file":"../src/client.cpp","command":"cppl )" + flags +
                                                            R"( -I../lib -c ../src/client.cpp -o client.o"}])");
    }
};

struct Opened {
    std::vector<Diagnostic> diagnostics;
    std::string lens;
    std::string hover;
};

// `text` as an editor opens it at `path`: what is published for it, the lens
// over the first `name`, and the hover there.
Opened open_document(const std::filesystem::path& path, const std::string& text, const std::string& name) {
    Server server(CPPL_TEST_DEFAULT_CLANG);
    Opened opened;
    server.set_diagnostic_publisher([&opened](const std::string&, std::vector<Diagnostic> diagnostics) {
        opened.diagnostics = std::move(diagnostics);
    });
    TextDocumentItem item;
    item.uri = path_to_uri(path.string());
    item.text = text;
    item.version = 1;
    server.text_document_did_open(item);

    TextDocumentIdentifier id;
    id.uri = item.uri;
    const Position at = PositionMapper(text).byte_offset_to_position(text.find(name));
    for (const CodeLens& lens : server.text_document_code_lens(id).value_or(std::vector<CodeLens>{})) {
        if (lens.range.start.line == at.line && lens.range.start.character == at.character) {
            opened.lens = lens.title;
        }
    }
    if (const std::optional<Hover> found = server.text_document_hover(id, at)) {
        opened.hover = found->contents;
    }
    return opened;
}

// The client as an editor opens it, with the lens and hover over `twice`.
Opened open_client(const Project& project) {
    return open_document(project.client, kClient, "twice");
}

// Whether an error with `code` mentioning `part` was published. When none was,
// what was published is printed, so a failure says what the editor showed.
bool has_error_coded(const Opened& opened, const std::string& code, const std::string& part) {
    const bool found = std::ranges::any_of(opened.diagnostics, [&code, &part](const Diagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Error && diagnostic.code == code &&
               diagnostic.message.find(part) != std::string::npos;
    });
    if (!found) {
        for (const Diagnostic& diagnostic : opened.diagnostics) {
            std::cerr << "published: [" << diagnostic.code << "] " << diagnostic.message << "\n";
        }
    }
    return found;
}

bool has_interface_error(const Opened& opened, const std::string& part) {
    return has_error_coded(opened, "cppl.verification.interface", part);
}

bool has_error(const Opened& opened) {
    return std::ranges::any_of(opened.diagnostics, [](const Diagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Error;
    });
}

} // namespace

CPPL_TEST(a_call_is_verified_against_the_interface_the_build_imports) {
    const cppl::driver::ScratchDirectory scratch;
    const Project project(scratch.path(), "c++20");

    // With no database, the other unit's contract is unavailable, as it is to
    // the CLI given no interface.
    const Opened alone = open_client(project);
    CPPL_CHECK(has_interface_error(alone, "no imported verification interface records its contract"));
    CPPL_CHECK(!alone.lens.starts_with("PROVEN"));

    // With the build's command, the call uses the record, and the claim is
    // named as resting on it, never as proven outright.
    project.compiled_with("-std=c++20 --cppl-import-interface=counter.cppli");
    const Opened imported = open_client(project);
    CPPL_CHECK(!has_error(imported));
    CPPL_CHECK_EQ(imported.lens, std::string("PROVEN: all 3 obligations relative to the imported contract of step"));
    CPPL_CHECK(imported.hover.find("relative to the imported contract of step (from `" +
                                   project.interface.lexically_normal().string() + "`)") != std::string::npos);
}

CPPL_TEST(an_interface_the_editor_cannot_use_is_reported_and_none_of_its_contracts_is_used) {
    const cppl::driver::ScratchDirectory scratch;
    const Project project(scratch.path(), "c++20");

    // One the build does not have.
    project.compiled_with("-std=c++20 --cppl-import-interface=missing.cppli");
    const Opened missing = open_client(project);
    CPPL_CHECK(has_interface_error(missing, "cannot use verification interface"));
    CPPL_CHECK(!missing.lens.starts_with("PROVEN"));

    // One whose unit has changed since it was written.
    project.compiled_with("-std=c++20 --cppl-import-interface=counter.cppli");
    write(project.root / "lib" / "counter.cpp", std::string(kLibrary) + "// edited\n");
    const Opened stale = open_client(project);
    CPPL_CHECK(has_interface_error(stale, "it is stale"));
    CPPL_CHECK(!stale.lens.starts_with("PROVEN"));
}

CPPL_TEST(the_editor_compares_the_language_mode_its_build_compiles_in) {
    const cppl::driver::ScratchDirectory scratch;

    // Written in the default mode, read in c++20: another language mode.
    const Project defaulted(scratch.path() / "defaulted", "");
    defaulted.compiled_with("-std=c++20 --cppl-import-interface=counter.cppli");
    const Opened across = open_client(defaulted);
    CPPL_CHECK(has_interface_error(across, "another C++ language mode"));
    CPPL_CHECK(!across.lens.starts_with("PROVEN"));

    // The last `-std=` is the one Clang obeys, and the one compared.
    const Project later(scratch.path() / "later", "c++20");
    later.compiled_with("-std=c++17 -std=c++20 --cppl-import-interface=counter.cppli");
    CPPL_CHECK(open_client(later).lens.starts_with("PROVEN"));
    later.compiled_with("-std=c++20 -std=c++17 --cppl-import-interface=counter.cppli");
    CPPL_CHECK(has_interface_error(open_client(later), "another C++ language mode"));
}

// SPEC: ARITH-014 -- the editor verifies a document for the target its build
// compiles it for, as the CLI does, or not at all.
CPPL_TEST(the_editor_verifies_for_the_target_its_build_compiles_for) {
    const cppl::driver::ScratchDirectory scratch;
    // Written for x86-64 wherever the test runs, so that x32 is another target
    // on every host.
    const std::string x86_64 = "--target=x86_64-unknown-linux-gnu";
    const Project project(scratch.path(), "c++20", {x86_64});

    project.compiled_with("-std=c++20 " + x86_64 + " --cppl-import-interface=counter.cppli");
    CPPL_CHECK(open_client(project).lens.starts_with("PROVEN"));

    // The build compiles the client for x32, which this interface was not
    // produced for: the CLI refuses it, and so does the editor.
    project.compiled_with("-std=c++20 " + x86_64 + " -mx32 --cppl-import-interface=counter.cppli");
    const Opened x32 = open_client(project);
    CPPL_CHECK(has_interface_error(x32, "it was produced for another target"));
    CPPL_CHECK(!x32.lens.starts_with("PROVEN"));

    // True of x86-64's 64-bit `unsigned long` and false of x32's 32-bit one:
    // proven for the one build and not for the other.
    const std::string widened = "verified unsigned long widened(unsigned long x)\n"
                                "    expects (x == 4294967295ul)\n"
                                "    ensures (result > 4294967295ul)\n"
                                "{\n"
                                "    return x + 1ul;\n"
                                "}\n";
    const std::filesystem::path document = project.root / "src" / "widened.cpp";
    project.compiled_with("-std=c++20 " + x86_64);
    CPPL_CHECK(open_document(document, widened, "widened").lens.starts_with("PROVEN"));
    project.compiled_with("-std=c++20 " + x86_64 + " -mx32");
    const Opened narrow = open_document(document, widened, "widened");
    CPPL_CHECK(has_error_coded(narrow, "cppl.proof.failure", "does not satisfy its contract"));
    CPPL_CHECK(!narrow.lens.starts_with("PROVEN"));

    // An option the editor cannot pass on, which could select the target
    // itself, leaves the document unverified rather than verified without it.
    project.compiled_with("-std=c++20 " + x86_64 + " -Xclang -fno-signed-char");
    const Opened opaque = open_document(document, widened, "widened");
    CPPL_CHECK(has_error_coded(opaque, "cppl.unsupported", "is not verified in the editor: '-Xclang'"));
    CPPL_CHECK(!opaque.lens.starts_with("PROVEN"));
}
