// The workspace index: every file of a workspace read in the background, for
// workspace symbols and for references into files no open document holds
// (src/lsp/include/cppl/lsp/workspace_index.hpp).

#include "cppl/driver/scratch.hpp"
#include "cppl/lsp/editor_view.hpp"
#include "cppl/lsp/position.hpp"
#include "cppl/lsp/protocol.hpp"
#include "cppl/lsp/server.hpp"
#include "cppl/lsp/transport.hpp"
#include "cppl/lsp/uri.hpp"
#include "cppl/lsp/workspace_index.hpp"
#include "cppl/testing/test.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace cppl::lsp;

namespace {

// Reading a file runs Clang and the compile, so a sanitized build takes a
// while; the index is always given long enough.
constexpr std::chrono::seconds kIndexed(300);

void write(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

// Rewrites `path` as an edit on disk, with a time the index cannot mistake for
// the one it read.
void edit(const std::filesystem::path& path, const std::string& text) {
    const std::filesystem::file_time_type before = std::filesystem::last_write_time(path);
    write(path, text);
    std::filesystem::last_write_time(path, before + std::chrono::seconds(10));
}

WorkspaceIndex::Options options() {
    WorkspaceIndex::Options options;
    options.driver = resolve_driver(CPPL_TEST_DEFAULT_CLANG);
    options.clang = CPPL_TEST_DEFAULT_CLANG;
    options.clang_arguments = {"-std=c++20"};
    return options;
}

std::string file_of(const std::string& uri) {
    return uri.substr(uri.rfind('/') + 1);
}

// `name` for each symbol, with `container::` before it when it has one, and
// `@file` after, in the order given.
std::string names(const std::vector<WorkspaceIndex::Symbol>& symbols) {
    std::string out;
    for (const WorkspaceIndex::Symbol& symbol : symbols) {
        out += (out.empty() ? "" : " ") + (symbol.container.empty() ? "" : symbol.container + "::") + symbol.name +
               "@" + file_of(symbol.location.uri);
    }
    return out;
}

// The same, sorted, for what is found in no particular order.
std::string sorted_names(std::vector<WorkspaceIndex::Symbol> symbols) {
    std::ranges::sort(symbols, [](const WorkspaceIndex::Symbol& lhs, const WorkspaceIndex::Symbol& rhs) {
        return lhs.name < rhs.name;
    });
    return names(symbols);
}

void open(Server& server, const std::filesystem::path& path, const std::string& text) {
    TextDocumentItem item;
    item.uri = path_to_uri(path.string());
    item.text = text;
    item.version = 1;
    server.text_document_did_open(item);
}

Position position_of(const std::string& text, const std::string& needle, std::size_t into = 0) {
    const std::size_t at = text.find(needle);
    if (at == std::string::npos) {
        ::cppl::testing::fail(__FILE__, __LINE__, "no '" + needle + "' in the text");
    }
    return PositionMapper(text).byte_offset_to_position(at + into);
}

std::string at(const std::string& file, const std::string& text, const std::string& needle, std::size_t into = 0) {
    const Position position = position_of(text, needle, into);
    return file + ":" + std::to_string(position.line) + ":" + std::to_string(position.character);
}

std::string references(Server& server, const std::filesystem::path& path, const Position& position,
                       bool declarations = true) {
    TextDocumentIdentifier id;
    id.uri = path_to_uri(path.string());
    const std::optional<std::vector<Location>> found = server.text_document_references(id, position, declarations);
    if (!found.has_value()) {
        return "null";
    }
    std::string out;
    for (const Location& location : *found) {
        out += (out.empty() ? "" : " ") + file_of(location.uri) + ":" + std::to_string(location.range.start.line) +
               ":" + std::to_string(location.range.start.character);
    }
    return out;
}

std::string framed(const std::string& body) {
    return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}

// The workspace symbols matching `query` once a server initialized with
// `params` over the wire has indexed its workspace.
std::string symbols_after_initialize(const std::string& params, const std::string& query) {
    Server server(CPPL_TEST_DEFAULT_CLANG, {"-std=c++20"});
    std::istringstream input(framed(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":)" + params + "}"));
    std::ostringstream output;
    std::ostringstream log;
    [[maybe_unused]] const int exit_code = run_transport(server, input, output, log);
    CPPL_CHECK(output.str().find(R"("workspaceSymbolProvider":true)") != std::string::npos);
    CPPL_CHECK(server.wait_for_index(kIndexed));
    return sorted_names(server.workspace_symbols(query));
}

} // namespace

CPPL_TEST(a_name_matches_a_query_by_how_much_of_it_the_query_spells) {
    CPPL_CHECK_EQ(WorkspaceIndex::match("parse_header", "Parse_Header"), 4);
    CPPL_CHECK_EQ(WorkspaceIndex::match("parse_header", "PARSE"), 3);
    CPPL_CHECK_EQ(WorkspaceIndex::match("parse_header", "head"), 2);
    CPPL_CHECK_EQ(WorkspaceIndex::match("parse_header", "phd"), 1);
    CPPL_CHECK_EQ(WorkspaceIndex::match("parse_header", "hp"), 0);
    CPPL_CHECK_EQ(WorkspaceIndex::match("parse_header", "parse_headers"), 0);
    CPPL_CHECK_EQ(WorkspaceIndex::match("anything", ""), 1);
}

CPPL_TEST(symbols_are_ranked_best_match_first_then_by_name_and_listed_once) {
    const auto symbol = [](const std::string& name, const std::string& uri, std::uint32_t line) {
        return WorkspaceIndex::Symbol{name, "", SymbolKind::Function, Location{uri, Range{{line, 0}, {line, 1}}}};
    };
    std::vector<WorkspaceIndex::Symbol> symbols = {
        symbol("reheader", "file:///a.cpp", 0), symbol("header_b", "file:///a.cpp", 1),
        symbol("h_e_a_d", "file:///a.cpp", 2),  symbol("head", "file:///a.cpp", 3),
        symbol("header_a", "file:///a.cpp", 4), symbol("head", "file:///b.cpp", 3),
        symbol("head", "file:///a.cpp", 3),
    };
    WorkspaceIndex::rank(symbols, "head");
    CPPL_CHECK_EQ(names(symbols), std::string("head@a.cpp head@b.cpp header_a@a.cpp header_b@a.cpp reheader@a.cpp "
                                              "h_e_a_d@a.cpp"));
}

CPPL_TEST(every_source_under_a_root_is_indexed_but_hidden_package_build_and_nested_trees) {
    const cppl::driver::ScratchDirectory scratch;
    const std::filesystem::path root = scratch.path() / "project";
    write(root / "src" / "a.cpp", "int in_source();\n");
    write(root / "include" / "b.HPP", "int in_header();\n");
    write(root / "c.cppl", "int in_cppl();\n");
    write(root / ".cache" / "c.cpp", "int in_hidden();\n");
    write(root / "node_modules" / "d.cpp", "int in_packages();\n");
    write(root / "out" / "CMakeCache.txt", "");
    write(root / "out" / "e.cpp", "int in_build();\n");
    std::filesystem::create_directories(root / "vendor" / ".git");
    write(root / "vendor" / "f.cpp", "int in_nested_repository();\n");
    write(root / "notes.txt", "int in_text();\n");
    // A file the build compiles is indexed wherever it lives.
    write(scratch.path() / "outside" / "g.cpp", "int in_listed();\n");
    write(root / "compile_commands.json",
          R"([{"directory":")" + root.string() +
              R"(","file":"../outside/g.cpp","command":"clang++ -c ../outside/g.cpp"}])");

    WorkspaceIndex index({root}, options());
    CPPL_CHECK(index.wait_until_current(kIndexed));
    CPPL_CHECK_EQ(sorted_names(index.symbols("in_", 100)),
                  std::string("in_cppl@c.cppl in_header@b.HPP in_listed@g.cpp in_source@a.cpp"));
    // At most as many as asked for, best first.
    CPPL_CHECK_EQ(names(index.symbols("in_source", 1)), std::string("in_source@a.cpp"));
    CPPL_CHECK(index.symbols("in_", 0).empty());
}

