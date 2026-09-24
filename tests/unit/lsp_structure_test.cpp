// Folding and selection ranges: the structure Clang parsed and the C++L
// structure the recognizer found, traced back to the text as written
// (src/lsp/include/cppl/lsp/editor_view.hpp, compiler/frontend/include/cppl/
// frontend/structure.hpp).

#include "cppl/lsp/position.hpp"
#include "cppl/lsp/protocol.hpp"
#include "cppl/lsp/server.hpp"
#include "cppl/lsp/transport.hpp"
#include "cppl/testing/test.hpp"

#include <cstddef>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using namespace cppl::lsp;

namespace {

const std::string kUri = "file:///work/structure.cpp";

void expect(const std::string& actual, const std::string& expected, int line) {
    if (actual != expected) {
        ::cppl::testing::fail(__FILE__, line, "got '" + actual + "', expected '" + expected + "'");
    }
}

void open(Server& server, const std::string& text) {
    TextDocumentItem item;
    item.uri = kUri;
    item.text = text;
    item.version = 1;
    server.text_document_did_open(item);
}

// Each fold as `start-end`, with `:character` where one is sent and the kind
// in parentheses.
std::string folds_of(const std::string& text, bool line_folding_only = true) {
    Server server(CPPL_TEST_DEFAULT_CLANG, {"-std=c++20"});
    ClientCapabilities capabilities;
    capabilities.line_folding_only = line_folding_only;
    server.initialize(capabilities);
    open(server, text);
    TextDocumentIdentifier id;
    id.uri = kUri;
    const std::optional<std::vector<FoldingRange>> folds = server.text_document_folding_range(id);
    if (!folds.has_value()) {
        return "null";
    }
    std::string out;
    for (const FoldingRange& fold : *folds) {
        out += out.empty() ? "" : " ";
        out += std::to_string(fold.start_line);
        if (fold.start_character.has_value()) {
            out += ":" + std::to_string(*fold.start_character);
        }
        out += "-" + std::to_string(fold.end_line);
        if (fold.end_character.has_value()) {
            out += ":" + std::to_string(*fold.end_character);
        }
        if (!fold.kind.empty()) {
            out += "(" + fold.kind + ")";
        }
    }
    return out;
}

std::string describe(const Range& range) {
    return std::to_string(range.start.line) + ":" + std::to_string(range.start.character) + "-" +
           std::to_string(range.end.line) + ":" + std::to_string(range.end.character);
}

// What selecting outward from `|` in `text` selects, innermost first.
std::string selection_at(std::string text) {
    const std::size_t cursor = text.find('|');
    if (cursor == std::string::npos) {
        ::cppl::testing::fail(__FILE__, __LINE__, "the text marks no cursor");
    }
    text.erase(cursor, 1);
    Server server(CPPL_TEST_DEFAULT_CLANG, {"-std=c++20"});
    open(server, text);
    TextDocumentIdentifier id;
    id.uri = kUri;
    const std::optional<std::vector<std::vector<Range>>> chains =
        server.text_document_selection_range(id, {PositionMapper(text).byte_offset_to_position(cursor)});
    if (!chains.has_value() || chains->size() != 1) {
        return "null";
    }
    std::string out;
    for (const Range& range : chains->front()) {
        out += (out.empty() ? "" : " ") + describe(range);
    }
    return out;
}

} // namespace

CPPL_TEST(every_cpp_body_and_include_run_folds) {
    const std::string text = "#include <cstddef>\n"
                             "#include <vector>\n"
                             "\n"
                             "namespace shapes {\n"
                             "\n"
                             "struct Box {\n"
                             "    int width;\n"
                             "    int height;\n"
                             "};\n"
                             "\n"
                             "enum class Side {\n"
                             "    left,\n"
                             "    right,\n"
                             "};\n"
                             "\n"
                             "int area(const Box& box) {\n"
                             "    auto scaled = [](int side) {\n"
                             "        return side * 2;\n"
                             "    };\n"
                             "    return scaled(box.width) * box.height;\n"
                             "}\n"
                             "\n"
                             "} // namespace shapes\n";
    // A client that folds whole lines keeps each closing brace in view.
    expect(folds_of(text), "0-1(imports) 3-21 5-7 10-12 15-19 16-17", __LINE__);
}

