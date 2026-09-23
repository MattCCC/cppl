// Navigation: definition, declaration, type definition and implementation,
// answered by Clang over each document's projection and traced back to the text
// as written (src/lsp/include/cppl/lsp/editor_view.hpp).

#include "cppl/clang/editor.hpp"
#include "cppl/lsp/position.hpp"
#include "cppl/lsp/protocol.hpp"
#include "cppl/lsp/server.hpp"
#include "cppl/testing/test.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using namespace cppl;
using namespace cppl::lsp;
using clangbridge::Destination;

namespace {

const std::string kInclude = std::string(CPPL_TEST_FIXTURES_DIR) + "/include";

Server make_server() {
    return Server(CPPL_TEST_DEFAULT_CLANG, {"-std=c++20", "-I" + kInclude});
}

void open(Server& server, const std::string& uri, const std::string& text) {
    TextDocumentItem item;
    item.uri = uri;
    item.text = text;
    item.version = 1;
    server.text_document_did_open(item);
}

// Where the `nth` occurrence of `needle` starts in `text`, moved `into` bytes
// further.
Position position_of(const std::string& text, const std::string& needle, std::size_t nth = 0, std::size_t into = 0) {
    std::size_t at = text.find(needle);
    for (std::size_t skipped = 0; skipped < nth && at != std::string::npos; ++skipped) {
        at = text.find(needle, at + 1);
    }
    if (at == std::string::npos) {
        ::cppl::testing::fail(__FILE__, __LINE__, "no occurrence " + std::to_string(nth) + " of '" + needle + "'");
    }
    return PositionMapper(text).byte_offset_to_position(at + into);
}

std::string describe(const Range& range) {
    return std::to_string(range.start.line) + ":" + std::to_string(range.start.character) + "-" +
           std::to_string(range.end.line) + ":" + std::to_string(range.end.character);
}

// Each location as `file:start-end`, by the file's last path component.
std::string describe(const std::optional<std::vector<Location>>& locations) {
    if (!locations.has_value()) {
        return "null";
    }
    std::string out;
    for (const Location& location : *locations) {
        const std::size_t slash = location.uri.rfind('/');
        out += (out.empty() ? "" : " ") + location.uri.substr(slash + 1) + ":" + describe(location.range);
    }
    return out;
}

std::string go(Server& server, Destination destination, const std::string& uri, const Position& position) {
    TextDocumentIdentifier id;
    id.uri = uri;
    return describe(server.text_document_navigate(destination, id, position));
}

// The range the `nth` occurrence of `needle` spans in `file`, or its first
// `length` bytes when that is given.
std::string spans(const std::string& file, const std::string& text, const std::string& needle, std::size_t nth = 0,
                  std::size_t length = 0) {
    const Position start = position_of(text, needle, nth);
    const Position end{start.line, start.character + static_cast<std::uint32_t>(length != 0 ? length : needle.size())};
    return file + ":" + describe(Range{start, end});
}

} // namespace

CPPL_TEST(a_local_and_a_function_lead_to_where_they_are_defined) {
    Server server = make_server();
    const std::string text = "int twice(int value) {\n"
                             "    int doubled = value * 2;\n"
                             "    return doubled;\n"
                             "}\n"
                             "int main() { return twice(21); }\n";
    open(server, "file:///work/main.cpp", text);
    CPPL_CHECK_EQ(go(server, Destination::Definition, "file:///work/main.cpp", position_of(text, "doubled", 1)),
                  spans("main.cpp", text, "doubled"));
    CPPL_CHECK_EQ(go(server, Destination::Definition, "file:///work/main.cpp", position_of(text, "value", 1)),
                  spans("main.cpp", text, "value"));
    CPPL_CHECK_EQ(go(server, Destination::Definition, "file:///work/main.cpp", position_of(text, "twice(21)")),
                  spans("main.cpp", text, "twice"));
}

CPPL_TEST(a_position_just_past_a_name_names_it) {
    Server server = make_server();
    const std::string text = "int answer = 42;\n"
                             "int main() { return answer; }\n";
    open(server, "file:///work/main.cpp", text);
    CPPL_CHECK_EQ(go(server, Destination::Definition, "file:///work/main.cpp", position_of(text, "answer;", 0, 6)),
                  spans("main.cpp", text, "answer"));
}

CPPL_TEST(at_a_definition_the_answer_is_its_earlier_declaration) {
    Server server = make_server();
    const std::string text = "int later(int);\n"
                             "int main() { return later(1); }\n"
                             "int later(int x) { return x; }\n";
    open(server, "file:///work/main.cpp", text);
    // From a use: the definition. From the definition: the declaration.
    CPPL_CHECK_EQ(go(server, Destination::Definition, "file:///work/main.cpp", position_of(text, "later", 1)),
                  spans("main.cpp", text, "later", 2));
    CPPL_CHECK_EQ(go(server, Destination::Definition, "file:///work/main.cpp", position_of(text, "later", 2)),
                  spans("main.cpp", text, "later", 0));
    // A declaration request always answers the first declaration.
    CPPL_CHECK_EQ(go(server, Destination::Declaration, "file:///work/main.cpp", position_of(text, "later", 1)),
                  spans("main.cpp", text, "later", 0));
}

