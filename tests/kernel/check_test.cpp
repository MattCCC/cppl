// Kernel acceptance and rejection rules.
//
// Every rule is exercised with valid evidence, invalid evidence and malformed
// evidence. These tests call the real kernel API; nothing here is mocked, and
// no other component is involved (ARCHITECTURE.md 63).

#include "cppl/kernel/check.hpp"
#include "cppl/kernel/context.hpp"
#include "cppl/testing/test.hpp"

namespace {

using cppl::kernel::Acceptance;
using cppl::kernel::CoreErrorKind;
using cppl::kernel::CoreLimits;
using cppl::kernel::DefId;
using cppl::kernel::Definition;
using cppl::kernel::IntType;
using cppl::kernel::PrimOp;
using cppl::kernel::ProofTerm;
using cppl::kernel::Proposition;
using cppl::kernel::RejectionKind;
using cppl::kernel::Signedness;
using cppl::kernel::Term;
using cppl::kernel::Type;
using cppl::kernel::VarIndex;

const IntType kSigned32{32, Signedness::Signed};
const IntType kUnsigned32{32, Signedness::Unsigned};
const IntType kUnsigned8{8, Signedness::Unsigned};
const IntType kSigned8{8, Signedness::Signed};

Type signed32() {
    return Type::integer(32, Signedness::Signed);
}

Type unsigned32() {
    return Type::integer(32, Signedness::Unsigned);
}

Term bound() {
    return Term::variable(VarIndex{0});
}

ProofTerm introduce(Type binder) {
    return ProofTerm::forall_introduction(std::move(binder), ProofTerm::reflexivity());
}

}  // namespace

CPPL_TEST(reflexivity_proves_a_term_equal_to_itself) {
    const cppl::kernel::Context context;
    const Proposition goal =
        Proposition::for_all(signed32(), Proposition::equality(signed32(), bound(), bound()));

    const auto result = cppl::kernel::check(context, goal, introduce(signed32()), CoreLimits{});

    CPPL_CHECK(result.has_value());
    CPPL_CHECK(result->proposition() == goal);
}

CPPL_TEST(unfolding_a_definition_closes_the_goal) {
    cppl::kernel::Context context;
    Definition identity;
    identity.id = DefId{0};
    identity.name = "identity";
    identity.parameters = {signed32()};
    identity.result = signed32();
    identity.body = Term::variable(cppl::kernel::parameter_reference(1, 0));
    CPPL_CHECK(context.define(identity).has_value());

    const Proposition goal = Proposition::for_all(
        signed32(),
        Proposition::equality(signed32(), Term::call(DefId{0}, {bound()}), bound()));

    const auto result = cppl::kernel::check(context, goal, introduce(signed32()), CoreLimits{});
    CPPL_CHECK(result.has_value());
}

CPPL_TEST(reflexivity_does_not_prove_a_false_equality) {
    const cppl::kernel::Context context;
    // forall x : u32, Eq(x, x + 1)
    const Proposition goal = Proposition::for_all(
        unsigned32(),
        Proposition::equality(unsigned32(), bound(),
                              Term::primitive(PrimOp::AddWrap, kUnsigned32,
                                              {bound(), Term::literal(kUnsigned32, 1)})));

    const auto result = cppl::kernel::check(context, goal, introduce(unsigned32()), CoreLimits{});

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::NotDefinitionallyEqual);
}

CPPL_TEST(distinct_literals_are_not_definitionally_equal) {
    const cppl::kernel::Context context;
    const Proposition goal = Proposition::equality(signed32(), Term::literal(kSigned32, 1),
                                                   Term::literal(kSigned32, 2));

    const auto result =
        cppl::kernel::check(context, goal, ProofTerm::reflexivity(), CoreLimits{});

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::NotDefinitionallyEqual);
}

CPPL_TEST(a_quantified_goal_is_not_closed_by_bare_reflexivity) {
    const cppl::kernel::Context context;
    const Proposition goal =
        Proposition::for_all(signed32(), Proposition::equality(signed32(), bound(), bound()));

    const auto result =
        cppl::kernel::check(context, goal, ProofTerm::reflexivity(), CoreLimits{});

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::ProofShapeMismatch);
}

