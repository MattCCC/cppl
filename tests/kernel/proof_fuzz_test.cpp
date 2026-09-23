// Reproducible structural fuzzing of all thirteen proof constructors, with false
// closed goals as the rejection oracle. No frontend or automation is involved.
#include "cppl/kernel/box.hpp"
#include "cppl/kernel/check.hpp"
#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/testing/test.hpp"

#include <cstdint>
#include <utility>
#include <vector>

namespace {
namespace k = cppl::kernel;
const auto type = k::Type::integer(2, k::Signedness::Unsigned);
const auto boolean = k::Type{k::kBoolean};
struct Random {
    std::uint64_t state = 0xd0e84571be39ac62ULL;
    std::uint32_t below(std::uint32_t n) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return static_cast<std::uint32_t>(state % n);
    }
};
k::Term literal(unsigned n) {
    return k::Term::literal(type.integer_type(), n);
}
k::Proposition eq(unsigned a, unsigned b) {
    return k::Proposition::equality(type, literal(a), literal(b));
}
k::Term term(Random& random) {
    switch (random.below(5)) {
        case 0:
            return k::Term::variable(k::VarIndex{random.below(8)});
        case 1:
            return k::Term::call(k::DefId{random.below(8)}, {});
        case 2:
            return k::Term::literal(k::IntType{0, k::Signedness::Unsigned}, 0);
        case 3:
            return k::Term::primitive(static_cast<k::PrimOp>(random.below(255)), type.integer_type(), {});
        default:
            return literal(random.below(8));
    }
}
k::Proposition claim(Random& random) {
    auto goal = eq(random.below(4), random.below(4));
    if (random.below(2))
        goal = k::Proposition::for_all(type, std::move(goal));
    if (random.below(3) == 0)
        goal = k::Proposition::conjunction(eq(random.below(4), random.below(4)), std::move(goal));
    if (random.below(3) == 0)
        goal = k::Proposition::disjunction(eq(random.below(4), random.below(4)), std::move(goal));
    return goal;
}
k::ProofTerm proof(Random& random, unsigned depth) {
    const auto choice = random.below(depth == 0 ? 2 : 13);
    if (choice == 0)
        return k::ProofTerm::reflexivity();
    if (choice == 1)
        return k::ProofTerm::hypothesis(k::HypothesisIndex{random.below(8)});
    if (choice == 2)
        return k::ProofTerm::forall_introduction(type, proof(random, depth - 1));
    if (choice == 3)
        return k::ProofTerm::forall_elimination(claim(random), proof(random, depth - 1), term(random));
    if (choice == 4)
        return k::ProofTerm::implication_introduction(claim(random), proof(random, depth - 1));
    if (choice == 5)
        return k::ProofTerm::implication_elimination(k::Proposition::implication(claim(random), claim(random)),
                                                     proof(random, depth - 1), proof(random, depth - 1));
    if (choice == 6)
        return k::ProofTerm::equality_elimination(type, term(random), term(random), claim(random),
                                                  proof(random, depth - 1), proof(random, depth - 1));
    if (choice == 7)
        return k::ProofTerm::conditional_elimination(type, k::Term::literal(boolean.integer_type(), random.below(3)),
                                                     term(random), term(random), claim(random),
                                                     proof(random, depth - 1), proof(random, depth - 1));
    if (choice == 8)
        return k::ProofTerm::conjunction_introduction(proof(random, depth - 1), proof(random, depth - 1));
    if (choice == 9)
        return k::ProofTerm::conjunction_elimination(claim(random), proof(random, depth - 1), random.below(2) != 0);
    if (choice == 10)
        return k::ProofTerm::disjunction_introduction(proof(random, depth - 1), random.below(2) != 0);
    if (choice == 11)
        return k::ProofTerm::disjunction_elimination(claim(random), proof(random, depth - 1), proof(random, depth - 1),
                                                     proof(random, depth - 1));
    std::vector<k::ArithmeticFact> facts;
    facts.push_back(k::ArithmeticFact{claim(random), k::Box<k::ProofTerm>{proof(random, depth - 1)}});
    return k::ProofTerm::linear_arithmetic(
        std::move(facts), k::ArithmeticCertificate{k::FarkasSum{{{random.below(16), random.below(4)}}}});
}
} // namespace

CPPL_TEST(malformed_proof_trees_cannot_establish_false_closed_goals) {
    Random random;
    for (unsigned sample = 0; sample < 4000; ++sample) {
        auto evidence = proof(random, 3);
        auto goal = eq(sample % 4, (sample + 1) % 4);
        // Exercise recursion under legitimate contexts as well as at the root.
        for (unsigned binder = 0; binder < sample % 4; ++binder) {
            goal = k::Proposition::for_all(type, std::move(goal));
            evidence = k::ProofTerm::forall_introduction(type, std::move(evidence));
        }
        if (sample % 2 == 0) {
            goal = k::Proposition::implication(eq(0, 0), std::move(goal));
            evidence = k::ProofTerm::implication_introduction(eq(0, 0), std::move(evidence));
        }
        k::CoreLimits limits;
        limits.max_term_depth = 8;
        limits.max_normalization_steps = 128;
        limits.max_certificate_nodes = 32;
        CPPL_CHECK(!k::check({}, goal, evidence, limits).has_value());
    }
}