CPPL_TEST(a_standard_library_name_leads_into_its_system_header) {
    Server server = make_server();
    const std::string text = "#include <vector>\n"
                             "int main() { std::vector<int> v; v.push_back(1); return static_cast<int>(v.size()); }\n";
    open(server, "file:///work/main.cpp", text);
    // Where the standard library defines it is the library's business; that it
    // is found there, and not in the document, is the point.
    const std::string vector =
        go(server, Destination::Definition, "file:///work/main.cpp", position_of(text, "vector<"));
    CPPL_CHECK(vector.starts_with("vector"));
    CPPL_CHECK(vector.find("main.cpp") == std::string::npos);
    const std::string push_back =
        go(server, Destination::Definition, "file:///work/main.cpp", position_of(text, "push_back"));
    CPPL_CHECK(!push_back.empty());
    CPPL_CHECK(push_back.find("main.cpp") == std::string::npos);
}

CPPL_TEST(an_include_leads_to_the_start_of_the_file_it_includes) {
    Server server = make_server();
    const std::string text = "#include <payment.hpp>\n"
                             "int main() { return scale(1); }\n";
    open(server, "file:///work/main.cpp", text);
    CPPL_CHECK_EQ(go(server, Destination::Definition, "file:///work/main.cpp", position_of(text, "payment")),
                  std::string("payment.hpp:0:0-0:0"));
}

CPPL_TEST(a_name_in_a_header_holding_cppl_is_found_through_its_projection) {
    // `payment.hpp` holds a Law and a `pure` function, so Clang reads its
    // projection; the name it reports is traced back to the header as written.
    Server server = make_server();
    const std::string text = "#include <payment.hpp>\n"
                             "int main() { return scale(1); }\n";
    open(server, "file:///work/main.cpp", text);
    CPPL_CHECK_EQ(go(server, Destination::Definition, "file:///work/main.cpp", position_of(text, "scale")),
                  std::string("payment.hpp:5:9-5:14"));
}

CPPL_TEST(a_name_inside_a_law_leads_to_the_cpp_it_names) {
    Server server = make_server();
    const std::string text = "pure int identity(int x) {\n"
                             "    return x;\n"
                             "}\n"
                             "law identity_returns_input(int x)\n"
                             "    proves (identity(x) == x);\n";
    open(server, "file:///work/main.cpp", text);
    CPPL_CHECK_EQ(go(server, Destination::Definition, "file:///work/main.cpp", position_of(text, "identity(x)")),
                  spans("main.cpp", text, "identity"));
    // The Law's own parameter: Clang finds it in the declaration standing for
    // the Law, which copied the parameters the author wrote.
    CPPL_CHECK_EQ(go(server, Destination::Definition, "file:///work/main.cpp", position_of(text, "x) ==")),
                  spans("main.cpp", text, "x)", 1, 1));
    CPPL_CHECK_EQ(go(server, Destination::Definition, "file:///work/main.cpp", position_of(text, "== x", 0, 3)),
                  spans("main.cpp", text, "x)", 1, 1));
}

CPPL_TEST(every_parameter_repeated_in_a_generated_declaration_leads_to_the_one_written) {
    // A premise, a proof's own claim and an assumption are each projected as a
    // function repeating the parameters; each use leads to the parameter the
    // author wrote, never to a repetition of it.
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
    const std::string law_parameter = spans("main.cpp", text, "value");
    CPPL_CHECK_EQ(go(server, Destination::Definition, "file:///work/main.cpp", position_of(text, "value", 1)),
                  law_parameter);
    CPPL_CHECK_EQ(go(server, Destination::Definition, "file:///work/main.cpp", position_of(text, "value", 3)),
                  law_parameter);
    const std::string proof_parameter = spans("main.cpp", text, "given");
    CPPL_CHECK_EQ(go(server, Destination::Definition, "file:///work/main.cpp", position_of(text, "given", 1)),
                  proof_parameter);
    CPPL_CHECK_EQ(go(server, Destination::Definition, "file:///work/main.cpp", position_of(text, "given", 2)),
                  proof_parameter);
    // The Law itself, named by the proof, is the Law's name.
    CPPL_CHECK_EQ(go(server, Destination::Definition, "file:///work/main.cpp", position_of(text, "guarded(given")),
                  spans("main.cpp", text, "guarded"));
    // The name `h` an assumption binds is not C++.
    CPPL_CHECK(go(server, Destination::Definition, "file:///work/main.cpp", position_of(text, "h :")).empty());
}

