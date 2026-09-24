// References and document highlights: every place a name is written, found by
// Clang in each open document's unit and traced back to the text as written
// (src/lsp/include/cppl/lsp/editor_view.hpp).

#include "cppl/lsp/position.hpp"
#include "cppl/lsp/protocol.hpp"
#include "cppl/lsp/server.hpp"
#include "cppl/testing/test.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

using namespace cppl::lsp;

namespace {

Server make_server() {
    return Server(CPPL_TEST_DEFAULT_CLANG, {"-std=c++20", "-I/work/include"});
}

void open(Server& server, const std::string& uri, const std::string& text) {
    TextDocumentItem item;
    item.uri = uri;
    item.text = text;
    item.version = 1;
    server.text_document_did_open(item);
}

Position position_of(const std::string& text, const std::string& needle, std::size_t nth = 0) {
    std::size_t at = text.find(needle);
    for (std::size_t skipped = 0; skipped < nth && at != std::string::npos; ++skipped) {
        at = text.find(needle, at + 1);
    }
    if (at == std::string::npos) {
        ::cppl::testing::fail(__FILE__, __LINE__, "no occurrence " + std::to_string(nth) + " of '" + needle + "'");
    }
    return PositionMapper(text).byte_offset_to_position(at);
}

// `file:line:character` of where each location starts.
std::string starts(const std::optional<std::vector<Location>>& locations) {
    if (!locations.has_value()) {
        return "null";
    }
    std::string out;
    for (const Location& location : *locations) {
        const std::size_t slash = location.uri.rfind('/');
        out += (out.empty() ? "" : " ") + location.uri.substr(slash + 1) + ":" +
               std::to_string(location.range.start.line) + ":" + std::to_string(location.range.start.character);
    }
    return out;
}

std::string references(Server& server, const std::string& uri, const Position& position, bool declarations = true) {
    TextDocumentIdentifier id;
    id.uri = uri;
    return starts(server.text_document_references(id, position, declarations));
}

// `line:character:kind` of each highlight.
std::string highlights(Server& server, const std::string& uri, const Position& position) {
    TextDocumentIdentifier id;
    id.uri = uri;
    const auto found = server.text_document_document_highlight(id, position);
    if (!found.has_value()) {
        return "null";
    }
    std::string out;
    for (const DocumentHighlight& highlight : *found) {
        out += (out.empty() ? "" : " ") + std::to_string(highlight.range.start.line) + ":" +
               std::to_string(highlight.range.start.character) + ":" + std::to_string(static_cast<int>(highlight.kind));
    }
    return out;
}

std::string at(const std::string& file, const std::string& text, const std::string& needle, std::size_t nth = 0) {
    const Position position = position_of(text, needle, nth);
    return file + ":" + std::to_string(position.line) + ":" + std::to_string(position.character);
}

} // namespace

CPPL_TEST(a_local_is_referenced_where_it_is_declared_read_and_written) {
    Server server = make_server();
    const std::string text = "int main() {\n"
                             "    int count = 0;\n"
                             "    count = 5;\n"
                             "    count += 1;\n"
                             "    count++;\n"
                             "    return count;\n"
                             "}\n";
    open(server, "file:///work/main.cpp", text);
    const std::string all = at("main.cpp", text, "count", 0) + " " + at("main.cpp", text, "count", 1) + " " +
                            at("main.cpp", text, "count", 2) + " " + at("main.cpp", text, "count", 3) + " " +
                            at("main.cpp", text, "count", 4);
    CPPL_CHECK_EQ(references(server, "file:///work/main.cpp", position_of(text, "count", 4)), all);
    CPPL_CHECK_EQ(references(server, "file:///work/main.cpp", position_of(text, "count", 0), false),
                  all.substr(all.find(' ') + 1));
    // A declaration, three writes and a read.
    CPPL_CHECK_EQ(highlights(server, "file:///work/main.cpp", position_of(text, "count", 2)),
                  std::string("1:8:1 2:4:3 3:4:3 4:4:3 5:11:2"));
}

