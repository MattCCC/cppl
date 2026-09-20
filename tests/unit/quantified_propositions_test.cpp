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

v::Expr predicate(v::Expr left, v::Expr right) {
    v::Expr result;
    result.type = v::Type::boolean();
    result.node = v::Binary{v::BinaryOp::Equal, {std::move(left), std::move(right)}};
    return result;
}

v::Expr conjunction(v::Expr left, v::Expr right) {
    v::Expr result;
    result.type = v::Type::boolean();
    result.node = v::Binary{v::BinaryOp::And, {std::move(left), std::move(right)}};
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

CPPL_TEST(a_conjunction_lowers_to_two_kernel_checked_propositions) {
    cppl::diagnostics::Engine engine;
    const auto same = predicate(value(0), value(0));
    const auto program = generate(conjunction(same, conjunction(same, same)), engine);
    CPPL_CHECK(!engine.has_errors());
    CPPL_CHECK_EQ(program.obligations.size(), std::size_t{1});
    const auto& outer = std::get<k::Forall>(program.obligations[0].goal.node);
    CPPL_CHECK(std::holds_alternative<k::And>(outer.body->node));
    const auto checked = cppl::automation::verify(program, engine);
    CPPL_CHECK(!engine.has_errors());
    CPPL_CHECK(checked[0].verdict.is_proven());
}

CPPL_TEST(malformed_conjunctions_never_become_obligations) {
    for (unsigned attack = 0; attack < 7; ++attack) {
        const auto same = predicate(value(0), value(0));
        auto expression = conjunction(same, same);
        auto& operands = std::get<v::Binary>(expression.node).operands;
        if (attack == 0)
            operands.pop_back();
        if (attack == 1)
            operands.push_back(same);
        if (attack == 2)
            expression.type = integer;
        if (attack == 3)
            operands[0] = value(0);
        if (attack == 4)
            operands[1] = value(0);
        if (attack == 5)
            operands[1] = predicate(value(1), value(1));
        if (attack == 6)
            operands[1] = equality(value(0), value(0)); // not a C++ Boolean operand
        cppl::diagnostics::Engine engine;
        const auto program = generate(expression, engine);
        CPPL_CHECK(engine.has_errors());
        CPPL_CHECK(program.obligations.empty());
    }
}

CPPL_TEST(conjunction_identity_tracks_both_sides_and_their_order) {
    cppl::diagnostics::Engine engine;
    const auto same = predicate(value(0), value(0));
    v::Expr zero;
    zero.type = integer;
    zero.node = v::IntLiteral{0};
    const auto other = predicate(value(0), zero);
    const auto first = generate(conjunction(same, other), engine).obligations.at(0).id;
    CPPL_CHECK(first == generate(conjunction(same, other), engine).obligations.at(0).id);
    CPPL_CHECK(!(first == generate(conjunction(same, same), engine).obligations.at(0).id));
    CPPL_CHECK(!(first == generate(conjunction(other, other), engine).obligations.at(0).id));
    CPPL_CHECK(!(first == generate(conjunction(other, same), engine).obligations.at(0).id));
    CPPL_CHECK(!(first == generate(implication(same, other), engine).obligations.at(0).id));
    CPPL_CHECK(!engine.has_errors());
}

CPPL_TEST(conjunction_identity_tracks_definitions_reached_from_either_side) {
    const auto identity = [](bool right, std::int64_t returned) {
        cppl::elaboration::Result elaborated;
        v::Function function;
        function.id = v::FunctionId{0};
        function.symbol = v::SymbolId{"constant"};
        function.qualified_name = "constant";
        function.result = integer;
        function.purity = v::Purity::Pure;
        v::Expr body;
        body.type = integer;
        body.node = v::IntLiteral{returned};
        function.returned_value = body;
        elaborated.module.functions.push_back(function);
        v::Expr call;
        call.type = integer;
        call.node = v::Call{function.symbol, function.qualified_name, {}};
        const auto dependent = predicate(call, call);
        const auto same = predicate(value(0), value(0));
        v::Law law;
        law.name = "dependency";
        law.parameters = {{"x", integer}};
        law.proposition = right ? conjunction(same, dependent) : conjunction(dependent, same);
        elaborated.module.laws.push_back(law);
        cppl::diagnostics::Engine engine;
        const auto program = cppl::obligations::generate(elaborated.module, elaborated, engine);
        CPPL_CHECK(!engine.has_errors());
        CPPL_CHECK_EQ(program.obligations.size(), std::size_t{1});
        return program.obligations.at(0).id;
    };
    for (const bool right : {false, true}) {
        CPPL_CHECK(identity(right, 0) == identity(right, 0));
        CPPL_CHECK(!(identity(right, 0) == identity(right, 1)));
    }
}