CPPL_TEST(an_equality_goal_is_not_closed_by_quantifier_introduction) {
    const cppl::kernel::Context context;
    const Proposition goal = Proposition::equality(signed32(), Term::literal(kSigned32, 1),
                                                   Term::literal(kSigned32, 1));

    const auto result = cppl::kernel::check(context, goal, introduce(signed32()), CoreLimits{});

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::ProofShapeMismatch);
}

CPPL_TEST(evidence_must_introduce_the_binder_the_goal_quantifies_over) {
    const cppl::kernel::Context context;
    const Proposition goal =
        Proposition::for_all(signed32(), Proposition::equality(signed32(), bound(), bound()));

    const auto result = cppl::kernel::check(context, goal, introduce(unsigned32()), CoreLimits{});

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::ProofShapeMismatch);
}

CPPL_TEST(a_goal_naming_an_unknown_definition_is_malformed) {
    const cppl::kernel::Context context;
    const Proposition goal = Proposition::for_all(
        signed32(),
        Proposition::equality(signed32(), Term::call(DefId{9}, {bound()}), bound()));

    const auto result = cppl::kernel::check(context, goal, introduce(signed32()), CoreLimits{});

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::MalformedProposition);
}

CPPL_TEST(a_goal_with_a_free_variable_is_malformed) {
    const cppl::kernel::Context context;
    const Proposition goal = Proposition::equality(signed32(), bound(), bound());

    const auto result =
        cppl::kernel::check(context, goal, ProofTerm::reflexivity(), CoreLimits{});

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::MalformedProposition);
}

CPPL_TEST(a_literal_outside_its_type_is_malformed) {
    const cppl::kernel::Context context;
    const Type small = Type::integer(8, Signedness::Signed);
    const Proposition goal = Proposition::equality(small, Term::literal(kSigned8, 1000),
                                                   Term::literal(kSigned8, 1000));

    const auto result =
        cppl::kernel::check(context, goal, ProofTerm::reflexivity(), CoreLimits{});

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::MalformedProposition);
}

CPPL_TEST(an_equality_stated_at_the_wrong_type_is_malformed) {
    const cppl::kernel::Context context;
    const Proposition goal = Proposition::equality(signed32(), Term::literal(kUnsigned32, 1),
                                                   Term::literal(kUnsigned32, 1));

    const auto result =
        cppl::kernel::check(context, goal, ProofTerm::reflexivity(), CoreLimits{});

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::MalformedProposition);
}

CPPL_TEST(the_context_rejects_a_duplicate_definition) {
    cppl::kernel::Context context;
    Definition definition;
    definition.id = DefId{0};
    definition.name = "zero";
    definition.result = signed32();
    definition.body = Term::literal(kSigned32, 0);

    CPPL_CHECK(context.define(definition).has_value());
    const auto again = context.define(definition);
    CPPL_CHECK(!again.has_value());
    CPPL_CHECK(again.error().kind == CoreErrorKind::DuplicateDefinition);
}

CPPL_TEST(the_context_rejects_a_recursive_definition) {
    cppl::kernel::Context context;
    Definition recursive;
    recursive.id = DefId{0};
    recursive.name = "loop";
    recursive.parameters = {signed32()};
    recursive.result = signed32();
    recursive.body = Term::call(DefId{0}, {Term::variable(VarIndex{0})});

    const auto admitted = context.define(recursive);
    CPPL_CHECK(!admitted.has_value());
    CPPL_CHECK(admitted.error().kind == CoreErrorKind::UnknownDefinition);
}

CPPL_TEST(the_context_rejects_a_body_of_the_wrong_type) {
    cppl::kernel::Context context;
    Definition mistyped;
    mistyped.id = DefId{0};
    mistyped.name = "mistyped";
    mistyped.result = signed32();
    mistyped.body = Term::literal(kUnsigned32, 0);

    const auto admitted = context.define(mistyped);
    CPPL_CHECK(!admitted.has_value());
    CPPL_CHECK(admitted.error().kind == CoreErrorKind::TypeMismatch);
}

