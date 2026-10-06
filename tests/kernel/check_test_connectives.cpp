// The kernel's checking tests (kernel_check_test): conjunction and
// disjunction, introduced, eliminated and taken cases on.

#include "check_test_support.hpp"
#include "cppl/kernel/check.hpp"
#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/testing/test.hpp"

using namespace check_test_detail;

namespace {

// 0 = 0, which reflexivity establishes.
Proposition settled() {
    return Proposition::equality(unsigned32(), zero(), zero());
}

// 0 = 1, which nothing establishes.
Proposition unsettled() {
    return Proposition::equality(unsigned32(), zero(), Term::literal(kUnsigned32, 1));
}

} // namespace

CPPL_TEST(a_conjunction_is_established_by_evidence_for_each_side) {
    const cppl::kernel::Context context;
    const Proposition goal = Proposition::conjunction(settled(), settled());

    const auto result = cppl::kernel::check(
        context, goal, ProofTerm::conjunction_introduction(ProofTerm::reflexivity(), ProofTerm::reflexivity()),
        CoreLimits{});

    CPPL_CHECK(result.has_value());
    CPPL_CHECK(result->proposition() == goal);
}

CPPL_TEST(a_conjunction_with_one_side_unestablished_is_rejected) {
    const cppl::kernel::Context context;
    for (const bool second : {false, true}) {
        const Proposition goal = second ? Proposition::conjunction(settled(), unsettled())
                                        : Proposition::conjunction(unsettled(), settled());

        const auto result = cppl::kernel::check(
            context, goal, ProofTerm::conjunction_introduction(ProofTerm::reflexivity(), ProofTerm::reflexivity()),
            CoreLimits{});

        CPPL_CHECK(!result.has_value());
        CPPL_CHECK(result.error().kind == RejectionKind::NotDefinitionallyEqual);
    }
}

CPPL_TEST(a_conjunction_goal_requires_conjunction_introduction) {
    const cppl::kernel::Context context;
    const Proposition goal = Proposition::conjunction(settled(), settled());

    // Reflexivity establishes each side on its own, and establishes nothing
    // about the conjunction of them.
    const auto result = cppl::kernel::check(context, goal, ProofTerm::reflexivity(), CoreLimits{});

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::ProofShapeMismatch);
}

CPPL_TEST(either_side_of_a_conjunction_follows_from_it) {
    const cppl::kernel::Context context;
    // x = 0 && 0 = 0, supposed, gives each of its sides.
    const Proposition conjunction = Proposition::conjunction(is_zero(), settled());
    for (const bool right : {false, true}) {
        const Proposition side = right ? settled() : is_zero();
        const Proposition goal = Proposition::for_all(unsigned32(), Proposition::implication(conjunction, side));

        const auto result = cppl::kernel::check(
            context, goal,
            ProofTerm::forall_introduction(
                unsigned32(), ProofTerm::implication_introduction(
                                  conjunction, ProofTerm::conjunction_elimination(
                                                   conjunction, ProofTerm::hypothesis(HypothesisIndex{0}), right))),
            CoreLimits{});

        CPPL_CHECK(result.has_value());
    }
}

CPPL_TEST(a_side_taken_from_a_conjunction_must_be_the_goal) {
    const cppl::kernel::Context context;
    const Proposition conjunction = Proposition::conjunction(is_zero(), settled());
    // The left side is supposed and the right side is claimed.
    const Proposition goal = Proposition::for_all(unsigned32(), Proposition::implication(conjunction, is_zero()));

    const auto result = cppl::kernel::check(
        context, goal,
        ProofTerm::forall_introduction(
            unsigned32(), ProofTerm::implication_introduction(
                              conjunction, ProofTerm::conjunction_elimination(
                                               conjunction, ProofTerm::hypothesis(HypothesisIndex{0}), true))),
        CoreLimits{});

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::ProofShapeMismatch);
}

CPPL_TEST(a_conjunction_cannot_be_invented_to_eliminate_from) {
    const cppl::kernel::Context context;
    // Nothing establishes the conjunction this elimination restates, so the
    // side it takes is not available either.
    const Proposition conjunction = Proposition::conjunction(settled(), unsettled());

    const auto result = cppl::kernel::check(
        context, settled(),
        ProofTerm::conjunction_elimination(
            conjunction, ProofTerm::conjunction_introduction(ProofTerm::reflexivity(), ProofTerm::reflexivity()),
            false),
        CoreLimits{});

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::NotDefinitionallyEqual);
}

