#include "cppl/frontend/admissible.hpp"

#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/source/location.hpp"
#include "recognition.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace cppl::frontend {

namespace {

// The index of the first token that starts at or after `offset`.
std::size_t first_token_from(const std::vector<Token>& tokens, std::size_t offset) {
    const auto found =
        std::ranges::lower_bound(tokens, offset, {}, [](const Token& token) { return token.span.offset; });
    return static_cast<std::size_t>(std::distance(tokens.begin(), found));
}

bool holds(const source::ByteSpan& span, std::size_t offset) {
    return offset > span.offset && offset < span.end();
}

// The innermost proof whose body holds `offset`. A body still unterminated
// runs to the end of the text, so a later proof written inside it is the
// innermost.
std::optional<std::size_t> proof_at(const Syntax& draft, std::size_t offset) {
    std::optional<std::size_t> innermost;
    for (std::size_t index = 0; index < draft.proofs.size(); ++index) {
        const ProofDeclaration& proof = draft.proofs[index];
        if (proof.body.length == 0) {
            continue;
        }
        const bool open = proof.completeness == Completeness::UnterminatedBody;
        const bool inside =
            offset > proof.body.offset && (open ? offset <= proof.body.end() : holds(proof.body, offset));
        if (inside && (!innermost.has_value() || proof.body.offset > draft.proofs[*innermost].body.offset)) {
            innermost = index;
        }
    }
    return innermost;
}

// Whether a statement is written to its end. One the recognizer could not read
// and that no `;` or `}` ends yet is the statement still being written.
bool finished(const std::vector<Token>& tokens, const ProofStatement& statement) {
    if (statement.kind != ProofStatementKind::Unread) {
        return true;
    }
    const std::size_t after = first_token_from(tokens, statement.span.end());
    if (after == 0) {
        return false;
    }
    const Token& last = tokens[after - 1];
    return last.is_punctuator(";") || last.is_punctuator("}");
}

// The blocks of a proof body that hold `offset`, outermost first: the body,
// then each arm body inside it. Each is its statements and the byte just
// inside its `{`.
struct Block {
    const std::vector<ProofStatement>* statements = nullptr;
    std::size_t start = 0;
};

std::vector<Block> blocks_at(const ProofDeclaration& proof, std::size_t offset) {
    std::vector<Block> blocks{Block{&proof.statements, proof.body.offset + 1}};
    while (true) {
        const ProofArm* inner = nullptr;
        for (const ProofStatement& statement : *blocks.back().statements) {
            for (const ProofArm& arm : statement.arms) {
                if (arm.body_span.length != 0 && holds(arm.body_span, offset)) {
                    inner = &arm;
                }
            }
        }
        if (inner == nullptr) {
            return blocks;
        }
        blocks.push_back(Block{&inner->statements, inner->body_span.offset + 1});
    }
}

// The declaration's clauses that may be written at `offset`, when `offset`
// follows its parameters or one of its clauses with nothing but space between.
std::optional<std::vector<ClauseKind>> clauses_after(ClauseOwner owner, source::ByteSpan parameters,
                                                     const std::vector<Clause>& clauses,
                                                     std::optional<std::size_t> previous_end, std::size_t offset) {
    if (!previous_end.has_value()) {
        return std::nullopt;
    }
    // A parenthesized list ends with the `)` just past its span.
    bool follows = *previous_end == parameters.end() + 1;
    for (const Clause& clause : clauses) {
        follows = follows || *previous_end == clause.expression.end() + 1;
    }
    if (!follows) {
        return std::nullopt;
    }
    return clauses_admitted(owner, clauses, offset);
}

} // namespace