CPPL_TEST(each_file_is_indexed_with_its_builds_flags_and_its_declarations_nested) {
    const cppl::driver::ScratchDirectory scratch;
    const std::filesystem::path root = scratch.path() / "project";
    write(root / "include" / "config.hpp", "#pragma once\nnamespace shapes { struct Circle { double radius; }; }\n");
    write(root / "src" / "feature.cpp", "#include <config.hpp>\n"
                                        "#ifdef FEATURE\n"
                                        "int with_feature(shapes::Circle circle);\n"
                                        "#endif\n");
    write(root / "build" / "compile_commands.json",
          R"([{"directory":")" + (root / "build").string() +
              R"(","file":"../src/feature.cpp","command":"clang++ -I../include -DFEATURE -c ../src/feature.cpp"}])");

    WorkspaceIndex index({root}, options());
    CPPL_CHECK(index.wait_until_current(kIndexed));
    CPPL_CHECK_EQ(names(index.symbols("with_feature", 10)), std::string("with_feature@feature.cpp"));
    const std::vector<WorkspaceIndex::Symbol> radius = index.symbols("radius", 10);
    CPPL_CHECK_EQ(names(radius), std::string("shapes::Circle::radius@config.hpp"));
    if (radius.size() == 1) {
        CPPL_CHECK(radius.front().kind == SymbolKind::Field);
        CPPL_CHECK_EQ(radius.front().location.range.start.line, std::uint32_t{1});
    }
}