CPPL_TEST(the_context_rejects_a_body_referring_to_a_missing_parameter) {
    cppl::kernel::Context context;
    Definition unbound;
    unbound.id = DefId{0};
    unbound.name = "unbound";
    unbound.result = signed32();
    unbound.body = Term::variable(VarIndex{0});

    const auto admitted = context.define(unbound);
    CPPL_CHECK(!admitted.has_value());
    CPPL_CHECK(admitted.error().kind == CoreErrorKind::VariableOutOfScope);
}

CPPL_TEST(wrapping_addition_folds_at_the_type_width) {
    const cppl::kernel::Context context;
    const Term sum = Term::primitive(PrimOp::AddWrap, kUnsigned8,
                                     {Term::literal(kUnsigned8, 255), Term::literal(kUnsigned8, 1)});

    const auto normalized = cppl::kernel::normalize(context, sum, CoreLimits{});

    CPPL_CHECK(normalized.has_value());
    CPPL_CHECK(*normalized == Term::literal(kUnsigned8, 0));
}

CPPL_TEST(normalization_is_deterministic) {
    cppl::kernel::Context context;
    Definition add_one;
    add_one.id = DefId{0};
    add_one.name = "add_one";
    add_one.parameters = {unsigned32()};
    add_one.result = unsigned32();
    add_one.body = Term::primitive(PrimOp::AddWrap, kUnsigned32,
                                   {Term::variable(cppl::kernel::parameter_reference(1, 0)),
                                    Term::literal(kUnsigned32, 1)});
    CPPL_CHECK(context.define(add_one).has_value());

    const Term call = Term::call(DefId{0}, {Term::literal(kUnsigned32, 41)});
    const auto first = cppl::kernel::normalize(context, call, CoreLimits{});
    const auto second = cppl::kernel::normalize(context, call, CoreLimits{});

    CPPL_CHECK(first.has_value());
    CPPL_CHECK(second.has_value());
    CPPL_CHECK(*first == *second);
    CPPL_CHECK(*first == Term::literal(kUnsigned32, 42));
}

CPPL_TEST(an_arithmetic_law_is_proven_when_both_sides_reduce_alike) {
    cppl::kernel::Context context;
    Definition add_one;
    add_one.id = DefId{0};
    add_one.name = "add_one";
    add_one.parameters = {unsigned32()};
    add_one.result = unsigned32();
    add_one.body = Term::primitive(PrimOp::AddWrap, kUnsigned32,
                                   {Term::variable(cppl::kernel::parameter_reference(1, 0)),
                                    Term::literal(kUnsigned32, 1)});
    CPPL_CHECK(context.define(add_one).has_value());

    const Proposition goal = Proposition::for_all(
        unsigned32(),
        Proposition::equality(unsigned32(), Term::call(DefId{0}, {bound()}),
                              Term::primitive(PrimOp::AddWrap, kUnsigned32,
                                              {bound(), Term::literal(kUnsigned32, 1)})));

    const auto result = cppl::kernel::check(context, goal, introduce(unsigned32()), CoreLimits{});
    CPPL_CHECK(result.has_value());
}

CPPL_TEST(an_exhausted_normalization_budget_rejects_rather_than_accepts) {
    cppl::kernel::Context context;
    Definition first;
    first.id = DefId{0};
    first.name = "first";
    first.parameters = {unsigned32()};
    first.result = unsigned32();
    first.body = Term::variable(cppl::kernel::parameter_reference(1, 0));
    CPPL_CHECK(context.define(first).has_value());

    Definition second;
    second.id = DefId{1};
    second.name = "second";
    second.parameters = {unsigned32()};
    second.result = unsigned32();
    second.body = Term::call(DefId{0}, {Term::variable(cppl::kernel::parameter_reference(1, 0))});
    CPPL_CHECK(context.define(second).has_value());

    const Proposition goal = Proposition::for_all(
        unsigned32(),
        Proposition::equality(unsigned32(), Term::call(DefId{1}, {bound()}), bound()));

    CoreLimits exhausted;
    exhausted.max_normalization_steps = 0;

    const auto result = cppl::kernel::check(context, goal, introduce(unsigned32()), exhausted);
    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::CoreFailure);
}

