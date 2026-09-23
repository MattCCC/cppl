#include "cppl/lsp/semantic_tokens.hpp"

#include "cppl/frontend/syntax.hpp"
#include "cppl/lsp/position.hpp"
#include "cppl/lsp/protocol.hpp"
#include "cppl/source/location.hpp"

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>

namespace cppl::lsp {

namespace {

void collect_keywords(const std::vector<frontend::ProofStatement>& statements, std::vector<source::ByteSpan>& keywords) {
    for (const frontend::ProofStatement& statement : statements) {
        keywords.push_back(statement.keyword);
        for (const frontend::ProofArm& arm : statement.arms) {
            collect_keywords(arm.statements, keywords);
        }
    }
}

} // namespace

std::vector<std::uint32_t> proof_keyword_tokens(const frontend::Syntax& syntax, std::string_view text,
                                                bool path_claims_recognized) {
    std::vector<source::ByteSpan> keywords;
    for (const frontend::ProofDeclaration& proof : syntax.proofs) {
        collect_keywords(proof.statements, keywords);
    }
    if (path_claims_recognized) {
        for (const frontend::PathContradiction& claim : syntax.path_contradictions) {
            keywords.push_back(claim.statement.keyword);
        }
    }
    std::ranges::sort(keywords, {}, &source::ByteSpan::offset);

    const PositionMapper mapper(text);
    std::vector<std::uint32_t> data;
    data.reserve(keywords.size() * 5);
    Position previous;
    for (const source::ByteSpan& keyword : keywords) {
        const Position start = mapper.byte_offset_to_position(keyword.offset);
        const std::uint32_t line_delta = start.line - previous.line;
        data.push_back(line_delta);
        data.push_back(line_delta == 0 ? start.character - previous.character : start.character);
        data.push_back(count_utf16_code_units(text, keyword.offset, keyword.end()));
        data.push_back(0); // kKeywordTokenType
        data.push_back(0); // no modifiers
        previous = start;
    }
    return data;
}

} // namespace cppl::lsp