CPPL_TEST(an_edit_on_disk_is_read_again_and_a_removed_file_forgotten) {
    const cppl::driver::ScratchDirectory scratch;
    const std::filesystem::path root = scratch.path() / "project";
    write(root / "kept.cpp", "int first_name();\n");
    write(root / "removed.cpp", "int going_away();\n");

    WorkspaceIndex index({root}, options());
    CPPL_CHECK(index.wait_until_current(kIndexed));
    CPPL_CHECK_EQ(names(index.symbols("first_name", 10)), std::string("first_name@kept.cpp"));
    CPPL_CHECK_EQ(names(index.symbols("going_away", 10)), std::string("going_away@removed.cpp"));

    edit(root / "kept.cpp", "int second_name();\n");
    std::filesystem::rename(root / "removed.cpp", scratch.path() / "removed.cpp");
    write(root / "added.cpp", "int newly_added();\n");
    CPPL_CHECK(index.wait_until_current(kIndexed));
    CPPL_CHECK(index.symbols("first_name", 10).empty());
    CPPL_CHECK(index.symbols("going_away", 10).empty());
    CPPL_CHECK_EQ(names(index.symbols("second_name", 10)), std::string("second_name@kept.cpp"));
    CPPL_CHECK_EQ(names(index.symbols("newly_added", 10)), std::string("newly_added@added.cpp"));
}

CPPL_TEST(indexing_reports_each_file_read_out_of_those_found) {
    const cppl::driver::ScratchDirectory scratch;
    const std::filesystem::path root = scratch.path() / "project";
    write(root / "one.cpp", "int one();\n");
    write(root / "two.cpp", "int two();\n");
    std::vector<std::pair<std::size_t, std::size_t>> reported;
    {
        WorkspaceIndex index({root}, options(),
                             [&reported](std::size_t done, std::size_t total) { reported.emplace_back(done, total); });
        CPPL_CHECK(index.wait_until_current(kIndexed));
    }
    const std::vector<std::pair<std::size_t, std::size_t>> expected = {{0, 2}, {1, 2}, {2, 2}};
    CPPL_CHECK(reported == expected);
}

CPPL_TEST(a_workspace_symbol_comes_from_the_open_buffer_and_the_index_for_every_other_file) {
    const cppl::driver::ScratchDirectory scratch;
    const std::filesystem::path root = scratch.path() / "project";
    write(root / "edited.cpp", "int before_edit();\n");
    write(root / "closed.cpp", "int still_closed();\n");

    Server server(CPPL_TEST_DEFAULT_CLANG, {"-std=c++20"});
    server.initialize({}, {root.string()});
    CPPL_CHECK(server.wait_for_index(kIndexed));
    // The buffer, not the file on disk, answers for an open document.
    open(server, root / "edited.cpp", "int after_edit();\n");
    CPPL_CHECK_EQ(names(server.workspace_symbols("edit")), std::string("after_edit@edited.cpp"));
    CPPL_CHECK_EQ(names(server.workspace_symbols("still_closed")), std::string("still_closed@closed.cpp"));
    CPPL_CHECK(server.workspace_symbols("nothing_is_named_this").empty());

    // Shut down, the server indexes nothing more.
    server.shutdown();
    CPPL_CHECK(!server.indexing());
}

CPPL_TEST(without_a_workspace_workspace_symbols_are_the_open_documents) {
    Server server(CPPL_TEST_DEFAULT_CLANG, {"-std=c++20"});
    server.initialize();
    CPPL_CHECK(!server.indexing());
    CPPL_CHECK(server.wait_for_index(std::chrono::milliseconds(0)));
    open(server, "/work/only.cpp", "namespace outer { int only_open(); }\n");
    CPPL_CHECK_EQ(names(server.workspace_symbols("only")), std::string("outer::only_open@only.cpp"));
}

