// Completion: Clang's names and keywords for C++, and C++L's own words where the
// grammar admits them (src/lsp/include/cppl/lsp/completion.hpp).

#include "cppl/lsp/position.hpp"
#include "cppl/lsp/protocol.hpp"
#include "cppl/lsp/server.hpp"
#include "cppl/testing/test.hpp"

#include <algorithm>
#include <cstddef>
#include <string>

using namespace cppl::lsp;

namespace {

Server make_server(bool snippets = true) {
    Server server(CPPL_TEST_DEFAULT_CLANG, {"-std=c++20"});
    ClientCapabilities capabilities;
    capabilities.snippets = snippets;
    server.initialize(capabilities);
    return server;
}

// Opens `text` with `|` marking the cursor, and completes there.
CompletionList complete_at(Server& server, std::string text) {
    const std::size_t cursor = text.find('|');
    if (cursor == std::string::npos) {
        ::cppl::testing::fail(__FILE__, __LINE__, "the text marks no cursor");
    }
    text.erase(cursor, 1);
    TextDocumentItem item;
    item.uri = "file:///work/main.cpp";
    item.text = text;
    item.version = 1;
    server.text_document_did_open(item);
    TextDocumentIdentifier id;
    id.uri = item.uri;
    return server.text_document_completion(id, PositionMapper(text).byte_offset_to_position(cursor));
}

const CompletionItem* find(const CompletionList& list, const std::string& label) {
    const auto found =
        std::ranges::find_if(list.items, [&label](const CompletionItem& item) { return item.label == label; });
    return found == list.items.end() ? nullptr : &*found;
}

bool offers(const CompletionList& list, const std::string& label) {
    return find(list, label) != nullptr;
}

bool offers_starting(const CompletionList& list, const std::string& start) {
    return std::ranges::any_of(list.items,
                               [&start](const CompletionItem& item) { return item.label.starts_with(start); });
}

} // namespace

CPPL_TEST(a_member_access_offers_the_members) {
    Server server = make_server();
    const CompletionList list = complete_at(server, "struct Point { int x; int y; };\n"
                                                    "int read(Point p) { return p.|; }\n");
    const CompletionItem* x = find(list, "x");
    CPPL_CHECK(x != nullptr);
    if (x != nullptr) {
        CPPL_CHECK(x->kind == CompletionItemKind::Field);
        CPPL_CHECK_EQ(x->detail, std::string("int"));
    }
    CPPL_CHECK(offers(list, "y"));
    CPPL_CHECK(!offers(list, "read(Point p)"));
}

CPPL_TEST(what_has_been_typed_filters_and_ranks) {
    Server server = make_server();
    const CompletionList list = complete_at(server, "int counter = 0;\n"
                                                    "int count_down(int from);\n"
                                                    "int main() { return cou|; }\n");
    CPPL_CHECK(!list.items.empty());
    CPPL_CHECK(offers(list, "counter"));
    CPPL_CHECK(offers_starting(list, "count_down("));
    CPPL_CHECK(std::ranges::all_of(list.items, [](const CompletionItem& item) {
        return item.filterText.empty() || item.filterText.starts_with("c") || item.filterText.starts_with("C");
    }));
}

CPPL_TEST(a_call_is_inserted_with_its_parameters_as_placeholders) {
    Server server = make_server(true);
    const CompletionList list = complete_at(server, "#include <vector>\n"
                                                    "void add(std::vector<int>& v) { v.push_b|; }\n");
    const auto push_back =
        std::ranges::find_if(list.items, [](const CompletionItem& item) { return item.filterText == "push_back"; });
    CPPL_CHECK(push_back != list.items.end());
    if (push_back != list.items.end()) {
        CPPL_CHECK(push_back->snippet);
        CPPL_CHECK(push_back->insertText.starts_with("push_back(${1:"));
        CPPL_CHECK(push_back->kind == CompletionItemKind::Method);
    }
    // A client that takes no snippets is given the name alone.
    Server plain = make_server(false);
    const CompletionList without = complete_at(plain, "#include <vector>\n"
                                                      "void add(std::vector<int>& v) { v.push_b|; }\n");
    const auto name =
        std::ranges::find_if(without.items, [](const CompletionItem& item) { return item.filterText == "push_back"; });
    CPPL_CHECK(name != without.items.end());
    if (name != without.items.end()) {
        CPPL_CHECK(!name->snippet);
        CPPL_CHECK_EQ(name->insertText, std::string("push_back"));
    }
}

