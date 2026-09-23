#include "cppl/lsp/completion.hpp"

#include "cppl/clang/editor.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/lsp/protocol.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <optional>
#include <ranges>
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

// The identifier token that ends before `offset`, skipping space.
std::optional<std::string_view> word_before(const frontend::TokenStream& tokens, std::size_t offset) {
    const frontend::Token* last = nullptr;
    for (const frontend::Token& token : tokens.tokens()) {
        if (token.kind == frontend::TokenKind::EndOfFile || token.span.end() > offset) {
            break;
        }
        last = &token;
    }
    if (last == nullptr || last->kind != frontend::TokenKind::Identifier) {
        return std::nullopt;
    }
    return last->text;
}

// The last character before `offset` that is not space.
char last_written(std::string_view text, std::size_t offset) {
    while (offset > 0) {
        --offset;
        if (std::isspace(static_cast<unsigned char>(text[offset])) == 0) {
            return text[offset];
        }
    }
    return '\0';
}

struct Snippet {
    std::string_view label;
    std::string_view detail;
    std::string_view body;  // in snippet syntax
    std::string_view plain; // what is inserted without snippets
};

CompletionItem snippet_item(const Snippet& snippet, bool snippets) {
    CompletionItem item;
    item.label = std::string(snippet.label);
    item.kind = CompletionItemKind::Snippet;
    item.detail = std::string(snippet.detail);
    item.insertText = std::string(snippets ? snippet.body : snippet.plain);
    item.snippet = snippets;
    item.filterText = item.label;
    // C++L's own words where they apply, ahead of Clang's.
    item.sortText = "0" + item.label;
    return item;
}

void add_matching(std::vector<CompletionItem>& items, CompletionItem item, std::string_view prefix) {
    if (match(item.filterText.empty() ? item.label : item.filterText, prefix).has_value()) {
        items.push_back(std::move(item));
    }
}

constexpr Snippet kDeclarations[] = {
    {"law", "C++L: a Law", "law ${1:name}(${2:parameters})\n    proves (${3:proposition});", "law"},
    {"trusted law", "C++L: an explicit assumption",
     "trusted law ${1:name}(${2:parameters})\n    proves (${3:proposition});", "trusted law"},
    {"proof", "C++L: a proof",
     "proof ${1:name}(${2:parameters})\n    proves (${3:law}(${4:arguments}))\n{\n    ${0:refl;}\n}", "proof"},
    {"verified", "C++L: a verified function",
     "verified ${1:int} ${2:name}(${3:parameters})\n    ensures (${4:result == 0})\n{\n    $0\n}", "verified"},
    {"type", "C++L: a refinement type", "type ${1:Name} = ${2:int} where (${3:self >= 0});", "type"},
    {"pure", "C++L: a function the formal core may unfold", "pure ", "pure"},
};

constexpr Snippet kStatements[] = {
    {"refl", "C++L: both sides are definitionally equal", "refl;", "refl;"},
    {"exact", "C++L: the goal is what a proof proves", "exact ${1:proof};", "exact"},
    {"apply", "C++L: a proof's conclusion, applied", "apply ${1:proof};", "apply"},
    {"rewrite", "C++L: rewrite the goal by an equality", "rewrite ${1:h};", "rewrite"},
    {"assume", "C++L: name a premise", "assume ${1:h} : ${2:proposition};", "assume"},
    {"contradiction", "C++L: the context cannot occur", "contradiction ${1:evidence};", "contradiction"},
    {"cases", "C++L: one arm per state of a subject", "cases ${1:subject} {\n    $0\n}", "cases"},
    {"decompose", "C++L: a subject's components", "decompose ${1:subject} {\n    $0\n}", "decompose"},
};

constexpr Snippet kContractClauses[] = {
    {"expects", "C++L: a precondition", "expects (${1:condition})", "expects"},
    {"ensures", "C++L: a postcondition", "ensures (${1:result == 0})", "ensures"},
};

