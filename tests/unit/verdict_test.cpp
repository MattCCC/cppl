// There is one path to PROVEN: a kernel acceptance for exactly this goal.
//
// The type system already prevents an acceptance from being manufactured
// outside the kernel. This checks the other half: an acceptance obtained for
// one proposition cannot be presented as a verdict on another.

#include "cppl/kernel/check.hpp"
#include "cppl/obligations/status.hpp"
#include "cppl/testing/test.hpp"

namespace {

using cppl::kernel::CoreLimits;
using cppl::kernel::IntType;
using cppl::kernel::ProofTerm;
using cppl::kernel::Proposition;
using cppl::kernel::Signedness;
using cppl::kernel::Term;
using cppl::kernel::Type;
using cppl::kernel::VarIndex;

const IntType kSigned32{32, Signedness::Signed};

Type signed32() {
    return Type::integer(32, Signedness::Signed);
}

Proposition reflexive_goal() {
    return Proposition::for_all(
        signed32(), Proposition::equality(signed32(), Term::variable(VarIndex{0}), Term::variable(VarIndex{0})));
}

cppl::obligations::Obligation obligation_for(Proposition goal) {
    cppl::obligations::Obligation obligation;
    obligation.subject = "some_law";
    obligation.goal = std::move(goal);
    return obligation;
}

} // namespace

CPPL_TEST(a_kernel_acceptance_for_this_goal_yields_proven) {
    const cppl::kernel::Context context;
    const Proposition goal = reflexive_goal();
    const auto accepted = cppl::kernel::check(
        context, goal, ProofTerm::forall_introduction(signed32(), ProofTerm::reflexivity()), CoreLimits{});
    CPPL_CHECK(accepted.has_value());

    const cppl::obligations::Verdict verdict = cppl::obligations::Verdict::proven(*accepted, obligation_for(goal));

    CPPL_CHECK(verdict.is_proven());
    CPPL_CHECK(verdict.status() == cppl::obligations::Status::Proven);
}

CPPL_TEST(an_acceptance_for_another_goal_does_not_prove_this_one) {
    const cppl::kernel::Context context;
    const auto accepted = cppl::kernel::check(
        context, reflexive_goal(), ProofTerm::forall_introduction(signed32(), ProofTerm::reflexivity()), CoreLimits{});
    CPPL_CHECK(accepted.has_value());

    // A different proposition entirely: 1 == 2.
    const Proposition other =
        Proposition::equality(signed32(), Term::literal(kSigned32, 1), Term::literal(kSigned32, 2));

    const cppl::obligations::Verdict verdict = cppl::obligations::Verdict::proven(*accepted, obligation_for(other));

    CPPL_CHECK(!verdict.is_proven());
    CPPL_CHECK(verdict.status() == cppl::obligations::Status::Unresolved);
}

CPPL_TEST(an_unresolved_verdict_carries_its_reason) {
    const cppl::obligations::Verdict verdict = cppl::obligations::Verdict::unresolved("no evidence was produced");

    CPPL_CHECK(!verdict.is_proven());
    CPPL_CHECK_EQ(verdict.reason(), std::string("no evidence was produced"));
    CPPL_CHECK_EQ(cppl::obligations::describe(verdict.status()), std::string("UNRESOLVED"));
}

CPPL_TEST(every_status_is_distinct_in_reporting) {
    using cppl::obligations::describe;
    using cppl::obligations::Status;

    CPPL_CHECK_EQ(describe(Status::Proven), std::string("PROVEN"));
    CPPL_CHECK_EQ(describe(Status::Trusted), std::string("TRUSTED"));
    CPPL_CHECK_EQ(describe(Status::RuntimeChecked), std::string("RUNTIME-CHECKED"));
    CPPL_CHECK_EQ(describe(Status::Unsafe), std::string("UNSAFE"));
    CPPL_CHECK_EQ(describe(Status::Unverified), std::string("UNVERIFIED"));
    CPPL_CHECK_EQ(describe(Status::Unresolved), std::string("UNRESOLVED"));
}