CPPL_TEST(a_side_cannot_be_taken_from_evidence_that_is_not_a_conjunction) {
    const cppl::kernel::Context context;

    const auto result = cppl::kernel::check(
        context, settled(), ProofTerm::conjunction_elimination(settled(), ProofTerm::reflexivity(), false),
        CoreLimits{});

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::ProofShapeMismatch);
}

CPPL_TEST(a_malformed_conjunction_is_not_a_proposition) {
    const cppl::kernel::Context context;
    // One side is an equality between operands of different widths.
    const Proposition goal =
        Proposition::conjunction(settled(), Proposition::equality(unsigned32(), Term::literal(kUnsigned8, 0), zero()));

    const auto result = cppl::kernel::check(
        context, goal, ProofTerm::conjunction_introduction(ProofTerm::reflexivity(), ProofTerm::reflexivity()),
        CoreLimits{});

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::MalformedProposition);
}

CPPL_TEST(a_conjunction_carries_its_binder_into_both_sides) {
    const cppl::kernel::Context context;
    // forall x : u32. (x = x && x = x), which holds of the binder on both sides.
    const Proposition side = Proposition::equality(unsigned32(), bound(), bound());
    const Proposition goal = Proposition::for_all(unsigned32(), Proposition::conjunction(side, side));

    const auto result = cppl::kernel::check(
        context, goal,
        ProofTerm::forall_introduction(
            unsigned32(), ProofTerm::conjunction_introduction(ProofTerm::reflexivity(), ProofTerm::reflexivity())),
        CoreLimits{});

    CPPL_CHECK(result.has_value());

    // The same conjunction stated of a variable with no binder over it is not a
    // proposition at all.
    const auto unbound = cppl::kernel::check(
        context, Proposition::conjunction(side, side),
        ProofTerm::conjunction_introduction(ProofTerm::reflexivity(), ProofTerm::reflexivity()), CoreLimits{});

    CPPL_CHECK(!unbound.has_value());
    CPPL_CHECK(unbound.error().kind == RejectionKind::MalformedProposition);
}

CPPL_TEST(a_context_whose_hole_stands_at_the_wrong_type_is_rejected) {
    const cppl::kernel::Context context;

    const Proposition goal =
        Proposition::equality(signed32(), Term::literal(kSigned32, 0), Term::literal(kSigned32, 0));

    // The equality is at u32, so the hole stands for a u32; the context uses it
    // where an i32 is required.
    const Proposition motive = Proposition::equality(signed32(), bound(), Term::literal(kSigned32, 0));

    const auto result =
        cppl::kernel::check(context, goal,
                            ProofTerm::equality_elimination(unsigned32(), zero(), zero(), motive,
                                                            ProofTerm::reflexivity(), ProofTerm::reflexivity()),
                            CoreLimits{});

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::MalformedProposition);
}

CPPL_TEST(a_disjunction_is_established_by_evidence_for_either_side) {
    const cppl::kernel::Context context;
    for (const bool right : {false, true}) {
        // The established side is the one the evidence selects; the other side is
        // one nothing establishes, so a rule that ignored the selection would
        // show up here.
        const Proposition goal =
            right ? Proposition::disjunction(unsettled(), settled()) : Proposition::disjunction(settled(), unsettled());

        const auto result = cppl::kernel::check(
            context, goal, ProofTerm::disjunction_introduction(ProofTerm::reflexivity(), right), CoreLimits{});

        CPPL_CHECK(result.has_value());
        CPPL_CHECK(result->proposition() == goal);
    }
}

CPPL_TEST(a_disjunction_whose_selected_side_is_unestablished_is_rejected) {
    const cppl::kernel::Context context;
    for (const bool right : {false, true}) {
        // The other side holds, which establishes nothing about this selection.
        const Proposition goal =
            right ? Proposition::disjunction(settled(), unsettled()) : Proposition::disjunction(unsettled(), settled());

        const auto result = cppl::kernel::check(
            context, goal, ProofTerm::disjunction_introduction(ProofTerm::reflexivity(), right), CoreLimits{});

        CPPL_CHECK(!result.has_value());
        CPPL_CHECK(result.error().kind == RejectionKind::NotDefinitionallyEqual);
    }
}

