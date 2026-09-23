// The document outline: every C++ declaration Clang finds whose name the
// document writes, nested as declared, and every Law, proof and refinement type
// among them, never a declaration the projection generated
// (src/lsp/include/cppl/lsp/symbols.hpp).

#include "cppl/lsp/protocol.hpp"
#include "cppl/lsp/server.hpp"
#include "cppl/testing/test.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

using namespace cppl::lsp;

namespace {

const std::string kUri = "file:///work/outline.cpp";

std::vector<DocumentSymbol> outline_of(const std::string& text) {
    Server server(CPPL_TEST_DEFAULT_CLANG, {"-std=c++20"});
    TextDocumentItem item;
    item.uri = kUri;
    item.text = text;
    item.version = 1;
    server.text_document_did_open(item);
    TextDocumentIdentifier id;
    id.uri = kUri;
    const std::optional<std::vector<DocumentSymbol>> symbols = server.text_document_document_symbol(id);
    if (!symbols.has_value()) {
        ::cppl::testing::fail(__FILE__, __LINE__, "the document is unknown");
    }
    return *symbols;
}

const DocumentSymbol* named(const std::vector<DocumentSymbol>& symbols, const std::string& name) {
    for (const DocumentSymbol& symbol : symbols) {
        if (symbol.name == name) {
            return &symbol;
        }
    }
    return nullptr;
}

// Every name in the outline, depth first, each nested one as `outer/inner`.
void names(const std::vector<DocumentSymbol>& symbols, const std::string& outer, std::string& into) {
    for (const DocumentSymbol& symbol : symbols) {
        const std::string path = outer.empty() ? symbol.name : outer + "/" + symbol.name;
        into += path + "\n";
        names(symbol.children, path, into);
    }
}

std::string names(const std::vector<DocumentSymbol>& symbols) {
    std::string all;
    names(symbols, std::string(), all);
    return all;
}

bool is_kind(const DocumentSymbol* symbol, SymbolKind kind) {
    return symbol != nullptr && symbol->kind == kind;
}

bool has_detail(const DocumentSymbol* symbol, const std::string& detail) {
    return symbol != nullptr && symbol->detail == detail;
}

bool before(const Position& lhs, const Position& rhs) {
    return lhs.line < rhs.line || (lhs.line == rhs.line && lhs.character < rhs.character);
}

// LSP requires each name to lie inside its declaration, and each declaration
// inside the one it is nested in.
bool well_nested(const std::vector<DocumentSymbol>& symbols, const std::optional<Range>& outer) {
    for (const DocumentSymbol& symbol : symbols) {
        if (before(symbol.selection.start, symbol.range.start) || before(symbol.range.end, symbol.selection.end)) {
            return false;
        }
        if (outer.has_value() && (before(symbol.range.start, outer->start) || before(outer->end, symbol.range.end))) {
            return false;
        }
        if (!well_nested(symbol.children, symbol.range)) {
            return false;
        }
    }
    return true;
}

const std::string kProgram = "#define LIMIT 10\n"                                   // 0
                             "namespace geometry {\n"                               // 1
                             "struct Point {\n"                                     // 2
                             "    int x;\n"                                         // 3
                             "    int y;\n"                                         // 4
                             "    int sum() const;\n"                               // 5
                             "};\n"                                                 // 6
                             "enum class Axis { horizontal, vertical };\n"          // 7
                             "using Coordinate = int;\n"                            // 8
                             "law sum_commutes(int a, int b)\n"                     // 9
                             "    proves (a + b == b + a);\n"                       // 10
                             "}\n"                                                  // 11
                             "int geometry::Point::sum() const { return x + y; }\n" // 12
                             "namespace {\n"                                        // 13
                             "int hidden() { return 0; }\n"                         // 14
                             "}\n"                                                  // 15
                             "extern \"C\" int exported(int value);\n"              // 16
                             "pure int identity(int x) { return x; }\n"             // 17
                             "trusted law identity_is_input(int x)\n"               // 18
                             "    proves (identity(x) == x);\n"                     // 19
                             "proof identity_holds(int x)\n"                        // 20
                             "    proves (identity_is_input(x))\n"                  // 21
                             "{\n"                                                  // 22
                             "    exact identity_is_input(x);\n"                    // 23
                             "}\n"                                                  // 24
                             "type NonNegative = int where (self >= 0);\n"          // 25
                             "verified int keep(int amount)\n"                      // 26
                             "    expects (amount >= 0)\n"                          // 27
                             "    ensures (result == amount)\n"                     // 28
                             "{\n"                                                  // 29
                             "    return amount;\n"                                 // 30
                             "}\n";                                                 // 31

} // namespace

CPPL_TEST(every_declaration_is_outlined_in_order_and_nested_as_declared) {
    const std::vector<DocumentSymbol> outline = outline_of(kProgram);
    CPPL_CHECK_EQ(names(outline), std::string("LIMIT\n"
                                              "geometry\n"
                                              "geometry/Point\n"
                                              "geometry/Point/x\n"
                                              "geometry/Point/y\n"
                                              "geometry/Point/sum\n"
                                              "geometry/Axis\n"
                                              "geometry/Axis/horizontal\n"
                                              "geometry/Axis/vertical\n"
                                              "geometry/Coordinate\n"
                                              "geometry/sum_commutes\n"
                                              "Point::sum\n"
                                              "(anonymous)\n"
                                              "(anonymous)/hidden\n"
                                              "exported\n"
                                              "identity\n"
                                              "identity_is_input\n"
                                              "identity_holds\n"
                                              "NonNegative\n"
                                              "keep\n"));
    CPPL_CHECK(well_nested(outline, std::nullopt));
}

