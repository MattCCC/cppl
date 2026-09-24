#include "cppl/lsp/rename.hpp"

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/structure.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/frontend/words.hpp"
#include "cppl/lsp/position.hpp"
#include "cppl/lsp/protocol.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cppl::lsp {

namespace {

// [lex.key] and the alternative tokens of [lex.digraph], as of C++23.
constexpr auto kKeywords = std::to_array<std::string_view>({
    "alignas",       "alignof",     "asm",       "auto",      "bool",         "break",
    "case",          "catch",       "char",      "char8_t",   "char16_t",     "char32_t",
    "class",         "concept",     "const",     "consteval", "constexpr",    "constinit",
    "const_cast",    "continue",    "co_await",  "co_return", "co_yield",     "decltype",
    "default",       "delete",      "do",        "double",    "dynamic_cast", "else",
    "enum",          "explicit",    "export",    "extern",    "false",        "float",
    "for",           "friend",      "goto",      "if",        "inline",       "int",
    "long",          "mutable",     "namespace", "new",       "noexcept",     "nullptr",
    "operator",      "private",     "protected", "public",    "register",     "reinterpret_cast",
    "requires",      "return",      "short",     "signed",    "sizeof",       "static",
    "static_assert", "static_cast", "struct",    "switch",    "template",     "this",
    "thread_local",  "throw",       "true",      "try",       "typedef",      "typeid",
    "typename",      "union",       "unsigned",  "using",     "virtual",      "void",
    "volatile",      "wchar_t",     "while",     "and",       "and_eq",       "bitand",
    "bitor",         "compl",       "not",       "not_eq",    "or",           "or_eq",
    "xor",           "xor_eq",
});

bool starts_identifier(char character) {
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || character == '_';
}

bool continues_identifier(char character) {
    return starts_identifier(character) || (character >= '0' && character <= '9');
}

// What the recognizer reads in `text`: how many constructs of each kind it
// found, and how many statements each proof holds. A rename that leaves this
// alone leaves C++L alone. Only C++L's words (checked apart) and the arm
// labels a representation reserves (WORD-005) change what the recognizer
// reads. Read as the formatter reads, where a statement with an arm label the
// recognizer cannot read is left out whole, however deep the arm, and a split
// on a path with it, so a rename that makes one readable, or unreadable,
// changes this.
std::string shape(const std::string& path, const std::string& text) {
    const frontend::TokenStream stream = frontend::lex(text, path);
    diagnostics::Engine engine;
    const frontend::Syntax syntax = frontend::recognize(stream, engine, frontend::RecognitionMode::Edit);
    std::string out;
    for (const std::size_t count :
         {syntax.laws.size(), syntax.proofs.size(), syntax.pure_markers.size(), syntax.verified_functions.size(),
          syntax.loops.size(), syntax.path_contradictions.size(), syntax.path_splits.size(),
          syntax.refinement_types.size(), syntax.explicit_instantiations.size(), syntax.unchecked_clauses.size()}) {
        out += std::to_string(count) + ",";
    }
    for (const frontend::ProofDeclaration& proof : syntax.proofs) {
        out += std::to_string(proof.statements.size()) + ";";
    }
    return out;
}

} // namespace

std::optional<std::string> refuse_identifier(std::string_view name) {
    if (name.empty()) {
        return "a name cannot be empty";
    }
    if (!starts_identifier(name.front()) || !std::ranges::all_of(name, continues_identifier)) {
        return "'" + std::string(name) + "' is not a C++ identifier";
    }
    if (std::ranges::find(kKeywords, name) != kKeywords.end()) {
        return "'" + std::string(name) + "' is a C++ keyword";
    }
    return std::nullopt;
}

std::string apply_edits(const std::string& text, const std::vector<TextEdit>& edits) {
    const PositionMapper mapper(text);
    std::vector<std::pair<std::size_t, const TextEdit*>> at;
    at.reserve(edits.size());
    for (const TextEdit& edit : edits) {
        at.emplace_back(mapper.position_to_byte_offset(edit.range.start), &edit);
    }
    // Last first, so each edit's offsets still hold when it is applied.
    std::ranges::sort(at, [](const auto& lhs, const auto& rhs) { return lhs.first > rhs.first; });
    std::string out = text;
    for (const auto& [start, edit] : at) {
        const std::size_t end = mapper.position_to_byte_offset(edit->range.end);
        out.replace(start, end - start, edit->newText);
    }
    return out;
}

std::optional<std::string> changes_cppl(const std::string& path, const std::string& text,
                                        const std::vector<TextEdit>& edits, std::string_view name) {
    if (frontend::is_cppl_word(name)) {
        const frontend::TokenStream stream = frontend::lex(text, path);
        diagnostics::Engine engine;
        const frontend::Syntax syntax = frontend::recognize(stream, engine, frontend::RecognitionMode::Edit);
        const PositionMapper mapper(text);
        for (const TextEdit& edit : edits) {
            if (!frontend::enclosing(syntax, mapper.position_to_byte_offset(edit.range.start)).empty()) {
                std::string why = "'";
                why += name;
                why += "' is a C++L word, and a place to rename in ";
                why += path;
                why += " lies inside C++L";
                return why;
            }
        }
    }
    if (shape(path, text) != shape(path, apply_edits(text, edits))) {
        std::string why = "renaming to '";
        why += name;
        why += "' would change the C++L ";
        why += path;
        why += " holds";
        return why;
    }
    return std::nullopt;
}

} // namespace cppl::lsp