CPPL_TEST(references_and_the_outline_follow_each_edit) {
    // What a parse found is kept for the requests after it, and only until
    // the text is parsed again.
    Server server = make_server();
    const std::string before = "int total = 0;\nint read() { return total; }\n";
    open(server, "file:///work/edited.cpp", before);
    CPPL_CHECK_EQ(references(server, "file:///work/edited.cpp", position_of(before, "total")),
                  at("edited.cpp", before, "total", 0) + " " + at("edited.cpp", before, "total", 1));
    TextDocumentIdentifier id;
    id.uri = "file:///work/edited.cpp";
    CPPL_CHECK_EQ(server.text_document_document_symbol(id).value_or(std::vector<DocumentSymbol>{}).size(),
                  std::size_t{2});

    const std::string after = "int total = 0;\n\nint read() { return total; }\nint twice() { return total * 2; }\n";
    VersionedTextDocumentIdentifier versioned;
    versioned.uri = "file:///work/edited.cpp";
    versioned.version = 2;
    TextDocumentContentChangeEvent change;
    change.text = after;
    server.text_document_did_change(versioned, {change});
    CPPL_CHECK_EQ(references(server, "file:///work/edited.cpp", position_of(after, "total")),
                  at("edited.cpp", after, "total", 0) + " " + at("edited.cpp", after, "total", 1) + " " +
                      at("edited.cpp", after, "total", 2));
    CPPL_CHECK_EQ(server.text_document_document_symbol(id).value_or(std::vector<DocumentSymbol>{}).size(),
                  std::size_t{3});
}

CPPL_TEST(references_reach_every_open_document_and_the_headers_they_share) {
    Server server = make_server();
    const std::string header = "#pragma once\n"
                               "int shared_value();\n";
    const std::string first = "#include \"shared.hpp\"\n"
                              "int first() { return shared_value(); }\n";
    const std::string second = "#include \"shared.hpp\"\n"
                               "int second() { return shared_value() + 1; }\n";
    open(server, "file:///work/include/shared.hpp", header);
    open(server, "file:///work/first.cpp", first);
    open(server, "file:///work/second.cpp", second);
    // Ordered by document: `.../first.cpp`, `.../include/shared.hpp`, `.../second.cpp`.
    CPPL_CHECK_EQ(references(server, "file:///work/first.cpp", position_of(first, "shared_value")),
                  at("first.cpp", first, "shared_value") + " " + at("shared.hpp", header, "shared_value") + " " +
                      at("second.cpp", second, "shared_value"));
}

CPPL_TEST(a_member_is_referenced_through_access_and_initialization) {
    Server server = make_server();
    const std::string text = "struct Point {\n"
                             "    int x;\n"
                             "    Point() : x(1) {}\n"
                             "};\n"
                             "int read(Point p) { return p.x; }\n";
    open(server, "file:///work/main.cpp", text);
    CPPL_CHECK_EQ(references(server, "file:///work/main.cpp", position_of(text, "x;")),
                  at("main.cpp", text, "x;") + " " + at("main.cpp", text, "x(1)") + " " + at("main.cpp", text, "x; }"));
}

CPPL_TEST(a_macro_is_referenced_where_it_is_expanded) {
    Server server = make_server();
    const std::string text = "#define LIMIT 10\n"
                             "int under(int v) { return v < LIMIT ? v : LIMIT; }\n";
    open(server, "file:///work/main.cpp", text);
    CPPL_CHECK_EQ(references(server, "file:///work/main.cpp", position_of(text, "LIMIT", 1)),
                  at("main.cpp", text, "LIMIT", 0) + " " + at("main.cpp", text, "LIMIT", 1) + " " +
                      at("main.cpp", text, "LIMIT", 2));
}