CPPL_TEST(a_disjunction_goal_requires_disjunction_introduction_or_elimination) {
    const cppl::kernel::Context context;
    const Proposition goal = Proposition::disjunction(settled(), settled());

    // Reflexivity establishes each side on its own and says nothing about the
    // disjunction of them.
    const auto result = cppl::kernel::check(context, goal, ProofTerm::reflexivity(), CoreLimits{});

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::ProofShapeMismatch);
}

CPPL_TEST(a_disjunction_gives_what_both_of_its_cases_give) {
    const cppl::kernel::Context context;
    // forall x : u32. (x = 0 || 0 = 0) -> 0 = 0, taken by cases. Each case is
    // an implication from its own side, and both reach the same conclusion.
    const Proposition disjunction = Proposition::disjunction(is_zero(), settled());
    const Proposition goal = Proposition::for_all(unsigned32(), Proposition::implication(disjunction, settled()));

    const auto result = cppl::kernel::check(
        context, goal,
        ProofTerm::forall_introduction(
            unsigned32(),
            ProofTerm::implication_introduction(
                disjunction, ProofTerm::disjunction_elimination(
                                 disjunction, ProofTerm::hypothesis(HypothesisIndex{0}),
                                 ProofTerm::implication_introduction(is_zero(), ProofTerm::reflexivity()),
                                 ProofTerm::implication_introduction(settled(), ProofTerm::reflexivity())))),
        CoreLimits{});

    CPPL_CHECK(result.has_value());
}

CPPL_TEST(neither_side_of_a_disjunction_follows_from_it) {
    const cppl::kernel::Context context;
    // Supposing x = 0 || 0 = 0 does not establish x = 0: the case that supposes
    // the settled side cannot reach it.
    const Proposition disjunction = Proposition::disjunction(is_zero(), settled());
    const Proposition goal = Proposition::for_all(unsigned32(), Proposition::implication(disjunction, is_zero()));

    const auto result = cppl::kernel::check(
        context, goal,
        ProofTerm::forall_introduction(
            unsigned32(),
            ProofTerm::implication_introduction(
                disjunction,
                ProofTerm::disjunction_elimination(
                    disjunction, ProofTerm::hypothesis(HypothesisIndex{0}),
                    ProofTerm::implication_introduction(is_zero(), ProofTerm::hypothesis(HypothesisIndex{0})),
                    ProofTerm::implication_introduction(settled(), ProofTerm::hypothesis(HypothesisIndex{0}))))),
        CoreLimits{});

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::ProofShapeMismatch);
}

CPPL_TEST(a_case_that_covers_the_wrong_side_is_rejected) {
    const cppl::kernel::Context context;
    // Both cases suppose the left side, so the right side is never covered.
    const Proposition disjunction = Proposition::disjunction(is_zero(), settled());
    const Proposition goal = Proposition::for_all(unsigned32(), Proposition::implication(disjunction, settled()));

    const auto result = cppl::kernel::check(
        context, goal,
        ProofTerm::forall_introduction(
            unsigned32(),
            ProofTerm::implication_introduction(
                disjunction, ProofTerm::disjunction_elimination(
                                 disjunction, ProofTerm::hypothesis(HypothesisIndex{0}),
                                 ProofTerm::implication_introduction(is_zero(), ProofTerm::reflexivity()),
                                 ProofTerm::implication_introduction(is_zero(), ProofTerm::reflexivity())))),
        CoreLimits{});

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::ProofShapeMismatch);
}

CPPL_TEST(a_disjunction_cannot_be_invented_to_take_cases_on) {
    const cppl::kernel::Context context;
    // Nothing establishes this disjunction, so neither case can be reached
    // through it however the cases are written.
    const Proposition disjunction = Proposition::disjunction(unsettled(), unsettled());

    const auto result =
        cppl::kernel::check(context, settled(),
                            ProofTerm::disjunction_elimination(
                                disjunction, ProofTerm::disjunction_introduction(ProofTerm::reflexivity(), false),
                                ProofTerm::implication_introduction(unsettled(), ProofTerm::reflexivity()),
                                ProofTerm::implication_introduction(unsettled(), ProofTerm::reflexivity())),
                            CoreLimits{});

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::NotDefinitionallyEqual);
}

