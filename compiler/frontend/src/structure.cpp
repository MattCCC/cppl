#include "cppl/frontend/structure.hpp"

#include "cppl/frontend/syntax.hpp"
#include "cppl/source/location.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace cppl::frontend {

namespace {

source::ByteSpan through(std::size_t begin, std::size_t end) {
    return source::ByteSpan{begin, end - begin};
}

// A clause from its keyword through the `)` closing its expression.
source::ByteSpan whole(const Clause& clause) {
    return through(clause.keyword.offset, clause.expression.end() + 1);
}

// The spans that hold one offset, as they are met.
class Holding {
  public:
    explicit Holding(std::size_t offset) : offset_(offset) {}

    void add(const source::ByteSpan& span) {
        if (span.length != 0 && span.offset <= offset_ && offset_ <= span.end()) {
            spans_.push_back(span);
        }
    }

    void add(const Clause& clause) {
        add(clause.expression);
        add(whole(clause));
    }

    void add(const ProofStatement& statement) {
        add(statement.span);
        add(statement.proposition);
        for (const ProofArgument& argument : statement.arguments) {
            add(argument.span);
        }
        add(statement.arms_span);
        for (const ProofArm& arm : statement.arms) {
            add(arm.label);
            add(arm.span);
            add(arm.body_span);
            for (const ProofStatement& inner : arm.statements) {
                add(inner);
            }
        }
    }

    [[nodiscard]] std::vector<source::ByteSpan> innermost_first() && {
        std::ranges::stable_sort(spans_, {}, &source::ByteSpan::length);
        const auto [first, last] = std::ranges::unique(spans_);
        spans_.erase(first, last);
        return std::move(spans_);
    }

  private:
    std::size_t offset_;
    std::vector<source::ByteSpan> spans_;
};

void fold_arms(const ProofStatement& statement, std::vector<Block>& blocks) {
    if (statement.arms_span.length != 0) {
        blocks.push_back(Block{Block::Kind::Braces, statement.arms_span});
    }
    for (const ProofArm& arm : statement.arms) {
        if (arm.body_span.length != 0) {
            blocks.push_back(Block{Block::Kind::Braces, arm.body_span});
        }
        for (const ProofStatement& inner : arm.statements) {
            fold_arms(inner, blocks);
        }
    }
}

// The body a Law is proved by where it is declared, when it has one.
const ProofDeclaration* inline_proof(const Syntax& syntax, std::size_t law) {
    const auto found = std::ranges::find_if(syntax.proofs, [law](const ProofDeclaration& proof) {
        return proof.inline_law == law && proof.body.length != 0;
    });
    return found != syntax.proofs.end() ? &*found : nullptr;
}

} // namespace

std::vector<Block> blocks(const Syntax& syntax) {
    std::vector<Block> found;
    for (std::size_t index = 0; index < syntax.laws.size(); ++index) {
        if (inline_proof(syntax, index) == nullptr) {
            found.push_back(Block{Block::Kind::Declaration, syntax.laws[index].range.span});
        }
    }
    for (const ProofDeclaration& proof : syntax.proofs) {
        if (proof.body.length != 0) {
            found.push_back(Block{Block::Kind::Braces, proof.body});
        }
        for (const ProofStatement& statement : proof.statements) {
            fold_arms(statement, found);
        }
    }
    for (const PathCaseSplit& split : syntax.path_splits) {
        fold_arms(split.statement, found);
    }
    for (const RefinementType& type : syntax.refinement_types) {
        found.push_back(Block{Block::Kind::Declaration, type.range.span});
    }
    return found;
}

std::vector<source::ByteSpan> enclosing(const Syntax& syntax, std::size_t offset) {
    Holding holding(offset);
    for (std::size_t index = 0; index < syntax.laws.size(); ++index) {
        const LawDeclaration& law = syntax.laws[index];
        holding.add(law.name_span);
        holding.add(law.parameters);
        for (const Clause& clause : law.clauses) {
            holding.add(clause);
        }
        const ProofDeclaration* proved_here = inline_proof(syntax, index);
        holding.add(proved_here != nullptr ? through(law.range.span.offset, proved_here->body.end()) : law.range.span);
    }
    for (const ProofDeclaration& proof : syntax.proofs) {
        if (!proof.inline_law.has_value()) {
            holding.add(proof.name_span);
            holding.add(proof.parameters);
            if (proof.proves_keyword.length != 0) {
                holding.add(proof.proposition);
                holding.add(through(proof.proves_keyword.offset, proof.proposition.end() + 1));
            }
            holding.add(proof.range.span);
        }
        holding.add(proof.body);
        for (const ProofStatement& statement : proof.statements) {
            holding.add(statement);
        }
    }
    for (const std::vector<VerifiedFunction>* functions : {&syntax.verified_functions, &syntax.unchecked_clauses}) {
        for (const VerifiedFunction& function : *functions) {
            for (const Clause& clause : function.clauses) {
                holding.add(clause);
            }
            holding.add(function.clause_region);
        }
    }
    for (const LoopSpecification& loop : syntax.loops) {
        for (const Clause& invariant : loop.invariants) {
            holding.add(invariant);
        }
        if (const std::optional<Clause>& measure = loop.decreases; measure.has_value()) {
            holding.add(*measure);
        }
        holding.add(loop.clause_region);
    }
    for (const PathContradiction& claim : syntax.path_contradictions) {
        holding.add(claim.statement);
        holding.add(claim.span);
    }
    for (const PathCaseSplit& split : syntax.path_splits) {
        holding.add(split.statement);
        holding.add(split.span);
    }
    for (const RefinementType& type : syntax.refinement_types) {
        holding.add(type.name_span);
        holding.add(type.indices);
        holding.add(type.base);
        holding.add(type.predicate);
        holding.add(type.range.span);
    }
    return std::move(holding).innermost_first();
}

} // namespace cppl::frontend