CPPL_TEST(a_parameter_the_projection_repeats_is_one_name) {
    // The proof's parameter is repeated in the declaration for its claim and in
    // its assumption's probe; Clang sees two parameters, the author wrote one.
    Server server = make_server();
    const std::string text = "pure int identity(int x) {\n"
                             "    return x;\n"
                             "}\n"
                             "law guarded(int value)\n"
                             "    expects (identity(value) == 0)\n"
                             "    proves (identity(value) == value);\n"
                             "proof guarded_holds(int given)\n"
                             "    proves (guarded(given))\n"
                             "{\n"
                             "    assume h : identity(given) == 0;\n"
                             "    refl;\n"
                             "}\n";
    open(server, "file:///work/main.cpp", text);
    CPPL_CHECK_EQ(references(server, "file:///work/main.cpp", position_of(text, "given")),
                  at("main.cpp", text, "given", 0) + " " + at("main.cpp", text, "given", 1) + " " +
                      at("main.cpp", text, "given", 2));
    CPPL_CHECK_EQ(references(server, "file:///work/main.cpp", position_of(text, "value", 2)),
                  at("main.cpp", text, "value", 0) + " " + at("main.cpp", text, "value", 1) + " " +
                      at("main.cpp", text, "value", 2) + " " + at("main.cpp", text, "value", 3));
    // The Law is referenced by the proof that names it.
    CPPL_CHECK_EQ(references(server, "file:///work/main.cpp", position_of(text, "guarded(int")),
                  at("main.cpp", text, "guarded(int") + " " + at("main.cpp", text, "guarded(given"));
    // A pure function named in a Law is referenced there too.
    CPPL_CHECK_EQ(references(server, "file:///work/main.cpp", position_of(text, "identity", 0), false),
                  at("main.cpp", text, "identity", 1) + " " + at("main.cpp", text, "identity", 2) + " " +
                      at("main.cpp", text, "identity", 3));
}

CPPL_TEST(a_refinement_type_is_referenced_where_it_is_used) {
    Server server = make_server();
    const std::string text = "type NonNegative = int where (self >= 0);\n"
                             "type Small = NonNegative where (self < 10);\n"
                             "int keep(NonNegative n) { return n; }\n";
    open(server, "file:///work/main.cpp", text);
    CPPL_CHECK_EQ(references(server, "file:///work/main.cpp", position_of(text, "NonNegative", 2)),
                  at("main.cpp", text, "NonNegative", 0) + " " + at("main.cpp", text, "NonNegative", 1) + " " +
                      at("main.cpp", text, "NonNegative", 2));
}

CPPL_TEST(nothing_is_referenced_where_nothing_written_is_named) {
    Server server = make_server();
    const std::string text = "verified int keep(int amount)\n"
                             "    ensures (result == amount)\n"
                             "{\n"
                             "    return amount;\n"
                             "}\n";
    open(server, "file:///work/main.cpp", text);
    // `result` is declared by nothing anyone wrote: its one written use is all
    // there is to show.
    CPPL_CHECK_EQ(references(server, "file:///work/main.cpp", position_of(text, "result")),
                  at("main.cpp", text, "result"));
    CPPL_CHECK(references(server, "file:///work/main.cpp", Position{2, 0}).empty());
    CPPL_CHECK(highlights(server, "file:///work/main.cpp", position_of(text, "ensures")).empty());
    // The parameter named in the contract is the parameter.
    CPPL_CHECK_EQ(references(server, "file:///work/main.cpp", position_of(text, "amount", 2)),
                  at("main.cpp", text, "amount", 0) + " " + at("main.cpp", text, "amount", 1) + " " +
                      at("main.cpp", text, "amount", 2));
}

CPPL_TEST(references_in_an_unknown_document_are_null) {
    Server server = make_server();
    CPPL_CHECK_EQ(references(server, "file:///never-opened.cpp", Position{0, 0}), std::string("null"));
    CPPL_CHECK_EQ(highlights(server, "file:///never-opened.cpp", Position{0, 0}), std::string("null"));
}