constexpr Snippet kLawClauses[] = {
    {"proves", "C++L: what it states", "proves (${1:proposition})", "proves"},
    {"expects", "C++L: the premise it is stated under", "expects (${1:condition})", "expects"},
};

// The tokens that end at or before `offset`.
std::vector<const frontend::Token*> tokens_before(const frontend::TokenStream& tokens, std::size_t offset) {
    std::vector<const frontend::Token*> before;
    for (const frontend::Token& token : tokens.tokens()) {
        if (token.kind == frontend::TokenKind::EndOfFile || token.span.end() > offset) {
            break;
        }
        before.push_back(&token);
    }
    return before;
}

// The `(` that the `)` at `close` closes, or nothing.
std::optional<std::size_t> opening(const std::vector<const frontend::Token*>& tokens, std::size_t close) {
    int depth = 0;
    for (std::size_t index = close + 1; index > 0; --index) {
        const frontend::Token& token = *tokens[index - 1];
        if (token.is_punctuator(")")) {
            ++depth;
        } else if (token.is_punctuator("(") && --depth == 0) {
            return index - 1;
        }
    }
    return std::nullopt;
}

// A proof body still open at `offset`, found from the tokens alone: a proof
// being written does not parse, so it is not in the recognized syntax. The
// body is the innermost `{` not yet closed that follows
// `proof name(...) proves (...)`.
struct OpenProof {
    std::string_view name;
    std::size_t open = 0; // the body's `{`, as an index into the tokens
};

std::optional<OpenProof> open_proof_at(const std::vector<const frontend::Token*>& before) {
    const auto proof_head = [&before](std::size_t brace) -> std::optional<OpenProof> {
        if (brace == 0 || !before[brace - 1]->is_punctuator(")")) {
            return std::nullopt;
        }
        const std::optional<std::size_t> claim = opening(before, brace - 1);
        if (!claim.has_value() || *claim < 2 || !before[*claim - 1]->is_identifier("proves") ||
            !before[*claim - 2]->is_punctuator(")")) {
            return std::nullopt;
        }
        const std::optional<std::size_t> parameters = opening(before, *claim - 2);
        if (!parameters.has_value() || *parameters < 2 ||
            before[*parameters - 1]->kind != frontend::TokenKind::Identifier ||
            !before[*parameters - 2]->is_identifier("proof")) {
            return std::nullopt;
        }
        return OpenProof{before[*parameters - 1]->text, brace};
    };
    std::vector<std::optional<OpenProof>> open;
    for (std::size_t index = 0; index < before.size(); ++index) {
        if (before[index]->is_punctuator("{")) {
            open.push_back(proof_head(index));
        } else if (before[index]->is_punctuator("}") && !open.empty()) {
            open.pop_back();
        }
    }
    for (const std::optional<OpenProof>& enclosing : std::views::reverse(open)) {
        if (enclosing.has_value()) {
            return enclosing;
        }
    }
    return std::nullopt;
}

