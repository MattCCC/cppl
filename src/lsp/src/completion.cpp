#include "cppl/lsp/completion.hpp"

#include "cppl/clang/editor.hpp"
#include "cppl/frontend/admissible.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/lsp/protocol.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace cppl::lsp {

namespace {

constexpr std::size_t kMostItems = 150;

char lower(char character) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
}

// How well a candidate matches what has been typed of it: 0 for a prefix in
// the same case, 1 for a prefix in any case, 2 for the typed letters in order
// from its first, nothing for no match.
std::optional<int> match(std::string_view candidate, std::string_view prefix) {
    if (prefix.empty()) {
        return 0;
    }
    if (candidate.starts_with(prefix)) {
        return 0;
    }
    if (candidate.empty() || lower(candidate.front()) != lower(prefix.front())) {
        return std::nullopt;
    }
    if (candidate.size() >= prefix.size() &&
        std::ranges::equal(candidate.substr(0, prefix.size()), prefix,
                           [](char lhs, char rhs) { return lower(lhs) == lower(rhs); })) {
        return 1;
    }
    std::size_t at = 1;
    for (std::size_t index = 1; index < prefix.size(); ++index) {
        while (at < candidate.size() && lower(candidate[at]) != lower(prefix[index])) {
            ++at;
        }
        if (at == candidate.size()) {
            return std::nullopt;
        }
        ++at;
    }
    return 2;
}

CompletionItemKind item_kind(clangbridge::Completion::Kind kind) {
    using Kind = clangbridge::Completion::Kind;
    switch (kind) {
        case Kind::Function:
            return CompletionItemKind::Function;
        case Kind::Method:
            return CompletionItemKind::Method;
        case Kind::Constructor:
            return CompletionItemKind::Constructor;
        case Kind::Field:
            return CompletionItemKind::Field;
        case Kind::Variable:
        case Kind::Parameter:
            return CompletionItemKind::Variable;
        case Kind::Class:
        case Kind::TypeAlias:
            return CompletionItemKind::Class;
        case Kind::Struct:
            return CompletionItemKind::Struct;
        case Kind::Enum:
            return CompletionItemKind::Enum;
        case Kind::Enumerator:
            return CompletionItemKind::EnumMember;
        case Kind::Namespace:
            return CompletionItemKind::Module;
        case Kind::TemplateParameter:
            return CompletionItemKind::TypeParameter;
        case Kind::Macro:
            return CompletionItemKind::Constant;
        case Kind::Concept:
            return CompletionItemKind::Interface;
        case Kind::Keyword:
            return CompletionItemKind::Keyword;
        case Kind::Other:
            return CompletionItemKind::Text;
    }
    return CompletionItemKind::Text;
}

// C++L's own words where they apply, ahead of Clang's.
CompletionItem snippet_item(std::string label, std::string_view detail, std::string body, std::string plain,
                            bool snippets) {
    CompletionItem item;
    item.label = std::move(label);
    item.kind = CompletionItemKind::Snippet;
    item.detail = std::string(detail);
    item.insertText = snippets ? std::move(body) : std::move(plain);
    item.snippet = snippets;
    item.filterText = item.label;
    item.sortText = "0" + item.label;
    return item;
}

void add_matching(std::vector<CompletionItem>& items, CompletionItem item, std::string_view prefix) {
    if (match(item.filterText.empty() ? item.label : item.filterText, prefix).has_value()) {
        items.push_back(std::move(item));
    }
}

// A declaration, laid out as the formatter lays it out. Each is checked to be
// what the recognizer reads as that declaration (lsp_completion_test).
struct Declaration {
    std::string_view label;
    std::string_view detail;
    std::string_view body;  // in snippet syntax
    std::string_view plain; // what is inserted without snippets
};