CPPL_TEST(a_term_nested_beyond_the_depth_limit_is_rejected) {
    const cppl::kernel::Context context;

    Term nested = Term::literal(kUnsigned32, 0);
    for (int depth = 0; depth < 40; ++depth) {
        nested = Term::primitive(PrimOp::AddWrap, kUnsigned32,
                                 {nested, Term::literal(kUnsigned32, 0)});
    }

    CoreLimits shallow;
    shallow.max_term_depth = 4;

    const Proposition goal = Proposition::equality(unsigned32(), nested, nested);
    const auto result =
        cppl::kernel::check(context, goal, ProofTerm::reflexivity(), shallow);

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::MalformedProposition);
}

namespace {

// `identity(x) = x` over unsigned 32-bit integers.
cppl::kernel::Context with_identity() {
    cppl::kernel::Context context;
    Definition identity;
    identity.id = DefId{0};
    identity.name = "identity";
    identity.parameters = {unsigned32()};
    identity.result = unsigned32();
    identity.body = Term::variable(cppl::kernel::parameter_reference(1, 0));
    CPPL_CHECK(context.define(std::move(identity)).has_value());
    return context;
}

Term outer() {
    return Term::variable(VarIndex{1});
}

Term sum(Term lhs, Term rhs) {
    return Term::primitive(PrimOp::AddWrap, kUnsigned32, {std::move(lhs), std::move(rhs)});
}

// forall a, b : u32. a + b = a + b
Proposition commuted_sum() {
    return Proposition::for_all(
        unsigned32(),
        Proposition::for_all(unsigned32(), Proposition::equality(unsigned32(),
                                                                 sum(outer(), bound()),
                                                                 sum(outer(), bound()))));
}

ProofTerm introduce_twice() {
    return ProofTerm::forall_introduction(unsigned32(), introduce(unsigned32()));
}

}  // namespace

CPPL_TEST(quantified_evidence_is_instantiated_at_a_term) {
    const cppl::kernel::Context context = with_identity();

    const Proposition general = Proposition::for_all(
        unsigned32(),
        Proposition::equality(unsigned32(), Term::call(DefId{0}, {bound()}), bound()));

    // The general statement does not match the goal; instantiated at 41 it does.
    const Proposition goal =
        Proposition::equality(unsigned32(), Term::call(DefId{0}, {Term::literal(kUnsigned32, 41)}),
                              Term::literal(kUnsigned32, 41));

    CPPL_CHECK(!cppl::kernel::check(context, goal, introduce(unsigned32()), CoreLimits{})
                    .has_value());

    const auto result = cppl::kernel::check(
        context, goal,
        ProofTerm::forall_elimination(general, introduce(unsigned32()),
                                      Term::literal(kUnsigned32, 41)),
        CoreLimits{});

    CPPL_CHECK(result.has_value());
    CPPL_CHECK(result->proposition() == goal);
}

CPPL_TEST(instantiation_at_the_wrong_type_is_rejected) {
    const cppl::kernel::Context context = with_identity();

    const Proposition general = Proposition::for_all(
        unsigned32(),
        Proposition::equality(unsigned32(), Term::call(DefId{0}, {bound()}), bound()));
    const Proposition goal =
        Proposition::equality(unsigned32(), Term::call(DefId{0}, {Term::literal(kUnsigned32, 41)}),
                              Term::literal(kUnsigned32, 41));

    const auto result = cppl::kernel::check(
        context, goal,
        ProofTerm::forall_elimination(general, introduce(unsigned32()),
                                      Term::literal(kSigned32, 41)),
        CoreLimits{});

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::ProofShapeMismatch);
}