CPPL_TEST(a_name_in_a_contract_leads_to_the_parameter_it_names) {
    Server server = make_server();
    const std::string text = "verified int keep(int amount)\n"
                             "    expects (amount >= 0)\n"
                             "    ensures (result == amount)\n"
                             "{\n"
                             "    return amount;\n"
                             "}\n";
    open(server, "file:///work/main.cpp", text);
    const std::string parameter = spans("main.cpp", text, "amount");
    CPPL_CHECK_EQ(go(server, Destination::Definition, "file:///work/main.cpp", position_of(text, "amount", 1)),
                  parameter);
    CPPL_CHECK_EQ(go(server, Destination::Definition, "file:///work/main.cpp", position_of(text, "amount", 2)),
                  parameter);
    CPPL_CHECK_EQ(go(server, Destination::Definition, "file:///work/main.cpp", position_of(text, "amount", 3)),
                  parameter);
    // `result` is no declaration anyone wrote.
    CPPL_CHECK(go(server, Destination::Definition, "file:///work/main.cpp", position_of(text, "result")).empty());
}

CPPL_TEST(a_law_named_by_a_proof_leads_to_the_law) {
    Server server = make_server();
    const std::string text = "pure int identity(int x) {\n"
                             "    return x;\n"
                             "}\n"
                             "law identity_returns_input(int x)\n"
                             "    proves (identity(x) == x);\n"
                             "proof identity_returns_input_holds(int x)\n"
                             "    proves (identity_returns_input(x))\n"
                             "{\n"
                             "    refl;\n"
                             "}\n";
    open(server, "file:///work/main.cpp", text);
    CPPL_CHECK_EQ(
        go(server, Destination::Definition, "file:///work/main.cpp", position_of(text, "identity_returns_input(x)")),
        spans("main.cpp", text, "identity_returns_input"));
    // A proof statement is not C++, and Clang has nothing to say about it.
    CPPL_CHECK(go(server, Destination::Definition, "file:///work/main.cpp", position_of(text, "refl")).empty());
}

CPPL_TEST(a_refinement_type_leads_to_its_declaration) {
    Server server = make_server();
    const std::string text = "type NonNegative = int where (self >= 0);\n"
                             "int use(NonNegative n) { return n; }\n";
    open(server, "file:///work/main.cpp", text);
    CPPL_CHECK_EQ(go(server, Destination::Definition, "file:///work/main.cpp", position_of(text, "NonNegative", 1)),
                  spans("main.cpp", text, "NonNegative"));
    // Its type definition is the refinement too, which is what the parameter's
    // type is written as.
    CPPL_CHECK_EQ(go(server, Destination::TypeDefinition, "file:///work/main.cpp", position_of(text, "n)")),
                  spans("main.cpp", text, "NonNegative"));
}

CPPL_TEST(a_type_definition_leads_through_pointers_and_references) {
    Server server = make_server();
    const std::string text = "struct Point { int x; };\n"
                             "int read(const Point* point, Point& other) { return point->x + other.x; }\n";
    open(server, "file:///work/main.cpp", text);
    CPPL_CHECK_EQ(go(server, Destination::TypeDefinition, "file:///work/main.cpp", position_of(text, "point->")),
                  spans("main.cpp", text, "Point"));
    CPPL_CHECK_EQ(go(server, Destination::TypeDefinition, "file:///work/main.cpp", position_of(text, "other.")),
                  spans("main.cpp", text, "Point"));
}

CPPL_TEST(auto_leads_to_the_type_it_was_deduced_as) {
    Server server = make_server();
    const std::string text = "struct Widget {};\n"
                             "Widget make() { return {}; }\n"
                             "int main() { auto made = make(); (void)made; }\n";
    open(server, "file:///work/main.cpp", text);
    CPPL_CHECK_EQ(go(server, Destination::Definition, "file:///work/main.cpp", position_of(text, "auto")),
                  spans("main.cpp", text, "Widget"));
}

CPPL_TEST(an_overloaded_operator_leads_to_its_declaration) {
    Server server = make_server();
    const std::string text = "struct Money { int cents; };\n"
                             "Money operator+(Money a, Money b) { return {a.cents + b.cents}; }\n"
                             "int main() { Money a{1}, b{2}; return (a + b).cents; }\n";
    open(server, "file:///work/main.cpp", text);
    const std::string found = go(server, Destination::Definition, "file:///work/main.cpp", position_of(text, "+ b)"));
    CPPL_CHECK_EQ(found, "main.cpp:1:6-1:15");
}