constexpr Declaration kDeclarations[] = {
    {"law", "C++L: a Law", "law ${1:name}(${2:parameters})\n    proves (${3:proposition});", "law"},
    {"trusted law", "C++L: an explicit assumption",
     "trusted law ${1:name}(${2:parameters})\n    proves (${3:proposition});", "trusted law"},
    {"proof", "C++L: a proof",
     "proof ${1:name}(${2:parameters})\n    proves (${3:law}(${4:arguments}))\n{\n    ${0:refl;}\n}", "proof"},
    {"verified", "C++L: a verified function",
     "verified ${1:int} ${2:name}(${3:parameters})\n    ensures (${4:result == 0})\n{\n    $0\n}", "verified"},
    {"type", "C++L: a refinement type", "type ${1:Name} = ${2:int} where (${3:self >= 0});", "type"},
    {"pure", "C++L: a function the formal core may unfold", "pure ", "pure"},
    {"unsafe", "C++L: a function whose calls cross an unsafe boundary", "unsafe ", "unsafe"},
};

// A proof statement, spelled with the recognizer's own word for it: what
// follows the word, with and without snippets.
struct Statement {
    frontend::ProofStatementKind kind;
    std::string_view detail;
    std::string_view after;
    std::string_view plain_after;
};

constexpr Statement kStatements[] = {
    {frontend::ProofStatementKind::Reflexivity, "C++L: both sides are definitionally equal", ";", ";"},
    {frontend::ProofStatementKind::Exact, "C++L: the goal is what a proof proves", " ${1:proof};", ""},
    {frontend::ProofStatementKind::Apply, "C++L: a proof's conclusion, applied", " ${1:proof};", ""},
    {frontend::ProofStatementKind::Rewrite, "C++L: rewrite the goal by an equality", " ${1:h};", ""},
    {frontend::ProofStatementKind::Assume, "C++L: name a premise", " ${1:h} : ${2:proposition};", ""},
    {frontend::ProofStatementKind::Contradiction, "C++L: the context cannot occur", " ${1:evidence};", ""},
    {frontend::ProofStatementKind::Cases, "C++L: one arm per state of a subject", " ${1:subject} {\n    $0\n}", ""},
    {frontend::ProofStatementKind::Decompose, "C++L: a subject's components", " ${1:subject} {\n    $0\n}", ""},
};

// A clause, spelled with the recognizer's own word for it, and what its
// parentheses hold until the author writes it.
struct ClauseText {
    frontend::ClauseOwner owner;
    frontend::ClauseKind kind;
    std::string_view detail;
    std::string_view placeholder;
};

constexpr ClauseText kClauses[] = {
    {frontend::ClauseOwner::Law, frontend::ClauseKind::Expects, "C++L: the premise it is stated under", "condition"},
    {frontend::ClauseOwner::Law, frontend::ClauseKind::Proves, "C++L: what it states", "proposition"},
    {frontend::ClauseOwner::Proof, frontend::ClauseKind::Proves, "C++L: what it proves", "proposition"},
    {frontend::ClauseOwner::VerifiedFunction, frontend::ClauseKind::Expects, "C++L: a precondition", "condition"},
    {frontend::ClauseOwner::VerifiedFunction, frontend::ClauseKind::Ensures, "C++L: a postcondition", "result == 0"},
};

std::string_view evidence_detail(frontend::Evidence::Kind kind) {
    switch (kind) {
        case frontend::Evidence::Kind::Assumption:
            return "assumption";
        case frontend::Evidence::Kind::TrustedLaw:
            return "trusted law";
        case frontend::Evidence::Kind::Proof:
            return "proof";
    }
    return "proof";
}

} // namespace