// Whether the tokens before `offset` end a Law's or a proof's name and
// parameters with no clause yet: `law name(...)`, `trusted law name(...)`.
std::optional<std::string_view> declaration_head_before(const frontend::TokenStream& tokens, std::size_t offset) {
    std::vector<const frontend::Token*> before;
    for (const frontend::Token& token : tokens.tokens()) {
        if (token.kind == frontend::TokenKind::EndOfFile || token.span.end() > offset) {
            break;
        }
        before.push_back(&token);
    }
    if (before.empty() || !before.back()->is_punctuator(")")) {
        return std::nullopt;
    }
    int depth = 0;
    std::size_t index = before.size();
    while (index > 0) {
        --index;
        if (before[index]->is_punctuator(")")) {
            ++depth;
        } else if (before[index]->is_punctuator("(") && --depth == 0) {
            break;
        }
    }
    if (depth != 0 || index < 2 || before[index - 1]->kind != frontend::TokenKind::Identifier) {
        return std::nullopt;
    }
    const std::string_view keyword = before[index - 2]->text;
    if (keyword == "law" || keyword == "proof") {
        return keyword;
    }
    return std::nullopt;
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

std::vector<CompletionItem> cppl_completions(const frontend::TokenStream& tokens, const frontend::Syntax& syntax,
                                             std::string_view text, std::size_t offset, std::string_view prefix,
                                             clangbridge::Scope scope, bool snippets) {
    std::vector<CompletionItem> items;
    const std::size_t start = offset - prefix.size();
    const char before = last_written(text, start);

    const std::vector<const frontend::Token*> written = tokens_before(tokens, start);
    if (const std::optional<OpenProof> proof = open_proof_at(written)) {
        // After `exact`, `apply`, `rewrite` or `contradiction`: what it can
        // name -- another proof, a trusted Law, a name this body assumed.
        const std::optional<std::string_view> word = word_before(tokens, start);
        if (word.has_value() &&
            (*word == "exact" || *word == "apply" || *word == "rewrite" || *word == "contradiction")) {
            std::vector<std::string> offered;
            const auto offer = [&](std::string_view name, std::string_view detail, CompletionItemKind kind) {
                if (name == proof->name || std::ranges::find(offered, name) != offered.end()) {
                    return;
                }
                offered.emplace_back(name);
                CompletionItem item;
                item.label = std::string(name);
                item.kind = kind;
                item.detail = std::string(detail);
                item.sortText = "0" + item.label;
                add_matching(items, std::move(item), prefix);
            };
            const std::vector<frontend::Token>& all = tokens.tokens();
            for (std::size_t index = 0; index + 1 < all.size(); ++index) {
                if (all[index].is_identifier("proof") && all[index + 1].kind == frontend::TokenKind::Identifier) {
                    offer(all[index + 1].text, "proof", CompletionItemKind::Reference);
                } else if (index + 2 < all.size() && all[index].is_identifier("trusted") &&
                           all[index + 1].is_identifier("law") &&
                           all[index + 2].kind == frontend::TokenKind::Identifier) {
                    offer(all[index + 2].text, "trusted law", CompletionItemKind::Reference);
                }
            }
            for (std::size_t index = proof->open; index + 2 < written.size(); ++index) {
                if (written[index]->is_identifier("assume") &&
                    written[index + 1]->kind == frontend::TokenKind::Identifier &&
                    written[index + 2]->is_punctuator(":")) {
                    offer(written[index + 1]->text, "assumption", CompletionItemKind::Variable);
                }
            }
            return items;
        }
        // At the start of a statement: the statements.
        if (before == '{' || before == ';' || before == '}') {
            for (const Snippet& snippet : kStatements) {
                add_matching(items, snippet_item(snippet, snippets), prefix);
            }
        }
        return items;
    }

    // After a Law's or a proof's parameters: its clauses.
    if (const std::optional<std::string_view> head = declaration_head_before(tokens, start)) {
        for (const Snippet& snippet : kLawClauses) {
            if (*head == "proof" && snippet.label == "expects") {
                continue;
            }
            add_matching(items, snippet_item(snippet, snippets), prefix);
        }
        return items;
    }

    // Between a verified function's parameters and its body: its contract.
    for (const frontend::VerifiedFunction& verified : syntax.verified_functions) {
        if (start > verified.parameters.end() && start < verified.body_open && before == ')') {
            for (const Snippet& snippet : kContractClauses) {
                add_matching(items, snippet_item(snippet, snippets), prefix);
            }
            return items;
        }
    }

    // At the start of a declaration at namespace scope: C++L's declarations.
    if (scope == clangbridge::Scope::Namespace &&
        (before == '\0' || before == ';' || before == '}' || before == '{' || before == '>' || before == '"')) {
        for (const Snippet& snippet : kDeclarations) {
            add_matching(items, snippet_item(snippet, snippets), prefix);
        }
    }
    return items;
}

} // namespace cppl::lsp
