// The kernel's checking tests (kernel_check_test): quantifier introduction
// and instantiation.

#include "check_test_support.hpp"
#include "cppl/kernel/check.hpp"
#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/testing/test.hpp"

using namespace check_test_detail;

namespace {

// forall a, b : u32. a + b = a + b
Proposition commuted_sum() {
    return Proposition::for_all(
        unsigned32(), Proposition::for_all(unsigned32(), Proposition::equality(unsigned32(), sum(outer(), bound()),
                                                                               sum(outer(), bound()))));
}

ProofTerm introduce_twice() {
    return ProofTerm::forall_introduction(unsigned32(), introduce(unsigned32()));
}

} // namespace

CPPL_TEST(quantified_evidence_is_instantiated_at_a_term) {
    const cppl::kernel::Context context = with_identity();

    const Proposition general = Proposition::for_all(
        unsigned32(), Proposition::equality(unsigned32(), Term::call(DefId{0}, {bound()}), bound()));

    // The general statement does not match the goal; instantiated at 41 it does.
    const Proposition goal = Proposition::equality(unsigned32(), Term::call(DefId{0}, {Term::literal(kUnsigned32, 41)}),
                                                   Term::literal(kUnsigned32, 41));

    CPPL_CHECK(!cppl::kernel::check(context, goal, introduce(unsigned32()), CoreLimits{}).has_value());

    const auto result = cppl::kernel::check(
        context, goal, ProofTerm::forall_elimination(general, introduce(unsigned32()), Term::literal(kUnsigned32, 41)),
        CoreLimits{});

    CPPL_CHECK(result.has_value());
    CPPL_CHECK(result->proposition() == goal);
}

CPPL_TEST(instantiation_at_the_wrong_type_is_rejected) {
    const cppl::kernel::Context context = with_identity();

    const Proposition general = Proposition::for_all(
        unsigned32(), Proposition::equality(unsigned32(), Term::call(DefId{0}, {bound()}), bound()));
    const Proposition goal = Proposition::equality(unsigned32(), Term::call(DefId{0}, {Term::literal(kUnsigned32, 41)}),
                                                   Term::literal(kUnsigned32, 41));

    const auto result = cppl::kernel::check(
        context, goal, ProofTerm::forall_elimination(general, introduce(unsigned32()), Term::literal(kSigned32, 41)),
        CoreLimits{});

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::ProofShapeMismatch);
}

CPPL_TEST(instantiating_evidence_that_quantifies_over_nothing_is_rejected) {
    const cppl::kernel::Context context;

    const Proposition unquantified =
        Proposition::equality(unsigned32(), Term::literal(kUnsigned32, 1), Term::literal(kUnsigned32, 1));

    const auto result = cppl::kernel::check(
        context, unquantified,
        ProofTerm::forall_elimination(unquantified, ProofTerm::reflexivity(), Term::literal(kUnsigned32, 1)),
        CoreLimits{});

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::ProofShapeMismatch);
}

CPPL_TEST(instantiation_substitutes_the_named_binder_only) {
    const cppl::kernel::Context context;

    // forall b : u32. 7 + b = 7 + b
    const Proposition expected = Proposition::for_all(
        unsigned32(), Proposition::equality(unsigned32(), sum(Term::literal(kUnsigned32, 7), bound()),
                                            sum(Term::literal(kUnsigned32, 7), bound())));

    const auto result = cppl::kernel::check(
        context, expected,
        ProofTerm::forall_elimination(commuted_sum(), introduce_twice(), Term::literal(kUnsigned32, 7)), CoreLimits{});
    CPPL_CHECK(result.has_value());

    // The argument replaces the outer binder, so the other operand must stay
    // the remaining variable.
    const Proposition wrong_position = Proposition::for_all(
        unsigned32(), Proposition::equality(unsigned32(), sum(bound(), Term::literal(kUnsigned32, 7)),
                                            sum(bound(), Term::literal(kUnsigned32, 7))));

    CPPL_CHECK(!cppl::kernel::check(
                    context, wrong_position,
                    ProofTerm::forall_elimination(commuted_sum(), introduce_twice(), Term::literal(kUnsigned32, 7)),
                    CoreLimits{})
                    .has_value());
}

CPPL_TEST(an_argument_is_not_captured_by_a_binder_it_descends_into) {
    const cppl::kernel::Context context;

    // Instantiated at the enclosing variable y, under which a second binder b
    // is still to come. Inside that binder y is one level further out.
    const ProofTerm evidence = ProofTerm::forall_introduction(
        unsigned32(), ProofTerm::forall_elimination(commuted_sum(), introduce_twice(), bound()));

    // forall y, b : u32. y + b = y + b
    const auto result = cppl::kernel::check(context, commuted_sum(), evidence, CoreLimits{});
    CPPL_CHECK(result.has_value());

    // Had the argument not been shifted as it descended, it would have become
    // the inner binder and this captured statement would have been accepted.
    const Proposition captured = Proposition::for_all(
        unsigned32(), Proposition::for_all(unsigned32(), Proposition::equality(unsigned32(), sum(bound(), bound()),
                                                                               sum(bound(), bound()))));

    CPPL_CHECK(!cppl::kernel::check(context, captured, evidence, CoreLimits{}).has_value());
}

CPPL_TEST(evidence_offered_for_elimination_is_itself_checked) {
    const cppl::kernel::Context context = with_identity();

    // forall x : u32. identity(x) = x + 1, which is false.
    const Proposition false_claim =
        Proposition::for_all(unsigned32(), Proposition::equality(unsigned32(), Term::call(DefId{0}, {bound()}),
                                                                 sum(bound(), Term::literal(kUnsigned32, 1))));

    const Proposition goal = Proposition::equality(unsigned32(), Term::call(DefId{0}, {Term::literal(kUnsigned32, 41)}),
                                                   sum(Term::literal(kUnsigned32, 41), Term::literal(kUnsigned32, 1)));

    const auto result = cppl::kernel::check(
        context, goal,
        ProofTerm::forall_elimination(false_claim, introduce(unsigned32()), Term::literal(kUnsigned32, 41)),
        CoreLimits{});

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::NotDefinitionallyEqual);
}