CompletionList cpp_completions(std::vector<clangbridge::Completion> completions, std::string_view prefix,
                               bool snippets) {
    std::vector<std::tuple<int, unsigned, clangbridge::Completion>> ranked;
    ranked.reserve(completions.size());
    const bool reserved_wanted = prefix.starts_with("_");
    for (clangbridge::Completion& completion : completions) {
        if (completion.typed.starts_with("__cppl_") || (!reserved_wanted && completion.typed.starts_with("__"))) {
            continue;
        }
        const std::optional<int> score = match(completion.typed, prefix);
        if (!score.has_value()) {
            continue;
        }
        ranked.emplace_back(*score, completion.priority, std::move(completion));
    }
    std::ranges::sort(ranked, [](const auto& lhs, const auto& rhs) {
        if (std::get<0>(lhs) != std::get<0>(rhs)) {
            return std::get<0>(lhs) < std::get<0>(rhs);
        }
        if (std::get<1>(lhs) != std::get<1>(rhs)) {
            return std::get<1>(lhs) < std::get<1>(rhs);
        }
        return std::get<2>(lhs).typed < std::get<2>(rhs).typed;
    });

    CompletionList list;
    list.incomplete = ranked.size() > kMostItems;
    const std::size_t shown = std::min(ranked.size(), kMostItems);
    list.items.reserve(shown);
    for (std::size_t index = 0; index < shown; ++index) {
        clangbridge::Completion& completion = std::get<2>(ranked[index]);
        CompletionItem item;
        item.label = completion.label;
        item.kind = item_kind(completion.kind);
        item.detail = completion.result;
        item.documentation = completion.documentation;
        item.filterText = completion.typed;
        item.deprecated = completion.deprecated;
        // Keep the order decided here: it already weighs the match and Clang's
        // ranking.
        std::string rank = std::to_string(index);
        item.sortText = "1" + std::string(5 - std::min<std::size_t>(rank.size(), 5), '0') + rank;
        if (snippets && completion.snippet != completion.typed) {
            item.insertText = completion.snippet;
            item.snippet = true;
        } else {
            item.insertText = completion.typed;
        }
        list.items.push_back(std::move(item));
    }
    return list;
}

std::vector<CompletionItem> cppl_completions(const frontend::TokenStream& tokens, const frontend::Syntax& draft,
                                             std::size_t start, std::string_view prefix, clangbridge::Scope scope,
                                             bool snippets) {
    std::vector<CompletionItem> items;
    const frontend::Admissible here = frontend::admissible_at(tokens, draft, start);

    if (here.evidence_for.has_value() && here.proof.has_value()) {
        for (const frontend::Evidence& evidence : frontend::evidence_at(draft, *here.proof, start)) {
            CompletionItem item;
            item.label = evidence.name;
            item.kind = evidence.kind == frontend::Evidence::Kind::Assumption ? CompletionItemKind::Variable
                                                                              : CompletionItemKind::Reference;
            item.detail = std::string(evidence_detail(evidence.kind));
            item.sortText = "0" + item.label;
            add_matching(items, std::move(item), prefix);
        }
        return items;
    }
    if (here.statement) {
        for (const Statement& statement : kStatements) {
            const std::string word = frontend::describe(statement.kind);
            add_matching(items,
                         snippet_item(word, statement.detail, word + std::string(statement.after),
                                      word + std::string(statement.plain_after), snippets),
                         prefix);
        }
        return items;
    }
    for (const frontend::ClauseKind kind : here.clauses) {
        const auto* text = std::ranges::find_if(kClauses, [&](const ClauseText& candidate) {
            return candidate.owner == here.owner && candidate.kind == kind;
        });
        if (text == std::ranges::end(kClauses)) {
            continue;
        }
        const std::string word = frontend::describe(kind);
        add_matching(
            items,
            snippet_item(word, text->detail, word + " (${1:" + std::string(text->placeholder) + "})", word, snippets),
            prefix);
    }
    if (!here.clauses.empty()) {
        return items;
    }
    if (here.declaration && scope == clangbridge::Scope::Namespace) {
        for (const Declaration& declaration : kDeclarations) {
            add_matching(items,
                         snippet_item(std::string(declaration.label), declaration.detail, std::string(declaration.body),
                                      std::string(declaration.plain), snippets),
                         prefix);
        }
    }
    return items;
}

} // namespace cppl::lsp
