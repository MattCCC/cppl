#include "cppl/automation/evidence.hpp"
#include "cppl/obligations/generate.hpp"
#include "cppl/testing/test.hpp"

namespace {
namespace v = cppl::vir;
namespace k = cppl::kernel;
const auto integer = v::Type::integer(32, false);

// A law declared with one parameter states its proposition under one binder, so
// scope position 0 is that parameter and every further position is a binder the
// proposition introduces itself.
v::Expr value(std::uint32_t position) {
    v::Expr result;
    result.type = integer;
    result.node = v::ParameterRef{position, "x"};
    return result;
}

v::Expr equality(v::Expr left, v::Expr right) {
    v::Expr result;
    result.type = v::Type::proposition();
    result.node = v::FormalEquality{integer, {std::move(left), std::move(right)}};
    return result;
}

v::Expr universal(std::vector<v::Type> binders, v::Expr body) {
    v::Expr result;
    result.type = v::Type::proposition();
    result.node = v::Universal{std::move(binders), {std::move(body)}};
    return result;
}

v::Expr implication(v::Expr premise, v::Expr conclusion) {
    v::Expr result;
    result.type = v::Type::proposition();
    result.node = v::Implication{{std::move(premise), std::move(conclusion)}};
    return result;
}

cppl::obligations::Program generate(v::Expr proposition, cppl::diagnostics::Engine& engine) {
    cppl::elaboration::Result elaborated;
    v::Law law;
    law.name = "quantified";
    law.parameters = {{"x", integer}};
    law.proposition = std::move(proposition);
    elaborated.module.laws.push_back(std::move(law));
    return cppl::obligations::generate(elaborated.module, elaborated, engine);
}
} // namespace

CPPL_TEST(a_universal_lowers_to_a_kernel_quantifier_per_binder) {
    cppl::diagnostics::Engine engine;
    const auto program = generate(universal({integer, integer}, equality(value(1), value(1))), engine);
    CPPL_CHECK(!engine.has_errors());
    CPPL_CHECK_EQ(program.obligations.size(), std::size_t{1});

    // One quantifier for the law's parameter and one for each binder.
    const auto& outer = std::get<k::Forall>(program.obligations[0].goal.node);
    const auto& first = std::get<k::Forall>(outer.body->node);
    const auto& second = std::get<k::Forall>(first.body->node);
    CPPL_CHECK(std::holds_alternative<k::Eq>(second.body->node));

    const auto checked = cppl::automation::verify(program, engine);
    CPPL_CHECK(!engine.has_errors());
    CPPL_CHECK(checked[0].verdict.is_proven());
}

CPPL_TEST(an_implication_lowers_to_a_kernel_implication) {
    cppl::diagnostics::Engine engine;
    const auto goal = implication(equality(value(0), value(0)), equality(value(0), value(0)));
    const auto program = generate(goal, engine);
    CPPL_CHECK(!engine.has_errors());
    CPPL_CHECK_EQ(program.obligations.size(), std::size_t{1});
    const auto& quantified = std::get<k::Forall>(program.obligations[0].goal.node);
    CPPL_CHECK(std::holds_alternative<k::Implies>(quantified.body->node));

    const auto checked = cppl::automation::verify(program, engine);
    CPPL_CHECK(!engine.has_errors());
    CPPL_CHECK(checked[0].verdict.is_proven());
}

CPPL_TEST(a_binder_is_distinct_from_the_parameter_it_shadows) {
    cppl::diagnostics::Engine engine;
    // `forall y. x == y` under the law's own parameter. The two references must
    // reach different quantifiers, so the proposition is not an instance of
    // reflexivity and nothing proves it.
    const auto program = generate(universal({integer}, equality(value(0), value(1))), engine);
    CPPL_CHECK(!engine.has_errors());

    const auto& outer = std::get<k::Forall>(program.obligations[0].goal.node);
    const auto& inner = std::get<k::Forall>(outer.body->node);
    const auto& body = std::get<k::Eq>(inner.body->node);
    CPPL_CHECK(!(body.lhs == body.rhs));

    const auto checked = cppl::automation::verify(program, engine);
    CPPL_CHECK(engine.has_errors());
    CPPL_CHECK(!checked[0].verdict.is_proven());
}

CPPL_TEST(malformed_quantified_propositions_never_become_obligations) {
    for (unsigned attack = 0; attack < 7; ++attack) {
        auto expression = universal({integer}, equality(value(1), value(1)));
        auto& quantified = std::get<v::Universal>(expression.node);
        if (attack == 0)
            quantified.binders.clear();
        if (attack == 1)
            quantified.body.clear();
        if (attack == 2)
            quantified.body.push_back(equality(value(1), value(1)));
        if (attack == 3)
            expression.type = v::Type::boolean();
        if (attack == 4)
            quantified.body[0] = value(1); // an integer, not a proposition
        if (attack == 5)
            quantified.binders[0] = v::Type::proposition();
        if (attack == 6)
            quantified.body[0] = universal({}, equality(value(1), value(1)));

        cppl::diagnostics::Engine engine;
        const auto program = generate(expression, engine);
        CPPL_CHECK(engine.has_errors());
        CPPL_CHECK(program.obligations.empty());
    }
}

CPPL_TEST(malformed_implications_never_become_obligations) {
    for (unsigned attack = 0; attack < 4; ++attack) {
        auto expression = implication(equality(value(0), value(0)), equality(value(0), value(0)));
        auto& operands = std::get<v::Implication>(expression.node).operands;
        if (attack == 0)
            operands.pop_back();
        if (attack == 1)
            operands.push_back(equality(value(0), value(0)));
        if (attack == 2)
            expression.type = v::Type::boolean();
        if (attack == 3)
            operands[1] = value(0); // an integer, not a proposition

        cppl::diagnostics::Engine engine;
        const auto program = generate(expression, engine);
        CPPL_CHECK(engine.has_errors());
        CPPL_CHECK(program.obligations.empty());
    }
}

CPPL_TEST(a_quantified_body_cannot_reach_past_its_own_scope) {
    cppl::diagnostics::Engine engine;
    // One parameter and one binder leave two positions in scope; the third is
    // nothing the proposition may name.
    const auto program = generate(universal({integer}, equality(value(2), value(2))), engine);
    CPPL_CHECK(engine.has_errors());
    CPPL_CHECK(program.obligations.empty());
}

CPPL_TEST(a_false_universal_is_rejected_by_the_kernel) {
    cppl::diagnostics::Engine engine;
    v::Expr zero;
    zero.type = integer;
    zero.node = v::IntLiteral{0};
    const auto program = generate(universal({integer}, equality(value(1), zero)), engine);
    const auto checked = cppl::automation::verify(program, engine);
    CPPL_CHECK(engine.has_errors());
    CPPL_CHECK(!checked[0].verdict.is_proven());
}

CPPL_TEST(a_false_implication_is_rejected_by_the_kernel) {
    cppl::diagnostics::Engine engine;
    v::Expr zero;
    zero.type = integer;
    zero.node = v::IntLiteral{0};
    v::Expr one;
    one.type = integer;
    one.node = v::IntLiteral{1};
    const auto program = generate(implication(equality(value(0), zero), equality(value(0), one)), engine);
    const auto checked = cppl::automation::verify(program, engine);
    CPPL_CHECK(engine.has_errors());
    CPPL_CHECK(!checked[0].verdict.is_proven());
}
