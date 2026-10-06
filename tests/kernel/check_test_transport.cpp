// The kernel's checking tests (kernel_check_test): transport along an
// equality.

#include "check_test_support.hpp"
#include "cppl/kernel/check.hpp"
#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/testing/test.hpp"

#include <utility>

using namespace check_test_detail;

namespace {

// forall x : u32. x = 0 -> C(x)
Proposition under_that_premise(Proposition conclusion) {
    return Proposition::for_all(unsigned32(), Proposition::implication(is_zero(), std::move(conclusion)));
}

} // namespace

CPPL_TEST(evidence_is_transported_along_an_equality) {
    const cppl::kernel::Context context = with_identity();

    // forall x : u32. x = 0 -> identity(x) = 0
    //
    // Nothing reduces identity(x) to 0 while x stands for an arbitrary value,
    // so this goal is out of reach of every rule that came before: the premise
    // has to be used to transform the conclusion, not merely to be named.
    const Proposition goal =
        under_that_premise(Proposition::equality(unsigned32(), Term::call(DefId{0}, {bound()}), zero()));

    // C[-] = (identity(-) = 0)
    const Proposition motive = Proposition::equality(unsigned32(), Term::call(DefId{0}, {bound()}), zero());

    const auto result = cppl::kernel::check(
        context, goal,
        ProofTerm::forall_introduction(
            unsigned32(),
            ProofTerm::implication_introduction(
                is_zero(),
                // C[0] is identity(0) = 0, which reflexivity closes; the rule
                // hands back C[x].
                ProofTerm::equality_elimination(unsigned32(), bound(), zero(), motive,
                                                ProofTerm::hypothesis(HypothesisIndex{0}), ProofTerm::reflexivity()))),
        CoreLimits{});

    CPPL_CHECK(result.has_value());
    CPPL_CHECK(result->proposition() == goal);

    // Naming the premise without using it leaves the conclusion unproven.
    const auto without_transport =
        cppl::kernel::check(context, goal,
                            ProofTerm::forall_introduction(
                                unsigned32(), ProofTerm::implication_introduction(is_zero(), ProofTerm::reflexivity())),
                            CoreLimits{});
    CPPL_CHECK(!without_transport.has_value());
    CPPL_CHECK(without_transport.error().kind == RejectionKind::NotDefinitionallyEqual);
}

CPPL_TEST(symmetry_is_this_rule_at_the_context_that_fixes_the_right_side) {
    const cppl::kernel::Context context;

    // forall x : u32. x = 0 -> 0 = x
    //
    // The equality is used in the direction opposite to the one it is stated
    // in, and no rule of its own is needed for that: the context 0 = - has
    // C[0] = (0 = 0), which reflexivity closes.
    const Proposition goal = under_that_premise(Proposition::equality(unsigned32(), zero(), bound()));
    const Proposition motive = Proposition::equality(unsigned32(), zero(), bound());

    const auto result = cppl::kernel::check(
        context, goal,
        ProofTerm::forall_introduction(
            unsigned32(), ProofTerm::implication_introduction(
                              is_zero(), ProofTerm::equality_elimination(unsigned32(), bound(), zero(), motive,
                                                                         ProofTerm::hypothesis(HypothesisIndex{0}),
                                                                         ProofTerm::reflexivity()))),
        CoreLimits{});

    CPPL_CHECK(result.has_value());
    CPPL_CHECK(result->proposition() == goal);
}

CPPL_TEST(a_context_that_does_not_yield_the_goal_is_rejected) {
    const cppl::kernel::Context context = with_identity();

    const Proposition goal =
        under_that_premise(Proposition::equality(unsigned32(), Term::call(DefId{0}, {bound()}), zero()));

    // C[-] = (- = 0) yields x = 0, which is the premise and not the goal.
    const Proposition motive = Proposition::equality(unsigned32(), bound(), zero());

    const auto result = cppl::kernel::check(
        context, goal,
        ProofTerm::forall_introduction(
            unsigned32(), ProofTerm::implication_introduction(
                              is_zero(), ProofTerm::equality_elimination(unsigned32(), bound(), zero(), motive,
                                                                         ProofTerm::hypothesis(HypothesisIndex{0}),
                                                                         ProofTerm::reflexivity()))),
        CoreLimits{});

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::ProofShapeMismatch);
}

CPPL_TEST(the_equality_a_transport_uses_is_itself_checked) {
    const cppl::kernel::Context context = with_identity();

    const Proposition goal = Proposition::equality(unsigned32(), Term::call(DefId{0}, {zero()}), zero());

    // Offered 41 = 0 as the equality to transport along, with nothing that
    // establishes it.
    const Proposition motive = Proposition::equality(unsigned32(), Term::call(DefId{0}, {bound()}), zero());

    const auto result =
        cppl::kernel::check(context, goal,
                            ProofTerm::equality_elimination(unsigned32(), zero(), Term::literal(kUnsigned32, 41),
                                                            motive, ProofTerm::reflexivity(), ProofTerm::reflexivity()),
                            CoreLimits{});

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::NotDefinitionallyEqual);
}

CPPL_TEST(evidence_offered_for_transport_is_itself_checked) {
    const cppl::kernel::Context context = with_identity();

    // The equality holds and the context yields the goal, but what is
    // transported through it does not hold.
    const Proposition goal = under_that_premise(
        Proposition::equality(unsigned32(), Term::call(DefId{0}, {bound()}), Term::literal(kUnsigned32, 41)));
    const Proposition motive =
        Proposition::equality(unsigned32(), Term::call(DefId{0}, {bound()}), Term::literal(kUnsigned32, 41));

    const auto result = cppl::kernel::check(
        context, goal,
        ProofTerm::forall_introduction(
            unsigned32(), ProofTerm::implication_introduction(
                              is_zero(), ProofTerm::equality_elimination(unsigned32(), bound(), zero(), motive,
                                                                         ProofTerm::hypothesis(HypothesisIndex{0}),
                                                                         ProofTerm::reflexivity()))),
        CoreLimits{});

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::NotDefinitionallyEqual);
}