CPPL_TEST(a_body_in_an_expression_a_macro_begins_still_folds) {
    // Clang places an expression that begins with a macro in the macro, not in
    // the file, though the file writes the lambda inside it.
    expect(folds_of("#define ONE 1\n"
                    "int f() {\n"
                    "    return ONE + [] {\n"
                    "        return 2;\n"
                    "    }();\n"
                    "}\n"),
           "1-4 2-3", __LINE__);
}

CPPL_TEST(a_client_that_folds_within_lines_folds_between_the_braces) {
    expect(folds_of("int f() {\n    return 1;\n}\n", false), "0:9-2:0", __LINE__);
    expect(folds_of("int f() {\n    return 1;\n}\n", true), "0-1", __LINE__);
    // Nothing to fold between braces on neighbouring lines, for a client that
    // folds whole lines.
    expect(folds_of("int f() { int a = 1;\n    return a; }\n", true), "", __LINE__);
}

CPPL_TEST(comments_and_conditional_branches_fold) {
    const std::string text = "/* A comment\n"
                             "   over two lines. */\n"
                             "// A run\n"
                             "// of three\n"
                             "// line comments.\n"
                             "int value = 1; // after code\n"
                             "// is not part of\n"
                             "// the run after it.\n"
                             "#if defined(FAST)\n"
                             "int speed = 2;\n"
                             "#elif defined(SLOW)\n"
                             "int speed = 1;\n"
                             "#else\n"
                             "int speed = 0;\n"
                             "#endif\n";
    expect(folds_of(text), "0-1(comment) 2-4(comment) 6-7(comment) 8-9(region) 10-11(region) 12-13(region)", __LINE__);
    // Code after a comment's end is never folded away with it.
    expect(folds_of("/* two\n   lines */\nint y = 0;\n"), "0-1(comment)", __LINE__);
    expect(folds_of("/* two\n   lines */ int x = 0;\nint y = 0;\n"), "", __LINE__);
}

CPPL_TEST(cppl_bodies_arms_and_declarations_fold_as_the_recognizer_found_them) {
    const std::string text = "law holds(int x)\n"
                             "    proves (x == x);\n"
                             "\n"
                             "law holds_twice(int x)\n"
                             "    proves (x == x)\n"
                             "{\n"
                             "    refl;\n"
                             "}\n"
                             "\n"
                             "proof split(int x)\n"
                             "    proves (x == x)\n"
                             "{\n"
                             "    cases x {\n"
                             "        State::one => {\n"
                             "            refl;\n"
                             "        }\n"
                             "    }\n"
                             "    exact holds(x);\n"
                             "}\n"
                             "\n"
                             "type Small = int\n"
                             "    where (self < 10);\n";
    // Nothing the projection generated folds: its braces were never written.
    expect(folds_of(text), "0-1 5-6 11-17 12-15 13-14 20-21", __LINE__);
}

CPPL_TEST(a_cpp_selection_grows_through_what_clang_parsed) {
    expect(selection_at("int add(int left, int right);\n"
                        "int main() {\n"
                        "    return add(|1, 2);\n"
                        "}\n"),
           "2:15-2:16 2:11-2:20 2:4-2:20 1:11-3:1 1:0-3:1", __LINE__);
}

CPPL_TEST(a_proof_selection_grows_through_what_the_recognizer_found) {
    // The word, the statement, the arm's body, the arm, the arms, the `cases`
    // statement, the body and the proof.
    expect(selection_at("law holds(int x)\n"
                        "    proves (x == x);\n"
                        "proof twice(int x)\n"
                        "    proves (x == x)\n"
                        "{\n"
                        "    cases x {\n"
                        "        State::one => {\n"
                        "            exact |holds(x);\n"
                        "        }\n"
                        "    }\n"
                        "}\n"),
           "7:18-7:23 7:12-7:27 6:22-8:9 6:8-8:9 5:12-9:5 5:4-9:5 4:0-10:1 2:0-10:1", __LINE__);
}

