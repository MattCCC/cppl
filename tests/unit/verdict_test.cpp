// There is one path to PROVEN: a kernel acceptance for exactly this goal.
//
// The type system already prevents an acceptance from being manufactured
// outside the kernel. This checks the other half: an acceptance obtained for
// one proposition cannot be presented as a verdict on another.

#include "cppl/kernel/check.hpp"
#include "cppl/obligations/status.hpp"
#include "cppl/testing/test.hpp"
#include "cppl/vir/ids.hpp"

#include <cstdint>
#include <string>

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

namespace {

// A trusted law stating `1 == 1`, so the tests below can tell it apart from the
// goal and from another premise.
cppl::obligations::TrustedPremise premise(std::uint32_t law, std::int64_t value) {
    cppl::obligations::TrustedPremise premise;
    premise.law = cppl::vir::LawId{law};
    premise.name = "assumed_" + std::to_string(law);
    premise.proposition =
        Proposition::equality(signed32(), Term::literal(kSigned32, value), Term::literal(kSigned32, value));
    return premise;
}

// Evidence for the reflexive goal that supposes `assumed` first, without using it.
cppl::kernel::CheckResult accepted_relative_to(const cppl::obligations::TrustedPremise& assumed) {
    const cppl::kernel::Context context;
    return cppl::kernel::check(
        context, Proposition::implication(assumed.proposition, reflexive_goal()),
        ProofTerm::implication_introduction(assumed.proposition,
                                            ProofTerm::forall_introduction(signed32(), ProofTerm::reflexivity())),
        CoreLimits{});
}

} // namespace

// SPEC: TRUSTED-002, STATUS-002
CPPL_TEST(an_acceptance_under_trusted_premises_proves_the_goal_relative_to_them) {
    const auto assumed = premise(0, 1);
    const auto accepted = accepted_relative_to(assumed);
    CPPL_CHECK(accepted.has_value());

    const auto verdict = cppl::obligations::Verdict::proven(*accepted, obligation_for(reflexive_goal()), {assumed});

    CPPL_CHECK(verdict.is_proven());
    CPPL_CHECK_EQ(verdict.premises().size(), std::size_t{1});
    CPPL_CHECK(verdict.premises().front().law == assumed.law);
}

// A verdict naming fewer premises than the evidence was checked under would
// report a claim as resting on less than it does (TRUST.md TCB-REPORT-002).
CPPL_TEST(an_acceptance_under_premises_is_not_proof_of_the_goal_outright) {
    const auto accepted = accepted_relative_to(premise(0, 1));
    CPPL_CHECK(accepted.has_value());

    const auto verdict = cppl::obligations::Verdict::proven(*accepted, obligation_for(reflexive_goal()));

    CPPL_CHECK(!verdict.is_proven());
    CPPL_CHECK(verdict.premises().empty());
}

CPPL_TEST(an_acceptance_under_one_premise_does_not_name_another) {
    const auto accepted = accepted_relative_to(premise(0, 1));
    CPPL_CHECK(accepted.has_value());

    const auto verdict =
        cppl::obligations::Verdict::proven(*accepted, obligation_for(reflexive_goal()), {premise(1, 2)});

    CPPL_CHECK(!verdict.is_proven());
}

CPPL_TEST(an_outright_acceptance_does_not_become_relative_to_a_premise) {
    const cppl::kernel::Context context;
    const auto accepted = cppl::kernel::check(
        context, reflexive_goal(), ProofTerm::forall_introduction(signed32(), ProofTerm::reflexivity()), CoreLimits{});
    CPPL_CHECK(accepted.has_value());

    const auto verdict =
        cppl::obligations::Verdict::proven(*accepted, obligation_for(reflexive_goal()), {premise(0, 1)});

    CPPL_CHECK(!verdict.is_proven());
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
