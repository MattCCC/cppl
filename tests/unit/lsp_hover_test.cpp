// Hover: Clang's description of a C++ name, and a C++L declaration shown as it
// is written (src/lsp/include/cppl/lsp/hover.hpp).

#include "cppl/lsp/position.hpp"
#include "cppl/lsp/protocol.hpp"
#include "cppl/lsp/server.hpp"
#include "cppl/testing/test.hpp"

#include <cstddef>
#include <fstream>
#include <ios>
#include <optional>
#include <sstream>
#include <string>

using namespace cppl::lsp;

namespace {

const std::string kFixtures = CPPL_TEST_FIXTURES_DIR;

std::string read_fixture(const std::string& name) {
    std::ifstream stream(kFixtures + "/" + name, std::ios::binary);
    if (!stream) {
        ::cppl::testing::fail(__FILE__, __LINE__, "could not read fixture '" + name + "'");
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

Server make_server() {
    return Server(CPPL_TEST_DEFAULT_CLANG, {"-std=c++20", "-I" + kFixtures});
}

void open(Server& server, const std::string& uri, const std::string& text) {
    TextDocumentItem item;
    item.uri = uri;
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

std::optional<Hover> hover(Server& server, const std::string& uri, const Position& position) {
    TextDocumentIdentifier id;
    id.uri = uri;
    return server.text_document_hover(id, position);
}

// The hover's markdown, or "none".
std::string shown(Server& server, const std::string& uri, const Position& position) {
    const std::optional<Hover> found = hover(server, uri, position);
    return found.has_value() ? found->contents : std::string("none");
}

bool has(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

} // namespace

CPPL_TEST(a_function_shows_its_declaration_and_its_comment) {
    Server server = make_server();
    const std::string text = "/// Doubles a value.\n"
                             "int twice(int value) { return value * 2; }\n"
                             "int main() { return twice(21); }\n";
    open(server, "file:///work/main.cpp", text);
    const std::string markdown = shown(server, "file:///work/main.cpp", position_of(text, "twice(21)"));
    CPPL_CHECK(has(markdown, "**function** `twice`"));
    CPPL_CHECK(has(markdown, "int twice(int value)"));
    CPPL_CHECK(has(markdown, "Doubles a value."));
    CPPL_CHECK(!has(markdown, "return value"));
    // The range is the name hovered.
    const std::optional<Hover> found = hover(server, "file:///work/main.cpp", position_of(text, "twice(21)", 2));
    CPPL_CHECK(found.has_value() && found->range.has_value());
    CPPL_CHECK_EQ(found->range->start.character, 20u);
    CPPL_CHECK_EQ(found->range->end.character, 25u);
}

CPPL_TEST(a_constant_an_enumerator_and_a_type_show_what_clang_computed) {
    Server server = make_server();
    const std::string text = "namespace geo {\n"
                             "constexpr int limit = 6 * 7;\n"
                             "enum class Color { red = 3, green };\n"
                             "struct Point { int x; int y; };\n"
                             "}\n"
                             "int main() {\n"
                             "    geo::Point p{};\n"
                             "    return geo::limit + static_cast<int>(geo::Color::green) + p.x;\n"
                             "}\n";
    open(server, "file:///work/main.cpp", text);
    const std::string limit = shown(server, "file:///work/main.cpp", position_of(text, "limit +"));
    CPPL_CHECK(has(limit, "**variable** `geo::limit`"));
    CPPL_CHECK(has(limit, "Value: `42`"));
    const std::string green = shown(server, "file:///work/main.cpp", position_of(text, "green)"));
    CPPL_CHECK(has(green, "**enumerator** `geo::Color::green`"));
    CPPL_CHECK(has(green, "Value: `4`"));
    const std::string point = shown(server, "file:///work/main.cpp", position_of(text, "Point p"));
    CPPL_CHECK(has(point, "**struct** `geo::Point`"));
    CPPL_CHECK(has(point, "Size: 8 bytes, alignment 4"));
    const std::string field = shown(server, "file:///work/main.cpp", position_of(text, "x;\n}"));
    CPPL_CHECK(has(field, "**field** `geo::Point::x`"));
}

CPPL_TEST(a_macro_shows_its_definition) {
    Server server = make_server();
    const std::string text = "#define LIMIT (10 + 1)\n"
                             "int main() { return LIMIT; }\n";
    open(server, "file:///work/main.cpp", text);
    const std::string markdown = shown(server, "file:///work/main.cpp", position_of(text, "LIMIT;"));
    CPPL_CHECK(has(markdown, "**macro** `LIMIT`"));
    CPPL_CHECK(has(markdown, "#define LIMIT (10 + 1)"));
}

CPPL_TEST(a_name_from_a_header_says_where_it_is_declared) {
    Server server = make_server();
    const std::string text = "#include <vector>\n"
                             "int main() { std::vector<int> v; return static_cast<int>(v.size()); }\n";
    open(server, "file:///work/main.cpp", text);
    const std::string markdown = shown(server, "file:///work/main.cpp", position_of(text, "size()"));
    CPPL_CHECK(has(markdown, "size"));
    CPPL_CHECK(has(markdown, "Declared in `"));
    const std::string deduced = shown(server, "file:///work/main.cpp", position_of(text, "vector<int> v"));
    CPPL_CHECK(has(deduced, "std::"));
}

CPPL_TEST(auto_shows_the_type_it_was_deduced_as) {
    Server server = make_server();
    const std::string text = "struct Widget { int size; };\n"
                             "Widget make() { return {}; }\n"
                             "int main() { auto made = make(); return made.size; }\n";
    open(server, "file:///work/main.cpp", text);
    const std::string markdown = shown(server, "file:///work/main.cpp", position_of(text, "auto"));
    CPPL_CHECK(has(markdown, "**struct** `Widget`"));
    CPPL_CHECK(has(markdown, "Type: `Widget`") || has(markdown, "struct Widget"));
}

CPPL_TEST(a_law_and_a_proof_are_shown_as_written) {
    Server server = make_server();
    const std::string text = read_fixture("written_proof.cpp");
    open(server, "file:///written_proof.cpp", text);
    // A Law named in a proof's claim, which Clang resolves to what stands for it.
    const std::string law =
        shown(server, "file:///written_proof.cpp", position_of(text, "proves (identity_returns_input(x))", 8));
    CPPL_CHECK(has(law, "**law** `identity_returns_input`"));
    CPPL_CHECK(has(law, "proves (identity(x) == x);"));
    CPPL_CHECK(!has(law, "static"));
    CPPL_CHECK(!has(law, "__cppl"));
    // A proof a statement names, without its body.
    const std::string proof =
        shown(server, "file:///written_proof.cpp", position_of(text, "apply identity_returns_input_holds", 6));
    CPPL_CHECK(has(proof, "**proof** `identity_returns_input_holds`"));
    CPPL_CHECK(has(proof, "proves (identity_returns_input(x))"));
    CPPL_CHECK(!has(proof, "refl"));
}

CPPL_TEST(a_trusted_law_says_it_is_an_assumption) {
    Server server = make_server();
    const std::string text = read_fixture("trust_closure.cpp");
    open(server, "file:///trust_closure.cpp", text);
    const std::string markdown =
        shown(server, "file:///trust_closure.cpp", position_of(text, "exact sensor_identity(x)", 6));
    CPPL_CHECK(has(markdown, "**trusted law** `sensor_identity`"));
    CPPL_CHECK(has(markdown, "explicit assumption"));
}

CPPL_TEST(an_assumption_is_shown_with_the_premise_it_binds) {
    Server server = make_server();
    const std::string text = read_fixture("rewritten_proof.cpp");
    open(server, "file:///rewritten_proof.cpp", text);
    const std::string markdown = shown(server, "file:///rewritten_proof.cpp", position_of(text, "rewrite h", 8));
    CPPL_CHECK(has(markdown, "**assumption** `h`"));
    CPPL_CHECK(has(markdown, "assume h : x == 0u;"));
}

CPPL_TEST(a_refinement_type_says_what_it_refines_and_erases_to) {
    Server server = make_server();
    const std::string text = "type NonNegative = int where (self >= 0);\n"
                             "int keep(NonNegative n) { return n; }\n";
    open(server, "file:///work/main.cpp", text);
    const std::string markdown = shown(server, "file:///work/main.cpp", position_of(text, "NonNegative n"));
    CPPL_CHECK(has(markdown, "**refinement type** `NonNegative`"));
    CPPL_CHECK(has(markdown, "type NonNegative = int where (self >= 0);"));
    CPPL_CHECK(has(markdown, "It erases to `int`"));
    // `self` names the value the predicate is stated of.
    const std::string self = shown(server, "file:///work/main.cpp", position_of(text, "self"));
    CPPL_CHECK(has(self, "**self**"));
    CPPL_CHECK(has(self, "`int`"));
}

CPPL_TEST(a_verified_function_shows_its_contract_and_result_what_it_is) {
    Server server = make_server();
    const std::string text = "verified int keep(int amount)\n"
                             "    expects (amount >= 0)\n"
                             "    ensures (result == amount)\n"
                             "{\n"
                             "    return amount;\n"
                             "}\n"
                             "int main() { return keep(1); }\n";
    open(server, "file:///work/main.cpp", text);
    const std::string function = shown(server, "file:///work/main.cpp", position_of(text, "keep(1)"));
    CPPL_CHECK(has(function, "**verified function** `keep`"));
    CPPL_CHECK(has(function, "expects (amount >= 0)"));
    CPPL_CHECK(has(function, "ensures (result == amount)"));
    CPPL_CHECK(!has(function, "return amount"));
    const std::string result = shown(server, "file:///work/main.cpp", position_of(text, "result"));
    CPPL_CHECK(has(result, "**result**"));
    CPPL_CHECK(has(result, "The value the function returns"));
    CPPL_CHECK(!has(result, "__cppl"));
}

CPPL_TEST(nothing_is_shown_where_nothing_is_named) {
    Server server = make_server();
    const std::string text = "int value = 1; // a comment\n";
    open(server, "file:///work/main.cpp", text);
    CPPL_CHECK_EQ(shown(server, "file:///work/main.cpp", position_of(text, "a comment")), std::string("none"));
    CPPL_CHECK_EQ(shown(server, "file:///never-opened.cpp", Position{0, 0}), std::string("none"));
}
