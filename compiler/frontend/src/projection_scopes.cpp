// The formal namespace each Law and proof is projected into, and the
// directives a declaration may not hold.

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/projection.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/source/digest.hpp"
#include "cppl/source/location.hpp"
#include "projector.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cppl::frontend {

namespace {

// One namespace of the path a declaration stands in: its name, or nothing for
// an unnamed namespace, and whether ordinary C++ makes its members visible in
// the namespace enclosing it, as it does for an unnamed or an inline one.
struct NamespaceStep {
    std::string name;
    bool transparent = false;
};

// The namespaces `{` at `brace` opens, outermost first, or nothing when it opens
// any other scope. Read back to the `;`, `{` or `}` before it, as the
// recognizer classifies a scope: `namespace a::inline b {` opens `a` and the
// inline `b`, `inline namespace v {` the inline `v`, `namespace {` an unnamed
// one, and an attribute names none of them.
std::optional<std::vector<NamespaceStep>> namespace_opened(const std::vector<Token>& tokens, std::size_t brace) {
    std::size_t begin = brace;
    while (begin > 0 && !tokens[begin - 1].is_punctuator(";") && !tokens[begin - 1].is_punctuator("{") &&
           !tokens[begin - 1].is_punctuator("}")) {
        --begin;
    }
    std::size_t keyword = begin;
    while (keyword < brace && !tokens[keyword].is_identifier("namespace")) {
        ++keyword;
    }
    if (keyword == brace) {
        return std::nullopt;
    }
    bool inline_next = keyword > begin && tokens[keyword - 1].is_identifier("inline");
    std::vector<NamespaceStep> steps;
    int attribute = 0;
    for (std::size_t index = keyword + 1; index < brace; ++index) {
        const Token& token = tokens[index];
        if (token.is_punctuator("[")) {
            ++attribute;
            continue;
        }
        if (token.is_punctuator("]")) {
            --attribute;
            continue;
        }
        if (attribute != 0 || token.kind != TokenKind::Identifier) {
            continue;
        }
        if (token.text == "inline") {
            inline_next = true;
            continue;
        }
        steps.push_back(NamespaceStep{std::string(token.text), inline_next});
        inline_next = false;
    }
    if (steps.empty()) {
        steps.push_back(NamespaceStep{std::string{}, true});
    }
    return steps;
}

} // namespace

