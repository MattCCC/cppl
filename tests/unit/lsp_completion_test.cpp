// Completion: Clang's names and keywords for C++, and C++L's own words where the
// grammar admits them (src/lsp/include/cppl/lsp/completion.hpp).

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/lsp/position.hpp"
#include "cppl/lsp/protocol.hpp"
#include "cppl/lsp/server.hpp"
#include "cppl/testing/test.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

// C++L's own word, which a Clang keyword of the same spelling is not.
bool is_snippet(const CompletionItem* item) {
    return item != nullptr && item->kind == CompletionItemKind::Snippet;
}

// A name C++L offers as the evidence a statement names, which is not what
// Clang offers for the same name.
bool is_evidence(const CompletionItem* item, const std::string& detail) {
    return item != nullptr && item->detail == detail &&
           (item->kind == CompletionItemKind::Reference || item->kind == CompletionItemKind::Variable);
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

CPPL_TEST(a_word_being_typed_is_completed_where_the_compiler_places_it) {
    const std::string head = "proof other(int y)\n    proves (y == y)\n{\n    refl;\n}\n"
                             "proof mine(int x)\n    proves (x == x)\n{\n";
    Server clause = make_server();
    CPPL_CHECK(is_snippet(find(complete_at(clause, "law l(int x) pro|\n"), "proves")));
    Server statement = make_server();
    const CompletionList statements = complete_at(statement, head + "    ass|\n}\n");
    CPPL_CHECK(is_snippet(find(statements, "assume")));
    CPPL_CHECK(!offers(statements, "refl"));
    // The proof itself, as C++L evidence, not the function the projection
    // declares for it.
    Server evidence = make_server();
    const CompletionList named = complete_at(evidence, head + "    exact oth|\n}\n");
    CPPL_CHECK(is_evidence(find(named, "other"), "proof"));
    Server contract = make_server();
    CPPL_CHECK(is_snippet(
        find(complete_at(contract, "verified int keep(int x)\n    ens|\n{\n    return x;\n}\n"), "ensures")));
}

CPPL_TEST(a_proof_body_still_being_written_offers_statements) {
    Server unterminated = make_server();
    const CompletionList list =
        complete_at(unterminated, "int helper();\nproof p(int x)\n    proves (x == x)\n{\n    refl;\n    |");
    CPPL_CHECK(offers(list, "refl"));
    CPPL_CHECK(offers(list, "exact"));
    CPPL_CHECK(!offers(list, "law"));
    // After a statement the recognizer cannot read, the next is offered still.
    Server unread = make_server();
    CPPL_CHECK(offers(complete_at(unread, "proof p(int x)\n    proves (x == x)\n{\n    rewrite;\n    |\n}\n"), "refl"));
}

namespace {

// What a snippet inserts once each placeholder keeps its default text and the
// final tab stop holds `last`.
std::string instantiated(std::string_view snippet, std::string_view last) {
    std::string text;
    for (std::size_t at = 0; at < snippet.size(); ++at) {
        if (snippet[at] != '$') {
            text.push_back(snippet[at]);
            continue;
        }
        if (at + 1 < snippet.size() && snippet[at + 1] == '{') {
            const std::size_t colon = snippet.find(':', at);
            const std::size_t close = snippet.find('}', colon);
            text += snippet.substr(colon + 1, close - colon - 1);
            at = close;
            continue;
        }
        text += last;
        while (at + 1 < snippet.size() && std::isdigit(static_cast<unsigned char>(snippet[at + 1])) != 0) {
            ++at;
        }
    }
    return text;
}

// The compiler's own reading of `text`.
struct Read {
    std::string text;
    cppl::frontend::Syntax syntax;
};

Read compiled(std::string text) {
    Read read{std::move(text), {}};
    const cppl::frontend::TokenStream stream = cppl::frontend::lex(read.text, "main.cpp");
    cppl::diagnostics::Engine engine;
    read.syntax = cppl::frontend::recognize(stream, engine);
    return read;
}

bool has_clause(const std::vector<cppl::frontend::Clause>& clauses, const std::string& word) {
    return std::ranges::any_of(clauses, [&word](const cppl::frontend::Clause& clause) {
        return cppl::frontend::describe(clause.kind) == word;
    });
}

bool reads_law(const Read& read, bool trusted) {
    return read.syntax.laws.size() == 1 && read.syntax.laws[0].trusted == trusted;
}

bool reads_law_clause(const Read& read, const std::string& word) {
    return read.syntax.laws.size() == 1 && has_clause(read.syntax.laws[0].clauses, word);
}

bool reads_contract_clause(const Read& read, const std::string& word) {
    return read.syntax.verified_functions.size() == 1 && has_clause(read.syntax.verified_functions[0].clauses, word);
}

bool reads_statement(const Read& read, const std::string& word) {
    return read.syntax.proofs.size() == 1 && read.syntax.proofs[0].statements.size() == 1 &&
           cppl::frontend::statement_keyword(word) == read.syntax.proofs[0].statements[0].kind;
}

} // namespace

// Every snippet completion inserts is what the compiler's recognizer reads it
// as, so the text offered can never drift from the grammar the compiler reads.
CPPL_TEST(every_snippet_is_what_the_compiler_reads_it_as) {
    Server top = make_server();
    const CompletionList declarations = complete_at(top, "int helper();\n|\n");
    std::size_t checked = 0;
    for (const CompletionItem& item : declarations.items) {
        if (item.kind != CompletionItemKind::Snippet) {
            continue;
        }
        ++checked;
        const std::string written = instantiated(item.insertText, "");
        if (item.label == "law") {
            CPPL_CHECK(reads_law(compiled(written), false));
        } else if (item.label == "trusted law") {
            CPPL_CHECK(reads_law(compiled(written), true));
        } else if (item.label == "proof") {
            CPPL_CHECK_EQ(compiled(written).syntax.proofs.size(), std::size_t{1});
        } else if (item.label == "verified") {
            CPPL_CHECK(reads_contract_clause(compiled(written), "ensures"));
        } else if (item.label == "type") {
            CPPL_CHECK_EQ(compiled(written).syntax.refinement_types.size(), std::size_t{1});
        } else if (item.label == "pure") {
            CPPL_CHECK_EQ(compiled(written + "int f(int x) { return x; }\n").syntax.pure_markers.size(),
                          std::size_t{1});
        } else {
            ::cppl::testing::fail(__FILE__, __LINE__, "an unchecked declaration snippet: " + item.label);
        }
    }
    CPPL_CHECK_EQ(checked, std::size_t{6});

    const std::string head = "proof p(int x)\n    proves (x == x)\n{\n    ";
    Server body = make_server();
    const CompletionList statements = complete_at(body, head + "|\n}\n");
    checked = 0;
    for (const CompletionItem& item : statements.items) {
        if (item.kind != CompletionItemKind::Snippet) {
            continue;
        }
        ++checked;
        CPPL_CHECK(reads_statement(compiled(head + instantiated(item.insertText, "State::one => { refl; }") + "\n}\n"),
                                   item.label));
    }
    CPPL_CHECK_EQ(checked, std::size_t{8});

    Server law = make_server();
    checked = 0;
    for (const CompletionItem& item : complete_at(law, "law l(int x) |\n").items) {
        if (item.kind != CompletionItemKind::Snippet) {
            continue;
        }
        ++checked;
        const std::string conclusion = item.label == "proves" ? "" : "\n    proves (x == x)";
        CPPL_CHECK(reads_law_clause(
            compiled("law l(int x)\n    " + instantiated(item.insertText, "") + conclusion + ";\n"), item.label));
    }
    CPPL_CHECK_EQ(checked, std::size_t{2});
    Server contract = make_server();
    checked = 0;
    for (const CompletionItem& item :
         complete_at(contract, "verified int keep(int x)\n    |\n{\n    return x;\n}\n").items) {
        if (item.kind != CompletionItemKind::Snippet) {
            continue;
        }
        ++checked;
        CPPL_CHECK(reads_contract_clause(
            compiled("verified int keep(int x)\n    " + instantiated(item.insertText, "") + "\n{\n    return x;\n}\n"),
            item.label));
    }
    CPPL_CHECK_EQ(checked, std::size_t{2});
}

CPPL_TEST(completion_in_an_unknown_document_is_empty) {
    Server server = make_server();
    TextDocumentIdentifier id;
    id.uri = "file:///never-opened.cpp";
    CPPL_CHECK(server.text_document_completion(id, Position{0, 0}).items.empty());
}