CPPL_TEST(references_reach_files_no_open_document_holds) {
    const cppl::driver::ScratchDirectory scratch;
    const std::filesystem::path root = scratch.path() / "project";
    const std::string header = "#pragma once\nint shared_value();\n";
    const std::string first = "#include \"shared.hpp\"\nint first() { return shared_value(); }\n";
    const std::string second = "#include \"shared.hpp\"\nint second() { return shared_value() + 1; }\n";
    write(root / "shared.hpp", header);
    write(root / "first.cpp", first);
    write(root / "second.cpp", second);
    write(root / "unrelated.cpp", "int shared_value_elsewhere();\nint third() { return shared_value_elsewhere(); }\n");

    Server server(CPPL_TEST_DEFAULT_CLANG, {"-std=c++20"});
    server.initialize({}, {root.string()});
    CPPL_CHECK(server.wait_for_index(kIndexed));
    open(server, root / "first.cpp", first);
    const Position use = position_of(first, "shared_value");
    CPPL_CHECK_EQ(references(server, root / "first.cpp", use), at("first.cpp", first, "shared_value") + " " +
                                                                   at("second.cpp", second, "shared_value") + " " +
                                                                   at("shared.hpp", header, "shared_value"));
    // Without declarations: the header's is left out, wherever it was found.
    CPPL_CHECK_EQ(references(server, root / "first.cpp", use, false),
                  at("first.cpp", first, "shared_value") + " " + at("second.cpp", second, "shared_value"));

    // A closed file edited on disk is read again; an open one answers as the
    // editor holds it, whatever the disk says.
    const std::string second_edited = "#include \"shared.hpp\"\n\nint second() { return 2 * shared_value(); }\n";
    edit(root / "second.cpp", second_edited);
    edit(root / "first.cpp", "#include \"shared.hpp\"\n\n\nint first() { return shared_value(); }\n");
    CPPL_CHECK(server.wait_for_index(kIndexed));
    CPPL_CHECK_EQ(references(server, root / "first.cpp", use, false),
                  at("first.cpp", first, "shared_value") + " " + at("second.cpp", second_edited, "shared_value"));
}

CPPL_TEST(a_proof_statement_in_a_closed_file_is_a_use_of_the_law_it_names) {
    const cppl::driver::ScratchDirectory scratch;
    const std::filesystem::path root = scratch.path() / "project";
    const std::string laws = "#pragma once\n"
                             "\n"
                             "pure unsigned zero() {\n"
                             "    return 0u;\n"
                             "}\n"
                             "\n"
                             "trusted law sensor_identity(unsigned x)\n"
                             "    proves (x + zero() == x);\n";
    const std::string use = "#include \"laws.hpp\"\n"
                            "\n"
                            "law identity_holds(unsigned y)\n"
                            "    proves (y + zero() == y)\n"
                            "{\n"
                            "    exact sensor_identity(y);\n"
                            "}\n";
    write(root / "laws.hpp", laws);
    write(root / "use.cpp", use);

    Server server(CPPL_TEST_DEFAULT_CLANG, {"-std=c++20"});
    server.initialize({}, {root.string()});
    CPPL_CHECK(server.wait_for_index(kIndexed));
    open(server, root / "laws.hpp", laws);
    CPPL_CHECK_EQ(references(server, root / "laws.hpp", position_of(laws, "sensor_identity")),
                  at("laws.hpp", laws, "sensor_identity") + " " + at("use.cpp", use, "sensor_identity"));

    // Opened and edited, the file answers as the editor holds it.
    const std::string moved = "\n\n" + use;
    open(server, root / "use.cpp", moved);
    CPPL_CHECK_EQ(references(server, root / "laws.hpp", position_of(laws, "sensor_identity")),
                  at("laws.hpp", laws, "sensor_identity") + " " + at("use.cpp", moved, "sensor_identity"));
}

CPPL_TEST(the_workspace_is_each_folder_the_client_opened_or_else_its_root) {
    const cppl::driver::ScratchDirectory scratch;
    write(scratch.path() / "first" / "a.cpp", "int from_first();\n");
    write(scratch.path() / "second" / "b.cpp", "int from_second();\n");
    write(scratch.path() / "root" / "c.cpp", "int from_root();\n");
    const std::string first = path_to_uri((scratch.path() / "first").string());
    const std::string second = path_to_uri((scratch.path() / "second").string());
    const std::string root = path_to_uri((scratch.path() / "root").string());

    CPPL_CHECK_EQ(symbols_after_initialize(R"({"workspaceFolders":[{"uri":")" + first +
                                               R"(","name":"first"},)"
                                               R"({"uri":")" +
                                               second + R"(","name":"second"}],"rootUri":")" + root + R"("})",
                                           "from_"),
                  std::string("from_first@a.cpp from_second@b.cpp"));
    CPPL_CHECK_EQ(symbols_after_initialize(R"({"workspaceFolders":null,"rootUri":")" + root + R"("})", "from_"),
                  std::string("from_root@c.cpp"));
    CPPL_CHECK_EQ(
        symbols_after_initialize(R"({"rootPath":")" + (scratch.path() / "second").string() + R"("})", "from_"),
        std::string("from_second@b.cpp"));
    CPPL_CHECK(symbols_after_initialize(R"({"rootUri":null})", "from_").empty());
}