CPPL_TEST(instantiating_evidence_that_quantifies_over_nothing_is_rejected) {
    const cppl::kernel::Context context;

    const Proposition unquantified = Proposition::equality(
        unsigned32(), Term::literal(kUnsigned32, 1), Term::literal(kUnsigned32, 1));

    const auto result = cppl::kernel::check(
        context, unquantified,
        ProofTerm::forall_elimination(unquantified, ProofTerm::reflexivity(),
                                      Term::literal(kUnsigned32, 1)),
        CoreLimits{});

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::ProofShapeMismatch);
}

CPPL_TEST(instantiation_substitutes_the_named_binder_only) {
    const cppl::kernel::Context context;

    // forall b : u32. 7 + b = 7 + b
    const Proposition expected = Proposition::for_all(
        unsigned32(), Proposition::equality(unsigned32(),
                                            sum(Term::literal(kUnsigned32, 7), bound()),
                                            sum(Term::literal(kUnsigned32, 7), bound())));

    const auto result = cppl::kernel::check(
        context, expected,
        ProofTerm::forall_elimination(commuted_sum(), introduce_twice(),
                                      Term::literal(kUnsigned32, 7)),
        CoreLimits{});
    CPPL_CHECK(result.has_value());

    // The argument replaces the outer binder, so the other operand must stay
    // the remaining variable.
    const Proposition wrong_position = Proposition::for_all(
        unsigned32(), Proposition::equality(unsigned32(),
                                            sum(bound(), Term::literal(kUnsigned32, 7)),
                                            sum(bound(), Term::literal(kUnsigned32, 7))));

    CPPL_CHECK(!cppl::kernel::check(context, wrong_position,
                                    ProofTerm::forall_elimination(commuted_sum(), introduce_twice(),
                                                                  Term::literal(kUnsigned32, 7)),
                                    CoreLimits{})
                    .has_value());
}

CPPL_TEST(an_argument_is_not_captured_by_a_binder_it_descends_into) {
    const cppl::kernel::Context context;

    // Instantiated at the enclosing variable y, under which a second binder b
    // is still to come. Inside that binder y is one level further out.
    const ProofTerm evidence = ProofTerm::forall_introduction(
        unsigned32(),
        ProofTerm::forall_elimination(commuted_sum(), introduce_twice(), bound()));

    // forall y, b : u32. y + b = y + b
    const auto result = cppl::kernel::check(context, commuted_sum(), evidence, CoreLimits{});
    CPPL_CHECK(result.has_value());

    // Had the argument not been shifted as it descended, it would have become
    // the inner binder and this captured statement would have been accepted.
    const Proposition captured = Proposition::for_all(
        unsigned32(),
        Proposition::for_all(unsigned32(), Proposition::equality(unsigned32(),
                                                                 sum(bound(), bound()),
                                                                 sum(bound(), bound()))));

    CPPL_CHECK(!cppl::kernel::check(context, captured, evidence, CoreLimits{}).has_value());
}

CPPL_TEST(evidence_offered_for_elimination_is_itself_checked) {
    const cppl::kernel::Context context = with_identity();

    // forall x : u32. identity(x) = x + 1, which is false.
    const Proposition false_claim = Proposition::for_all(
        unsigned32(), Proposition::equality(unsigned32(), Term::call(DefId{0}, {bound()}),
                                            sum(bound(), Term::literal(kUnsigned32, 1))));

    const Proposition goal = Proposition::equality(
        unsigned32(), Term::call(DefId{0}, {Term::literal(kUnsigned32, 41)}),
        sum(Term::literal(kUnsigned32, 41), Term::literal(kUnsigned32, 1)));

    const auto result = cppl::kernel::check(
        context, goal,
        ProofTerm::forall_elimination(false_claim, introduce(unsigned32()),
                                      Term::literal(kUnsigned32, 41)),
        CoreLimits{});

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::NotDefinitionallyEqual);
}