namespace detail::projector {

FormalScopes formal_scopes(const TokenStream& stream, const Syntax& syntax, const ProjectionOptions& options) {
    struct Site {
        std::size_t offset = 0;
        bool law = false;
        std::size_t index = 0;
    };
    std::vector<Site> sites;
    sites.reserve(syntax.laws.size() + syntax.proofs.size());
    for (std::size_t index = 0; index < syntax.laws.size(); ++index) {
        sites.push_back(Site{syntax.laws[index].range.span.offset, true, index});
    }
    for (std::size_t index = 0; index < syntax.proofs.size(); ++index) {
        sites.push_back(Site{syntax.proofs[index].range.span.offset, false, index});
    }
    std::ranges::sort(sites, [](const Site& lhs, const Site& rhs) { return lhs.offset < rhs.offset; });

    FormalScopes scopes;
    scopes.laws.resize(syntax.laws.size());
    scopes.proofs.resize(syntax.proofs.size());

    // The path of each formal namespace created so far, in the order created.
    std::vector<std::vector<NamespaceStep>> created;
    const auto key = [](const std::vector<NamespaceStep>& path) {
        std::string text;
        for (const NamespaceStep& step : path) {
            text += step.name.empty() ? std::string("(anonymous)") : step.name;
            text += '\n';
        }
        return text;
    };
    const auto name_of = [&options, &key](const std::vector<NamespaceStep>& path) {
        return options.generated_prefix + "formal_" + source::hash_bytes(key(path)).to_short_hex(12);
    };
    // Whether a declaration in `path` sees the members of `other`: `other` is
    // `path` or a namespace enclosing it, or is reached from one of those only
    // through unnamed and inline namespaces.
    const auto sees = [](const std::vector<NamespaceStep>& path, const std::vector<NamespaceStep>& other) {
        std::size_t shared = 0;
        while (shared < path.size() && shared < other.size() && path[shared].name == other[shared].name) {
            ++shared;
        }
        return std::all_of(other.begin() + static_cast<std::ptrdiff_t>(shared), other.end(),
                           [](const NamespaceStep& step) { return step.transparent; });
    };

    // Each scope the walk has entered, and the namespaces it opened if it is a
    // namespace. A Law or a proof stands only at namespace scope, where every
    // entry is one; anything else leaves the site with no formal namespace to
    // nominate, never with a wrong one.
    std::vector<std::optional<std::vector<NamespaceStep>>> open;
    // Whether each namespace path seen is transparent, which only its first
    // declaration has to say.
    std::vector<std::vector<NamespaceStep>> transparent;
    const std::vector<Token>& tokens = stream.tokens();
    std::size_t next = 0;
    for (const Site& site : sites) {
        while (next < tokens.size() && tokens[next].kind != TokenKind::EndOfFile &&
               tokens[next].span.offset < site.offset) {
            if (tokens[next].is_punctuator("{")) {
                open.push_back(namespace_opened(tokens, next));
            } else if (tokens[next].is_punctuator("}") && !open.empty()) {
                open.pop_back();
            }
            ++next;
        }
        std::vector<NamespaceStep> path;
        bool namespaces_only = true;
        for (const auto& scope : open) {
            if (!scope.has_value()) {
                namespaces_only = false;
                break;
            }
            for (const NamespaceStep& step : *scope) {
                path.push_back(step);
                // A namespace reopened without `inline` stays inline.
                if (std::ranges::any_of(transparent, [&](const std::vector<NamespaceStep>& seen) {
                        return seen.size() == path.size() && key(seen) == key(path);
                    })) {
                    path.back().transparent = true;
                } else if (step.transparent) {
                    transparent.push_back(path);
                }
            }
        }

        std::string text = "\nnamespace " + name_of(path) + " {";
        if (namespaces_only) {
            for (const std::vector<NamespaceStep>& other : created) {
                if (key(other) != key(path) && sees(path, other)) {
                    text += " using namespace " + name_of(other) + ";";
                }
            }
            if (std::ranges::none_of(created, [&](const auto& other) { return key(other) == key(path); })) {
                created.push_back(path);
            }
        }
        (site.law ? scopes.laws[site.index] : scopes.proofs[site.index]) = std::move(text);
    }
    return scopes;
}

// A directive other than a line marker that C++L cannot keep where it was
// written, refused by name (SPEC.md ERASE-017).
//
// Erasure keeps every directive in the program exactly where it stands. The
// analysis text keeps one where it stands too, or, where it replaces a C++L
// declaration with generated C++, states it after that C++, in the scope the
// declaration stood in. Neither works for a directive inside an expression or
// a statement C++L states -- a clause's parentheses, a parameter list, a ghost
// declaration, a proof statement, a claim, a case split or a validation --
// since the analysis text copies those into generated C++, where a directive
// would land in the middle of an expression. Between the clauses of a
// declaration, and between the statements of a proof, it is kept.
void refuse_misplaced_directives(const TokenStream& stream, const Syntax& syntax,
                                 std::vector<diagnostics::Diagnostic>& diagnostics) {
    struct Region {
        source::ByteSpan span;
        bool whole = false; // refused anywhere inside, not only within parentheses
    };
    std::vector<Region> regions;
    regions.reserve(syntax.laws.size() + syntax.proofs.size() + syntax.refinement_types.size() +
                    syntax.verified_functions.size() + syntax.loops.size() + syntax.ghost_declarations.size() +
                    syntax.path_contradictions.size() + syntax.path_splits.size() + syntax.validations.size());
    for (const LawDeclaration& law : syntax.laws) {
        regions.push_back(Region{law.range.span, false});
    }
    for (const ProofDeclaration& proof : syntax.proofs) {
        regions.push_back(Region{proof.range.span, false});
        for (const ProofStatement& statement : proof.statements) {
            regions.push_back(Region{statement.span, true});
        }
    }
    for (const RefinementType& refinement : syntax.refinement_types) {
        regions.push_back(Region{refinement.range.span, false});
    }
    for (const VerifiedFunction& verified : syntax.verified_functions) {
        regions.push_back(Region{verified.clause_region, false});
    }
    for (const LoopSpecification& loop : syntax.loops) {
        regions.push_back(Region{loop.clause_region, false});
    }
    for (const GhostDeclaration& ghost : syntax.ghost_declarations) {
        regions.push_back(Region{ghost.erased, true});
    }
    for (const PathContradiction& claim : syntax.path_contradictions) {
        regions.push_back(Region{claim.span, true});
    }
    for (const PathCaseSplit& split : syntax.path_splits) {
        regions.push_back(Region{split.span, true});
    }
    for (const ValidationExpression& validation : syntax.validations) {
        regions.push_back(Region{validation.callee, true});
    }

    const std::vector<Token>& tokens = stream.tokens();
    // How many parentheses and brackets opened since `from` are still open at
    // `to`.
    const auto depth = [&tokens](std::size_t from, std::size_t to) {
        int open = 0;
        auto token = std::ranges::lower_bound(tokens, from, {}, [](const Token& each) { return each.span.offset; });
        for (; token != tokens.end() && token->kind != TokenKind::EndOfFile && token->span.offset < to; ++token) {
            if (token->is_punctuator("(") || token->is_punctuator("[")) {
                ++open;
            } else if (token->is_punctuator(")") || token->is_punctuator("]")) {
                --open;
            }
        }
        return open;
    };

    for (const Directive& directive : stream.directives()) {
        if (directive.line_marker) {
            continue;
        }
        const bool misplaced = std::ranges::any_of(regions, [&](const Region& region) {
            if (directive.span.offset < region.span.offset || directive.span.end() > region.span.end()) {
                return false;
            }
            return region.whole || depth(region.span.offset, directive.span.offset) > 0;
        });
        if (!misplaced) {
            continue;
        }
        // The directive takes a line of its own, so the token after it says
        // which line that is.
        source::SourceLocation location;
        const auto next = std::ranges::lower_bound(tokens, directive.span.end(), {},
                                                   [](const Token& each) { return each.span.offset; });
        if (next != tokens.end()) {
            location = stream.location_of(*next);
            const std::string_view between =
                stream.text().substr(directive.span.offset, next->span.offset - directive.span.offset);
            const auto lines = static_cast<std::uint32_t>(std::ranges::count(between, '\n'));
            location.line = location.line > lines ? location.line - lines : location.line;
            location.column = 1;
        }
        diagnostics::Diagnostic diagnostic;
        diagnostic.severity = diagnostics::Severity::Error;
        diagnostic.category = diagnostics::Category::UnsupportedSemantics;
        diagnostic.location = location;
        diagnostic.message = "the directive '" + std::string(stream.spelling(directive.span)) +
                             "' stands inside an expression or a statement C++L states, where it cannot be kept";
        diagnostic.notes.push_back(diagnostics::Note{
            "a directive is kept where it stands between the clauses of a declaration and between the statements of "
            "a proof; move it there, or before or after the construct",
            location});
        diagnostics.push_back(std::move(diagnostic));
    }
}

} // namespace detail::projector

} // namespace cppl::frontend