Admissible admissible_at(const TokenStream& stream, const Syntax& draft, std::size_t offset) {
    const std::vector<Token>& tokens = stream.tokens();
    Admissible here;
    const std::size_t next = first_token_from(tokens, offset);
    const std::optional<std::size_t> previous_end =
        next > 0 ? std::optional<std::size_t>{tokens[next - 1].span.end()} : std::nullopt;

    const auto admit = [&here](ClauseOwner owner, std::vector<ClauseKind> clauses) {
        here.owner = owner;
        here.clauses = std::move(clauses);
    };
    for (const LawDeclaration& law : draft.laws) {
        if (auto clauses = clauses_after(ClauseOwner::Law, law.parameters, law.clauses, previous_end, offset)) {
            admit(ClauseOwner::Law, std::move(*clauses));
            return here;
        }
    }
    for (const ProofDeclaration& proof : draft.proofs) {
        if (proof.inline_law.has_value()) {
            continue; // its Law's clauses are the Law's
        }
        std::vector<Clause> claim;
        if (proof.proves_keyword.length != 0) {
            claim.push_back(
                Clause{ClauseKind::Proves, proof.proves_keyword, proof.proposition, proof.proposition_location});
        }
        if (auto clauses = clauses_after(ClauseOwner::Proof, proof.parameters, claim, previous_end, offset)) {
            admit(ClauseOwner::Proof, std::move(*clauses));
            return here;
        }
    }
    for (const VerifiedFunction& function : draft.verified_functions) {
        if (offset >= function.body_open) {
            continue;
        }
        if (auto clauses = clauses_after(ClauseOwner::VerifiedFunction, function.parameters, function.clauses,
                                         previous_end, offset)) {
            admit(ClauseOwner::VerifiedFunction, std::move(*clauses));
            return here;
        }
    }

    if (const std::optional<std::size_t> proof = proof_at(draft, offset)) {
        here.proof = proof;
        const std::vector<Block> blocks = blocks_at(draft.proofs[*proof], offset);
        std::size_t boundary = blocks.back().start;
        for (const ProofStatement& statement : *blocks.back().statements) {
            if (statement.span.end() <= offset && finished(tokens, statement)) {
                boundary = std::max(boundary, statement.span.end());
            }
        }
        // What is written between the last statement and `offset` is the
        // statement being written: nothing yet, or its first word.
        const std::size_t first = first_token_from(tokens, boundary);
        if (first >= next) {
            here.statement = true;
        } else if (next - first == 1 && tokens[first].kind == TokenKind::Identifier) {
            const std::optional<ProofStatementKind> kind = statement_keyword(tokens[first].text);
            if (kind.has_value() && names_evidence(*kind)) {
                here.evidence_for = kind;
            }
        }
        return here;
    }

    const bool within =
        std::ranges::any_of(draft.laws,
                            [offset](const LawDeclaration& law) { return holds(law.range.span, offset); }) ||
        std::ranges::any_of(draft.proofs,
                            [offset](const ProofDeclaration& proof) { return holds(proof.range.span, offset); });
    here.declaration = !within && next < tokens.size() && detail::declaration_may_begin(tokens, next);
    return here;
}

std::vector<Evidence> evidence_at(const Syntax& draft, std::size_t proof, std::size_t offset) {
    std::vector<Evidence> found;
    if (proof >= draft.proofs.size()) {
        return found;
    }
    const ProofDeclaration& self = draft.proofs[proof];
    const auto offer = [&found](Evidence::Kind kind, const std::string& name) {
        if (!name.empty() &&
            std::ranges::none_of(found, [&name](const Evidence& known) { return known.name == name; })) {
            found.push_back(Evidence{kind, name});
        }
    };
    const std::vector<Block> blocks = blocks_at(self, offset);
    for (const Block& block : std::views::reverse(blocks)) {
        for (const ProofStatement& statement : std::views::reverse(*block.statements)) {
            if (statement.kind == ProofStatementKind::Assume && statement.span.end() <= offset) {
                offer(Evidence::Kind::Assumption, statement.reference);
            }
        }
    }
    for (const LawDeclaration& law : draft.laws) {
        if (law.trusted && law.completeness == Completeness::Whole) {
            offer(Evidence::Kind::TrustedLaw, law.name);
        }
    }
    for (const ProofDeclaration& other : draft.proofs) {
        if (other.name != self.name && other.completeness == Completeness::Whole) {
            offer(Evidence::Kind::Proof, other.name);
        }
    }
    return found;
}

} // namespace cppl::frontend