CPPL_TEST(each_declaration_has_its_kind_and_what_it_is) {
    const std::vector<DocumentSymbol> outline = outline_of(kProgram);
    const DocumentSymbol* geometry = named(outline, "geometry");
    CPPL_CHECK(geometry != nullptr);
    if (geometry == nullptr) {
        return;
    }
    CPPL_CHECK(geometry->kind == SymbolKind::Namespace);
    const DocumentSymbol* point = named(geometry->children, "Point");
    CPPL_CHECK(is_kind(point, SymbolKind::Struct));
    if (point != nullptr) {
        const DocumentSymbol* x = named(point->children, "x");
        CPPL_CHECK(is_kind(x, SymbolKind::Field));
        CPPL_CHECK(has_detail(x, "int"));
        CPPL_CHECK(is_kind(named(point->children, "sum"), SymbolKind::Method));
    }
    const DocumentSymbol* axis = named(geometry->children, "Axis");
    CPPL_CHECK(is_kind(axis, SymbolKind::Enum));
    if (axis != nullptr) {
        CPPL_CHECK(is_kind(named(axis->children, "vertical"), SymbolKind::EnumMember));
    }
    CPPL_CHECK(has_detail(named(geometry->children, "Coordinate"), "int"));

    const DocumentSymbol* law = named(geometry->children, "sum_commutes");
    CPPL_CHECK(law != nullptr);
    if (law != nullptr) {
        CPPL_CHECK(law->kind == SymbolKind::Interface);
        CPPL_CHECK_EQ(law->detail, std::string("law"));
        // The whole declaration, and its name as written.
        CPPL_CHECK_EQ(law->range.start.line, 9u);
        CPPL_CHECK_EQ(law->range.start.character, 0u);
        CPPL_CHECK_EQ(law->range.end.line, 10u);
        CPPL_CHECK_EQ(law->selection.start.line, 9u);
        CPPL_CHECK_EQ(law->selection.start.character, 4u);
        CPPL_CHECK_EQ(law->selection.end.character, 16u);
    }

    CPPL_CHECK(is_kind(named(outline, "LIMIT"), SymbolKind::Constant));
    CPPL_CHECK(has_detail(named(outline, "identity"), "pure int (int)"));
    CPPL_CHECK(has_detail(named(outline, "identity_is_input"), "trusted law"));
    const DocumentSymbol* proof = named(outline, "identity_holds");
    CPPL_CHECK(proof != nullptr);
    if (proof != nullptr) {
        CPPL_CHECK(proof->kind == SymbolKind::Function);
        CPPL_CHECK_EQ(proof->detail, std::string("proves (identity_is_input(x))"));
        CPPL_CHECK_EQ(proof->range.start.line, 20u);
        CPPL_CHECK_EQ(proof->range.end.line, 24u);
    }
    const DocumentSymbol* refinement = named(outline, "NonNegative");
    CPPL_CHECK(refinement != nullptr);
    if (refinement != nullptr) {
        CPPL_CHECK(refinement->kind == SymbolKind::Class);
        CPPL_CHECK_EQ(refinement->detail, std::string("refinement of int"));
        CPPL_CHECK_EQ(refinement->selection.start.character, 5u);
    }
    const DocumentSymbol* keep = named(outline, "keep");
    CPPL_CHECK(keep != nullptr);
    if (keep != nullptr) {
        CPPL_CHECK(keep->kind == SymbolKind::Function);
        CPPL_CHECK_EQ(keep->detail, std::string("verified int (int)"));
        CPPL_CHECK_EQ(keep->selection.start.line, 26u);
        CPPL_CHECK_EQ(keep->selection.start.character, 13u);
        CPPL_CHECK_EQ(keep->range.end.line, 31u);
    }
}

CPPL_TEST(nothing_the_projection_generated_is_outlined) {
    const std::string all = names(outline_of(kProgram));
    CPPL_CHECK(all.find("__cppl") == std::string::npos);
    CPPL_CHECK(all.find("self") == std::string::npos);
    // The refinement type is outlined once, as written, not also as the alias
    // the projection declares for it.
    CPPL_CHECK_EQ(all.find("NonNegative"), all.rfind("NonNegative"));
    // A proof is outlined once, not also as the function that stands for it.
    CPPL_CHECK_EQ(all.find("identity_holds"), all.rfind("identity_holds"));
}

CPPL_TEST(a_file_with_no_cppl_is_outlined_by_clang_alone) {
    const std::vector<DocumentSymbol> outline = outline_of("#include <vector>\n"
                                                           "template <typename T>\n"
                                                           "struct Box { T value; };\n"
                                                           "template <typename T>\n"
                                                           "concept Boxed = requires(T t) { t.value; };\n"
                                                           "int main() { int local = 0; return local; }\n");
    // Nothing from the header, and no local of a function body.
    CPPL_CHECK_EQ(names(outline), std::string("Box\nBox/value\nBoxed\nmain\n"));
    CPPL_CHECK(is_kind(named(outline, "Boxed"), SymbolKind::Interface));
}

CPPL_TEST(a_cppl_declaration_the_unit_cannot_parse_is_still_outlined) {
    // A proof that names nothing the unit declares is still written there.
    const std::vector<DocumentSymbol> outline = outline_of("proof orphan(int x)\n"
                                                           "    proves (missing(x))\n"
                                                           "{\n"
                                                           "    refl;\n"
                                                           "}\n");
    CPPL_CHECK_EQ(names(outline), std::string("orphan\n"));
}

CPPL_TEST(an_outline_of_an_unknown_document_is_null) {
    Server server(CPPL_TEST_DEFAULT_CLANG, {"-std=c++20"});
    TextDocumentIdentifier id;
    id.uri = "file:///never-opened.cpp";
    CPPL_CHECK(!server.text_document_document_symbol(id).has_value());
}
