// Compile flags: each document is read with the flags its build compiles it
// with, from the nearest `compile_commands.json`, by its editor unit and its
// compile alike (src/lsp/include/cppl/lsp/compile_commands.hpp).

#include "cppl/clang/editor.hpp"
#include "cppl/driver/scratch.hpp"
#include "cppl/lsp/compile_commands.hpp"
#include "cppl/lsp/position.hpp"
#include "cppl/lsp/protocol.hpp"
#include "cppl/lsp/server.hpp"
#include "cppl/lsp/uri.hpp"
#include "cppl/testing/test.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace cppl::lsp;

namespace {

void expect(const std::vector<std::string>& actual, const std::vector<std::string>& expected, int line) {
    if (actual != expected) {
        std::string got;
        for (const std::string& word : actual) {
            got += "[" + word + "]";
        }
        ::cppl::testing::fail(__FILE__, line, "got " + got);
    }
}

void write(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

bool has(const std::vector<std::string>& flags, const std::string& flag) {
    return std::ranges::find(flags, flag) != flags.end();
}

// The diagnostics published for `text` opened as `path`, errors only.
std::vector<Diagnostic> errors_of(const std::filesystem::path& path, const std::string& text) {
    Server server(CPPL_TEST_DEFAULT_CLANG);
    std::vector<Diagnostic> published;
    server.set_diagnostic_publisher(
        [&published](const std::string&, std::vector<Diagnostic> diagnostics) { published = std::move(diagnostics); });
    TextDocumentItem item;
    item.uri = path_to_uri(path.string());
    item.text = text;
    item.version = 1;
    server.text_document_did_open(item);
    std::erase_if(published,
                  [](const Diagnostic& diagnostic) { return diagnostic.severity != DiagnosticSeverity::Error; });
    return published;
}

} // namespace

CPPL_TEST(a_command_splits_as_a_shell_splits_it) {
    expect(split_command(R"(clang++  -DNAME="a b" -I'x y' -DQ=\"q\" a\ b.cpp)"),
           {"clang++", "-DNAME=a b", "-Ix y", "-DQ=\"q\"", "a b.cpp"}, __LINE__);
    expect(split_command(""), {}, __LINE__);
}

CPPL_TEST(only_what_changes_how_the_text_reads_is_kept) {
    expect(reading_flags({"clang++",
                          "-c",
                          "-o",
                          "out.o",
                          "-Iinclude",
                          "-I",
                          "other",
                          "-isystem",
                          "sys",
                          "-DX=1",
                          "-D",
                          "Y",
                          "-UZ",
                          "-std=c++20",
                          "-Wall",
                          "-Werror",
                          "-O2",
                          "-g",
                          "-MD",
                          "-MF",
                          "dep.d",
                          "--target=x86_64-linux-gnu",
                          "-fno-exceptions",
                          "-fsanitize=address",
                          "-x",
                          "c++",
                          "-include",
                          "pch.hpp",
                          "src/a.cpp"},
                         "/work/build"),
           {"-I/work/build/include", "-I", "/work/build/other", "-isystem", "/work/build/sys", "-DX=1", "-D", "Y",
            "-UZ", "-std=c++20", "-O2", "--target=x86_64-linux-gnu", "-fno-exceptions", "-include",
            "/work/build/pch.hpp"},
           __LINE__);
    // What follows `--` is input.
    expect(reading_flags({"cl", "-DA", "--", "-DB"}, "/"), {"-DA"}, __LINE__);
    // A database is the project's, not the server's: nothing in it loads code
    // into Clang or points it at other programs, and its compiler is never run.
    expect(reading_flags({"/tmp/evil-cc", "-fplugin=evil.so", "-Xclang", "-load", "-Xclang", "evil.so", "-B/tmp/evil",
                          "-B", "/tmp/evil", "-fpass-plugin=evil.so", "--gcc-toolchain=/tmp/evil", "-DKEPT"},
                         "/"),
           {"-DKEPT"}, __LINE__);
}

CPPL_TEST(the_nearest_database_gives_a_file_its_own_entry_or_the_most_alike) {
    const cppl::driver::ScratchDirectory scratch;
    const std::filesystem::path root = scratch.path() / "project";
    write(root / "build" / "compile_commands.json",
          R"([{"directory":")" + (root / "build").string() +
              R"(","file":"../src/a.cpp","command":"clang++ -DFROM_A -c ../src/a.cpp -o a.o"},)"
              R"({"directory":")" +
              (root / "build").string() + R"(","file":")" + (root / "lib" / "b.cpp").string() +
              R"(","arguments":["clang++","-DFROM_B","-c","b.cpp"]}])");
    CompileCommands commands;
    CPPL_CHECK(has(commands.flags_for((root / "src" / "a.cpp").string()), "-DFROM_A"));
    // A header takes the flags of the file its name matches, else of the file
    // nearest it.
    CPPL_CHECK(has(commands.flags_for((root / "include" / "a.hpp").string()), "-DFROM_A"));
    CPPL_CHECK(has(commands.flags_for((root / "lib" / "c.hpp").string()), "-DFROM_B"));
    // Outside every database, nothing.
    CPPL_CHECK(commands.flags_for((scratch.path() / "elsewhere.cpp").string()).empty());
    CPPL_CHECK(commands.flags_for("not/absolute.cpp").empty());

    // An edited database is read again.
    const std::filesystem::path database = root / "build" / "compile_commands.json";
    write(database, R"([{"directory":")" + (root / "build").string() +
                        R"(","file":"../src/a.cpp","command":"clang++ -DEDITED -c ../src/a.cpp"}])");
    std::filesystem::last_write_time(database, std::filesystem::last_write_time(database) + std::chrono::seconds(10));
    const std::vector<std::string> edited = commands.flags_for((root / "src" / "a.cpp").string());
    CPPL_CHECK(has(edited, "-DEDITED"));
    CPPL_CHECK(!has(edited, "-DFROM_A"));

    // A database that is not JSON gives nothing, rather than stale flags.
    write(database, "[{");
    std::filesystem::last_write_time(database, std::filesystem::last_write_time(database) + std::chrono::seconds(10));
    CPPL_CHECK(commands.flags_for((root / "src" / "a.cpp").string()).empty());
}