namespace {

using cppl::kernel::HypothesisIndex;

// 41 + 1 = 41, which no amount of normalization makes true.
Proposition unreachable_equality() {
    return Proposition::equality(unsigned32(),
                                 sum(Term::literal(kUnsigned32, 41), Term::literal(kUnsigned32, 1)),
                                 Term::literal(kUnsigned32, 41));
}

Proposition settled_equality() {
    return Proposition::equality(unsigned32(), Term::literal(kUnsigned32, 41),
                                 Term::literal(kUnsigned32, 41));
}

}  // namespace

CPPL_TEST(an_implication_is_introduced_by_supposing_its_premise) {
    const cppl::kernel::Context context;
    const Proposition goal = Proposition::implication(unreachable_equality(), settled_equality());

    const auto result = cppl::kernel::check(
        context, goal,
        ProofTerm::implication_introduction(unreachable_equality(), ProofTerm::reflexivity()),
        CoreLimits{});

    CPPL_CHECK(result.has_value());
    CPPL_CHECK(result->proposition() == goal);

    // The premise is supposed, never established: evidence that skips the
    // introduction is not evidence for the implication.
    const auto bare = cppl::kernel::check(context, goal, ProofTerm::reflexivity(), CoreLimits{});
    CPPL_CHECK(!bare.has_value());
    CPPL_CHECK(bare.error().kind == RejectionKind::ProofShapeMismatch);
}

CPPL_TEST(evidence_must_suppose_the_premise_the_goal_states) {
    const cppl::kernel::Context context;
    const Proposition goal = Proposition::implication(unreachable_equality(), settled_equality());

    const auto result = cppl::kernel::check(
        context, goal,
        ProofTerm::implication_introduction(settled_equality(), ProofTerm::reflexivity()),
        CoreLimits{});

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::ProofShapeMismatch);
}

CPPL_TEST(a_hypothesis_proves_the_premise_that_introduced_it) {
    const cppl::kernel::Context context;

    // P -> P, where P is false. Nothing but the hypothesis can close it, so
    // this goal is out of reach of every rule the kernel had before.
    const Proposition goal =
        Proposition::implication(unreachable_equality(), unreachable_equality());

    const auto result = cppl::kernel::check(
        context, goal,
        ProofTerm::implication_introduction(unreachable_equality(),
                                            ProofTerm::hypothesis(HypothesisIndex{0})),
        CoreLimits{});

    CPPL_CHECK(result.has_value());
    CPPL_CHECK(result->proposition() == goal);

    const auto by_reflexivity = cppl::kernel::check(
        context, goal,
        ProofTerm::implication_introduction(unreachable_equality(), ProofTerm::reflexivity()),
        CoreLimits{});
    CPPL_CHECK(!by_reflexivity.has_value());
    CPPL_CHECK(by_reflexivity.error().kind == RejectionKind::NotDefinitionallyEqual);
}

CPPL_TEST(a_hypothesis_no_introduction_placed_in_scope_is_rejected) {
    const cppl::kernel::Context context;

    const auto result = cppl::kernel::check(
        context, unreachable_equality(), ProofTerm::hypothesis(HypothesisIndex{0}), CoreLimits{});

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::MalformedProofTerm);
}

CPPL_TEST(a_hypothesis_standing_for_a_different_premise_is_rejected) {
    const cppl::kernel::Context context;

    // Supposing 41 = 41 says nothing about 41 + 1 = 41.
    const Proposition goal = Proposition::implication(settled_equality(), unreachable_equality());

    const auto result = cppl::kernel::check(
        context, goal,
        ProofTerm::implication_introduction(settled_equality(),
                                            ProofTerm::hypothesis(HypothesisIndex{0})),
        CoreLimits{});

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::ProofShapeMismatch);
}

