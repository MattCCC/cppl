// Each rejection below has a valid surrounding derivation. It must fail at
// the named check, so an unrelated malformed premise cannot mask a bypass.
#include "cppl/kernel/check.hpp"
#include "cppl/testing/test.hpp"

namespace {
namespace k = cppl::kernel;
const auto u32 = k::Type::integer(32, k::Signedness::Unsigned);
k::Term lit(std::int64_t n) {
    return k::Term::literal(u32.integer_type(), n);
}
k::Term var(std::uint32_t n) {
    return k::Term::variable(k::VarIndex{n});
}
k::Proposition eq(k::Term a, k::Term b) {
    return k::Proposition::equality(u32, std::move(a), std::move(b));
}
const auto truth = eq(lit(0), lit(0));
const auto falsehood = eq(lit(0), lit(1));
} // namespace

CPPL_TEST(implication_elimination_derives_its_conclusion_independently) {
    const auto implication = k::Proposition::implication(truth, truth);
    const auto proof = k::ProofTerm::implication_elimination(
        implication, k::ProofTerm::implication_introduction(truth, k::ProofTerm::reflexivity()),
        k::ProofTerm::reflexivity());
    CPPL_CHECK(k::check({}, truth, proof, {}).has_value());
    CPPL_CHECK(!k::check({}, falsehood, proof, {}).has_value());
}

CPPL_TEST(implication_elimination_checks_the_claimed_theorem_recursively) {
    const auto claimed = k::Proposition::implication(truth, falsehood);
    // The premise really holds and the claimed conclusion is exactly the goal.
    // Only checking the implication's own evidence exposes the forgery.
    const auto proof = k::ProofTerm::implication_elimination(
        claimed, k::ProofTerm::implication_introduction(truth, k::ProofTerm::reflexivity()),
        k::ProofTerm::reflexivity());
    CPPL_CHECK(!k::check({}, falsehood, proof, {}).has_value());
}

CPPL_TEST(transport_checks_equality_when_both_motive_instances_fit) {
    // Claimed 0=1 transports the valid proof of 1=1 through C[-]=(-=1).
    // C[0] is precisely the false goal, not a mismatching context.
    const auto proof = k::ProofTerm::equality_elimination(u32, lit(0), lit(1), eq(var(0), lit(1)),
                                                          k::ProofTerm::reflexivity(), k::ProofTerm::reflexivity());
    CPPL_CHECK(!k::check({}, falsehood, proof, {}).has_value());
    const auto valid = k::ProofTerm::equality_elimination(u32, lit(1), lit(1), eq(var(0), lit(1)),
                                                          k::ProofTerm::reflexivity(), k::ProofTerm::reflexivity());
    CPPL_CHECK(k::check({}, eq(lit(1), lit(1)), valid, {}).has_value());
}

CPPL_TEST(conditional_elimination_cannot_accept_a_goal_outside_its_motive) {
    const auto condition = k::Term::literal(k::kBoolean, 1);
    const auto proof = k::ProofTerm::conditional_elimination(
        u32, condition, lit(0), lit(0), eq(var(0), lit(0)),
        k::ProofTerm::implication_introduction(k::predicate(condition, true), k::ProofTerm::reflexivity()),
        k::ProofTerm::implication_introduction(k::predicate(condition, false), k::ProofTerm::reflexivity()));
    const auto selected = k::Term::primitive(k::PrimOp::Select, u32.integer_type(), {condition, lit(0), lit(0)});
    CPPL_CHECK(k::check({}, eq(selected, lit(0)), proof, {}).has_value());
    CPPL_CHECK(!k::check({}, eq(selected, lit(1)), proof, {}).has_value());
}
