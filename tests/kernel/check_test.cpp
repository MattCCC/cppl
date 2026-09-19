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