CPPL_TEST(an_implementation_is_every_override_and_every_derived_class) {
    Server server = make_server();
    const std::string text = "struct Shape { virtual int area() const = 0; virtual ~Shape() = default; };\n"
                             "struct Square : Shape { int area() const override { return 4; } };\n"
                             "struct Unit : Square { int area() const override { return 1; } };\n"
                             "int measure(const Shape& shape) { return shape.area(); }\n";
    open(server, "file:///work/main.cpp", text);
    CPPL_CHECK_EQ(go(server, Destination::Implementation, "file:///work/main.cpp", position_of(text, "area()", 3)),
                  spans("main.cpp", text, "area", 1) + " " + spans("main.cpp", text, "area", 2));
    CPPL_CHECK_EQ(go(server, Destination::Implementation, "file:///work/main.cpp", position_of(text, "Shape {")),
                  spans("main.cpp", text, "Square") + " " + spans("main.cpp", text, "Unit"));
    // A function that is not virtual has none.
    CPPL_CHECK(go(server, Destination::Implementation, "file:///work/main.cpp", position_of(text, "measure")).empty());
}

CPPL_TEST(nothing_is_named_by_space_a_keyword_a_comment_or_a_literal) {
    Server server = make_server();
    const std::string text = "int value = 1; // value\n"
                             "const char* note = \"value\";\n";
    open(server, "file:///work/main.cpp", text);
    CPPL_CHECK(go(server, Destination::Definition, "file:///work/main.cpp", position_of(text, "int")).empty());
    CPPL_CHECK(go(server, Destination::Definition, "file:///work/main.cpp", position_of(text, "value", 1)).empty());
    CPPL_CHECK(go(server, Destination::Definition, "file:///work/main.cpp", position_of(text, "value", 2)).empty());
    CPPL_CHECK(go(server, Destination::Definition, "file:///work/main.cpp", Position{5, 0}).empty());
}

CPPL_TEST(an_edit_is_answered_from_the_new_text_by_a_reparse) {
    Server server = make_server();
    const std::string before = "int first = 1;\n"
                               "int main() { return first; }\n";
    open(server, "file:///work/main.cpp", before);
    CPPL_CHECK_EQ(go(server, Destination::Definition, "file:///work/main.cpp", position_of(before, "first", 1)),
                  spans("main.cpp", before, "first"));

    const std::string after = "// moved\n"
                              "\n"
                              "int first = 1;\n"
                              "int main() { return first; }\n";
    VersionedTextDocumentIdentifier id;
    id.uri = "file:///work/main.cpp";
    id.version = 2;
    TextDocumentContentChangeEvent change;
    change.text = after;
    server.text_document_did_change(id, {change});
    CPPL_CHECK_EQ(go(server, Destination::Definition, "file:///work/main.cpp", position_of(after, "first", 1)),
                  spans("main.cpp", after, "first"));
}

CPPL_TEST(a_position_counts_utf16_code_units) {
    Server server = make_server();
    // U+1F600 is four UTF-8 bytes and two UTF-16 units, so `smile`, at byte 15,
    // is at character 13.
    const std::string text = "int /* \xF0\x9F\x98\x80 */ smile = 1;\n"
                             "int main() { return smile; }\n";
    open(server, "file:///work/main.cpp", text);
    CPPL_CHECK_EQ(go(server, Destination::Definition, "file:///work/main.cpp", position_of(text, "smile", 1)),
                  std::string("main.cpp:0:13-0:18"));
}

CPPL_TEST(an_open_buffer_is_read_as_the_editor_holds_it) {
    // The header on disk declares `scale` on line 6; the open buffer moves it.
    Server server = make_server();
    const std::string header = "#pragma once\n"
                               "\n"
                               "\n"
                               "inline int scale(int amount) { return amount; }\n";
    open(server, "file://" + kInclude + "/payment.hpp", header);
    const std::string text = "#include <payment.hpp>\n"
                             "int main() { return scale(1); }\n";
    open(server, "file:///work/main.cpp", text);
    CPPL_CHECK_EQ(go(server, Destination::Definition, "file:///work/main.cpp", position_of(text, "scale")),
                  std::string("payment.hpp:3:11-3:16"));
}

CPPL_TEST(a_program_with_errors_still_navigates) {
    Server server = make_server();
    const std::string text = "int helper() { return 1; }\n"
                             "int main() { undeclared(); return helper(); }\n";
    open(server, "file:///work/main.cpp", text);
    CPPL_CHECK_EQ(go(server, Destination::Definition, "file:///work/main.cpp", position_of(text, "helper", 1)),
                  spans("main.cpp", text, "helper"));
}

CPPL_TEST(navigation_in_an_unknown_document_is_null) {
    Server server = make_server();
    CPPL_CHECK_EQ(go(server, Destination::Definition, "file:///never-opened.cpp", Position{0, 0}), std::string("null"));
}
