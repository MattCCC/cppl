#include "cppl/lsp/semantic_tokens.hpp"

#include "cppl/elaboration/elaborate.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/lsp/position.hpp"
#include "cppl/lsp/protocol.hpp"
#include "cppl/source/location.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace cppl::lsp {

namespace {

class Collector {
  public:
    explicit Collector(const std::vector<elaboration::ResolvedName>& resolved) : resolved_(resolved) {}

    void keyword(const source::ByteSpan& span) {
        name(span, TokenType::Keyword, 0);
    }

    void name(const source::ByteSpan& span, TokenType type, std::uint32_t modifiers) {
        if (span.length != 0) {
            tokens_.push_back(SemanticToken{span, type, modifiers});
        }
    }

    void clause(const frontend::Clause& clause) {
        keyword(clause.keyword);
    }

    void statement(const frontend::ProofStatement& statement) {
        keyword(statement.keyword);
        if (statement.kind == frontend::ProofStatementKind::Assume) {
            name(statement.reference_span, TokenType::Variable, kDeclaration | kReadonly);
        } else if (frontend::names_evidence(statement.kind)) {
            // What a name a proof statement uses is, the compile decided.
            const auto found = std::ranges::find_if(resolved_, [&](const elaboration::ResolvedName& known) {
                return known.name == statement.reference && known.at.line == statement.reference_location.line &&
                       known.at.column == statement.reference_location.column;
            });
            if (found != resolved_.end()) {
                if (found->kind == elaboration::ResolvedName::Kind::Assumption) {
                    name(statement.reference_span, TokenType::Variable, kReadonly);
                } else {
                    name(statement.reference_span, TokenType::Function, 0);
                }
            }
        }
        for (const frontend::ProofArm& arm : statement.arms) {
            keyword(arm.omit_keyword);
            keyword(arm.by_keyword);
            for (const frontend::ProofStatement& inner : arm.statements) {
                this->statement(inner);
            }
        }
    }

    [[nodiscard]] std::vector<SemanticToken> collected() && {
        return std::move(tokens_);
    }

  private:
    const std::vector<elaboration::ResolvedName>& resolved_;
    std::vector<SemanticToken> tokens_;
};

} // namespace

std::vector<SemanticToken> cppl_tokens(const frontend::Syntax& syntax, bool path_claims_recognized,
                                       bool path_splits_recognized,
                                       const std::vector<elaboration::ResolvedName>& resolved) {
    Collector collector(resolved);
    for (const frontend::LawDeclaration& law : syntax.laws) {
        collector.keyword(law.trusted_keyword);
        collector.keyword(law.keyword);
        collector.name(law.name_span, TokenType::Function, kDeclaration);
        for (const frontend::Clause& clause : law.clauses) {
            collector.clause(clause);
        }
    }
    for (const frontend::ProofDeclaration& proof : syntax.proofs) {
        if (!proof.inline_law.has_value()) {
            collector.keyword(proof.keyword);
            collector.name(proof.name_span, TokenType::Function, kDeclaration);
            collector.keyword(proof.proves_keyword);
        }
        for (const frontend::ProofStatement& statement : proof.statements) {
            collector.statement(statement);
        }
    }
    for (const std::vector<frontend::VerifiedFunction>* functions :
         {&syntax.verified_functions, &syntax.unchecked_clauses}) {
        for (const frontend::VerifiedFunction& function : *functions) {
            collector.keyword(function.keyword);
            for (const frontend::Clause& clause : function.clauses) {
                collector.clause(clause);
            }
        }
    }
    for (const frontend::PureMarker& marker : syntax.pure_markers) {
        collector.keyword(marker.keyword);
    }
    for (const frontend::LoopSpecification& loop : syntax.loops) {
        for (const frontend::Clause& invariant : loop.invariants) {
            collector.clause(invariant);
        }
        if (const std::optional<frontend::Clause>& measure = loop.decreases; measure.has_value()) {
            collector.clause(*measure);
        }
    }
    for (const frontend::RefinementType& type : syntax.refinement_types) {
        collector.keyword(type.keyword);
        collector.name(type.name_span, TokenType::Type, kDeclaration);
        collector.keyword(type.where_keyword);
    }
    if (path_claims_recognized) {
        for (const frontend::PathContradiction& claim : syntax.path_contradictions) {
            if (!claim.split.has_value()) { // a claim in a split's arm is the split's
                collector.statement(claim.statement);
            }
        }
    }
    if (path_splits_recognized) {
        for (const frontend::PathCaseSplit& split : syntax.path_splits) {
            collector.statement(split.statement);
        }
    }
    return std::move(collector).collected();
}

std::vector<std::uint32_t> encode(std::vector<SemanticToken> tokens, std::string_view text) {
    std::ranges::stable_sort(tokens, {}, [](const SemanticToken& token) { return token.span.offset; });
    const PositionMapper mapper(text);
    std::vector<std::uint32_t> data;
    data.reserve(tokens.size() * 5);
    Position previous;
    std::size_t covered = 0;
    for (const SemanticToken& token : tokens) {
        if ((!data.empty() && token.span.offset < covered) || token.span.end() > text.size()) {
            continue;
        }
        const Position start = mapper.byte_offset_to_position(token.span.offset);
        const std::uint32_t line_delta = start.line - previous.line;
        data.push_back(line_delta);
        data.push_back(line_delta == 0 ? start.character - previous.character : start.character);
        data.push_back(count_utf16_code_units(text, token.span.offset, token.span.end()));
        data.push_back(static_cast<std::uint32_t>(token.type));
        data.push_back(token.modifiers);
        previous = start;
        covered = token.span.end();
    }
    return data;
}

} // namespace cppl::lsp