CPPL_TEST(a_name_the_projection_generated_is_never_offered) {
    Server server = make_server();
    const CompletionList list = complete_at(server, "pure int identity(int x) { return x; }\n"
                                                    "law identity_returns_input(int x)\n"
                                                    "    proves (identity(x) == x);\n"
                                                    "int main() { return __|; }\n");
    CPPL_CHECK(std::ranges::none_of(list.items,
                                    [](const CompletionItem& item) { return item.filterText.starts_with("__cppl"); }));
}

CPPL_TEST(a_name_inside_a_law_is_completed_by_clang) {
    Server server = make_server();
    const CompletionList list = complete_at(server, "pure int identity(int x) { return x; }\n"
                                                    "law identity_returns_input(int x)\n"
                                                    "    proves (identi|(x) == x);\n");
    CPPL_CHECK(offers_starting(list, "identity("));
}

CPPL_TEST(cppl_declarations_are_offered_where_a_declaration_may_begin) {
    Server server = make_server();
    const CompletionList top = complete_at(server, "int helper();\n"
                                                   "|\n");
    CPPL_CHECK(offers(top, "law"));
    CPPL_CHECK(offers(top, "trusted law"));
    CPPL_CHECK(offers(top, "proof"));
    CPPL_CHECK(offers(top, "verified"));
    CPPL_CHECK(offers(top, "type"));
    const CompletionItem* law = find(top, "law");
    CPPL_CHECK(law != nullptr);
    if (law != nullptr) {
        CPPL_CHECK(law->snippet);
        CPPL_CHECK(law->insertText.find("proves (${3:proposition});") != std::string::npos);
    }
    // Inside a function body a Law cannot be declared.
    Server inside = make_server();
    CPPL_CHECK(!offers(complete_at(inside, "int main() {\n    |\n}\n"), "law"));
}

CPPL_TEST(a_proof_body_offers_statements_and_the_names_they_take) {
    Server server = make_server();
    const std::string head = "proof other(int y)\n"
                             "    proves (y == y)\n"
                             "{\n"
                             "    refl;\n"
                             "}\n"
                             "proof mine(int x)\n"
                             "    proves (x == x)\n"
                             "{\n"
                             "    assume h : x == x;\n";
    const CompletionList statements = complete_at(server, head + "    |\n}\n");
    CPPL_CHECK(offers(statements, "refl"));
    CPPL_CHECK(offers(statements, "exact"));
    CPPL_CHECK(offers(statements, "assume"));
    CPPL_CHECK(offers(statements, "cases"));
    CPPL_CHECK(!offers(statements, "law"));

    Server named = make_server();
    const CompletionList names = complete_at(named, head + "    exact |\n}\n");
    CPPL_CHECK(offers(names, "other"));
    CPPL_CHECK(offers(names, "h"));
    // A proof does not name itself.
    CPPL_CHECK(!offers(names, "mine"));
    CPPL_CHECK(!offers(names, "refl"));
}

CPPL_TEST(clauses_are_offered_after_a_declarations_parameters) {
    Server law = make_server();
    const CompletionList after_law = complete_at(law, "law l(int x) |\n");
    CPPL_CHECK(offers(after_law, "proves"));
    CPPL_CHECK(offers(after_law, "expects"));

    Server function = make_server();
    const CompletionList contract = complete_at(function, "verified int keep(int x)\n"
                                                          "    |\n"
                                                          "{\n"
                                                          "    return x;\n"
                                                          "}\n");
    CPPL_CHECK(offers(contract, "ensures"));
    CPPL_CHECK(offers(contract, "expects"));
}

CPPL_TEST(completion_in_an_unknown_document_is_empty) {
    Server server = make_server();
    TextDocumentIdentifier id;
    id.uri = "file:///never-opened.cpp";
    CPPL_CHECK(server.text_document_completion(id, Position{0, 0}).items.empty());
}