CPPL_TEST(a_document_is_compiled_and_navigated_with_its_builds_flags) {
    const cppl::driver::ScratchDirectory scratch;
    const std::filesystem::path root = scratch.path() / "project";
    write(root / "include" / "dep.hpp", "int dep();\n");
    const std::filesystem::path main = root / "src" / "main.cpp";
    const std::string text = "#include <dep.hpp>\nint use() { return dep() + LIMIT; }\n";

    // Without the build's flags, neither the header nor the macro is found.
    CPPL_CHECK(!errors_of(main, text).empty());

    write(root / "build" / "compile_commands.json",
          R"([{"directory":")" + (root / "build").string() +
              R"(","file":"../src/main.cpp","command":"clang++ -I../include -DLIMIT=3 -c ../src/main.cpp"}])");
    CPPL_CHECK(errors_of(main, text).empty());

    // The editor unit reads the same flags: `dep` leads into the header.
    Server server(CPPL_TEST_DEFAULT_CLANG);
    TextDocumentItem item;
    item.uri = path_to_uri(main.string());
    item.text = text;
    item.version = 1;
    server.text_document_did_open(item);
    TextDocumentIdentifier id;
    id.uri = item.uri;
    const std::optional<std::vector<Location>> found =
        server.text_document_navigate(cppl::clangbridge::Destination::Declaration, id,
                                      PositionMapper(text).byte_offset_to_position(text.find("dep()")));
    const bool one = found.has_value() && found->size() == 1;
    CPPL_CHECK(one);
    if (one) {
        CPPL_CHECK(found->front().uri.ends_with("/include/dep.hpp"));
    }
}

CPPL_TEST(a_quoted_include_is_found_beside_the_document) {
    const cppl::driver::ScratchDirectory scratch;
    write(scratch.path() / "sibling.hpp", "int sibling();\n");
    CPPL_CHECK(
        errors_of(scratch.path() / "main.cpp", "#include \"sibling.hpp\"\nint twice() { return sibling() * 2; }\n")
            .empty());
}