CPPL_TEST(a_clause_selection_joins_clangs_expression_to_the_recognizers_clause) {
    // Clang parsed the proposition where the projection copied it; the clause
    // and the Law around it are the recognizer's.
    expect(selection_at("law holds(int x)\n"
                        "    proves (x == |x);\n"),
           "1:17-1:18 1:12-1:18 1:4-1:19 0:0-1:20", __LINE__);
}

CPPL_TEST(structure_requests_on_the_wire) {
    Server server(CPPL_TEST_DEFAULT_CLANG, {"-std=c++20"});
    const auto framed = [](const std::string& body) {
        return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
    };
    std::istringstream input(
        framed(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"capabilities":)"
               R"({"textDocument":{"foldingRange":{"lineFoldingOnly":true}}}}})") +
        framed(R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":)"
               R"({"uri":"file:///work/wire.cpp","languageId":"cpp","version":1,)"
               R"("text":"int f() {\n    return 1;\n}\n"}}})") +
        framed(R"({"jsonrpc":"2.0","id":2,"method":"textDocument/foldingRange","params":{"textDocument":)"
               R"({"uri":"file:///work/wire.cpp"}}})") +
        framed(R"({"jsonrpc":"2.0","id":3,"method":"textDocument/selectionRange","params":{"textDocument":)"
               R"({"uri":"file:///work/wire.cpp"},"positions":[{"line":1,"character":11}]}})") +
        framed(R"({"jsonrpc":"2.0","id":4,"method":"textDocument/selectionRange","params":{"textDocument":)"
               R"({"uri":"file:///work/wire.cpp"}}})") +
        framed(R"({"jsonrpc":"2.0","method":"exit"})"));
    std::ostringstream output;
    std::ostringstream log;
    [[maybe_unused]] const int exit_code = run_transport(server, input, output, log);
    const std::string written = output.str();
    CPPL_CHECK(written.find(R"("foldingRangeProvider":true)") != std::string::npos);
    CPPL_CHECK(written.find(R"("selectionRangeProvider":true)") != std::string::npos);
    CPPL_CHECK(written.find(R"("id":2,"result":[{"startLine":0,"endLine":1}])") != std::string::npos);
    CPPL_CHECK(written.find(R"json("id":3,"result":[{"range":{"start":{"line":1,"character":11},)json"
                            R"json("end":{"line":1,"character":12}},"parent":{"range":{"start":{"line":1,)json"
                            R"json("character":4},"end":{"line":1,"character":12}},"parent":)json") !=
               std::string::npos);
    CPPL_CHECK(written.find(R"("id":4,"error":{"code":-32602)") != std::string::npos);
}

CPPL_TEST(more_positions_than_any_editor_has_are_refused) {
    const auto asked = [](std::size_t count) {
        Server server(CPPL_TEST_DEFAULT_CLANG, {"-std=c++20"});
        std::string positions;
        for (std::size_t index = 0; index < count; ++index) {
            positions += std::string(index == 0 ? "" : ",") + R"({"line":0,"character":0})";
        }
        const auto framed = [](const std::string& body) {
            return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
        };
        std::istringstream input(
            framed(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})") +
            framed(R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":)"
                   R"({"uri":"file:///work/many.cpp","languageId":"cpp","version":1,"text":"int x;\n"}}})") +
            framed(R"({"jsonrpc":"2.0","id":2,"method":"textDocument/selectionRange","params":{"textDocument":)"
                   R"({"uri":"file:///work/many.cpp"},"positions":[)" +
                   positions + "]}}") +
            framed(R"({"jsonrpc":"2.0","method":"exit"})"));
        std::ostringstream output;
        std::ostringstream log;
        [[maybe_unused]] const int exit_code = run_transport(server, input, output, log);
        return output.str().find(R"("id":2,"error":{"code":-32602)") != std::string::npos;
    };
    CPPL_CHECK(!asked(4096));
    CPPL_CHECK(asked(4097));
}