CPPL_TEST(cases_cannot_be_taken_on_evidence_that_is_not_a_disjunction) {
    const cppl::kernel::Context context;

    const auto result = cppl::kernel::check(
        context, settled(),
        ProofTerm::disjunction_elimination(settled(), ProofTerm::reflexivity(),
                                           ProofTerm::implication_introduction(settled(), ProofTerm::reflexivity()),
                                           ProofTerm::implication_introduction(settled(), ProofTerm::reflexivity())),
        CoreLimits{});

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::ProofShapeMismatch);
}

CPPL_TEST(a_malformed_disjunction_is_not_a_proposition) {
    const cppl::kernel::Context context;
    // One side is an equality between operands of different widths.
    const Proposition goal =
        Proposition::disjunction(settled(), Proposition::equality(unsigned32(), Term::literal(kUnsigned8, 0), zero()));

    const auto result = cppl::kernel::check(
        context, goal, ProofTerm::disjunction_introduction(ProofTerm::reflexivity(), false), CoreLimits{});

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::MalformedProposition);
}

CPPL_TEST(a_disjunction_carries_its_binder_into_both_sides) {
    const cppl::kernel::Context context;
    const Proposition side = Proposition::equality(unsigned32(), bound(), bound());
    const Proposition goal = Proposition::for_all(unsigned32(), Proposition::disjunction(side, side));

    const auto result =
        cppl::kernel::check(context, goal,
                            ProofTerm::forall_introduction(
                                unsigned32(), ProofTerm::disjunction_introduction(ProofTerm::reflexivity(), true)),
                            CoreLimits{});

    CPPL_CHECK(result.has_value());

    // The same disjunction stated of a variable with no binder over it is not a
    // proposition at all.
    const auto unbound =
        cppl::kernel::check(context, Proposition::disjunction(side, side),
                            ProofTerm::disjunction_introduction(ProofTerm::reflexivity(), true), CoreLimits{});

    CPPL_CHECK(!unbound.has_value());
    CPPL_CHECK(unbound.error().kind == RejectionKind::MalformedProposition);
}

CPPL_TEST(a_case_analysis_does_not_leak_its_side_into_the_other_case) {
    const cppl::kernel::Context context;
    // forall x : u32. (x = 0 || x = 0) -> x = 0. Each case supposes its own side
    // and closes with it; the hypothesis a case names is its own, so the
    // hypothesis index must reach past the disjunction that is still supposed.
    const Proposition disjunction = Proposition::disjunction(is_zero(), is_zero());
    const Proposition goal = Proposition::for_all(unsigned32(), Proposition::implication(disjunction, is_zero()));

    const auto result = cppl::kernel::check(
        context, goal,
        ProofTerm::forall_introduction(
            unsigned32(),
            ProofTerm::implication_introduction(
                disjunction,
                ProofTerm::disjunction_elimination(
                    disjunction, ProofTerm::hypothesis(HypothesisIndex{0}),
                    ProofTerm::implication_introduction(is_zero(), ProofTerm::hypothesis(HypothesisIndex{0})),
                    ProofTerm::implication_introduction(is_zero(), ProofTerm::hypothesis(HypothesisIndex{0}))))),
        CoreLimits{});

    CPPL_CHECK(result.has_value());

    // Naming the outer disjunction instead of the case's own side is refused.
    const auto outer = cppl::kernel::check(
        context, goal,
        ProofTerm::forall_introduction(
            unsigned32(),
            ProofTerm::implication_introduction(
                disjunction,
                ProofTerm::disjunction_elimination(
                    disjunction, ProofTerm::hypothesis(HypothesisIndex{0}),
                    ProofTerm::implication_introduction(is_zero(), ProofTerm::hypothesis(HypothesisIndex{1})),
                    ProofTerm::implication_introduction(is_zero(), ProofTerm::hypothesis(HypothesisIndex{1}))))),
        CoreLimits{});

    CPPL_CHECK(!outer.has_value());
    CPPL_CHECK(outer.error().kind == RejectionKind::ProofShapeMismatch);
}