CPPL_TEST(a_premise_leaves_scope_with_the_implication_that_introduced_it) {
    const cppl::kernel::Context context;

    // (P -> P) -> P, offered evidence that names the innermost hypothesis for
    // both roles. Only (P -> P) is in scope, so it does not stand for P.
    const Proposition conditional =
        Proposition::implication(unreachable_equality(), unreachable_equality());
    const Proposition goal = Proposition::implication(conditional, unreachable_equality());

    const auto result = cppl::kernel::check(
        context, goal,
        ProofTerm::implication_introduction(
            conditional,
            ProofTerm::implication_elimination(conditional,
                                               ProofTerm::hypothesis(HypothesisIndex{0}),
                                               ProofTerm::hypothesis(HypothesisIndex{0}))),
        CoreLimits{});

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::ProofShapeMismatch);
}

CPPL_TEST(discharging_a_premise_establishes_the_conclusion) {
    const cppl::kernel::Context context = with_identity();

    const Proposition premise =
        Proposition::equality(unsigned32(), Term::call(DefId{0}, {Term::literal(kUnsigned32, 41)}),
                              Term::literal(kUnsigned32, 41));
    const Proposition goal = settled_equality();
    const Proposition conditional = Proposition::implication(premise, goal);

    const auto result = cppl::kernel::check(
        context, goal,
        ProofTerm::implication_elimination(
            conditional, ProofTerm::implication_introduction(premise, ProofTerm::reflexivity()),
            ProofTerm::reflexivity()),
        CoreLimits{});

    CPPL_CHECK(result.has_value());
    CPPL_CHECK(result->proposition() == goal);
}

CPPL_TEST(a_premise_offered_without_evidence_is_rejected) {
    const cppl::kernel::Context context;

    const Proposition conditional =
        Proposition::implication(unreachable_equality(), settled_equality());

    // The implication holds, and the premise handed to it does not.
    const auto result = cppl::kernel::check(
        context, settled_equality(),
        ProofTerm::implication_elimination(
            conditional,
            ProofTerm::implication_introduction(unreachable_equality(), ProofTerm::reflexivity()),
            ProofTerm::reflexivity()),
        CoreLimits{});

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::NotDefinitionallyEqual);
}

CPPL_TEST(a_premise_discharged_against_something_other_than_an_implication_is_rejected) {
    const cppl::kernel::Context context;

    const auto result = cppl::kernel::check(
        context, settled_equality(),
        ProofTerm::implication_elimination(settled_equality(), ProofTerm::reflexivity(),
                                           ProofTerm::reflexivity()),
        CoreLimits{});

    CPPL_CHECK(!result.has_value());
    CPPL_CHECK(result.error().kind == RejectionKind::ProofShapeMismatch);
}

CPPL_TEST(a_hypothesis_is_restated_under_the_binders_it_is_used_beneath) {
    const cppl::kernel::Context context;

    // forall x. (x + 1 = x) -> forall y. (x + 1 = x)
    //
    // Under the second binder the premise's variable is one level further out,
    // so the hypothesis has to be restated before it can close the goal.
    const Proposition premise_at_x =
        Proposition::equality(unsigned32(), sum(bound(), Term::literal(kUnsigned32, 1)), bound());
    const Proposition premise_at_x_under_y =
        Proposition::equality(unsigned32(), sum(outer(), Term::literal(kUnsigned32, 1)), outer());

    const ProofTerm evidence = ProofTerm::forall_introduction(
        unsigned32(),
        ProofTerm::implication_introduction(
            premise_at_x, ProofTerm::forall_introduction(
                              unsigned32(), ProofTerm::hypothesis(HypothesisIndex{0}))));

    const Proposition goal = Proposition::for_all(
        unsigned32(),
        Proposition::implication(premise_at_x,
                                 Proposition::for_all(unsigned32(), premise_at_x_under_y)));

    const auto result = cppl::kernel::check(context, goal, evidence, CoreLimits{});
    CPPL_CHECK(result.has_value());

    // Had the hypothesis not been restated, it would have named the inner
    // binder and this different statement would have been accepted.
    const Proposition captured = Proposition::for_all(
        unsigned32(),
        Proposition::implication(premise_at_x, Proposition::for_all(unsigned32(), premise_at_x)));

    CPPL_CHECK(!cppl::kernel::check(context, captured, evidence, CoreLimits{}).has_value());
}
