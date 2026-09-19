#include "cppl/kernel/check.hpp"
#include "cppl/obligations/generate.hpp"
#include "cppl/testing/test.hpp"

namespace {

namespace k = cppl::kernel;
namespace v = cppl::vir;
namespace o = cppl::obligations;

const auto kUnsigned = k::Type::integer(32, k::Signedness::Unsigned);
const auto vUnsigned = v::Type::integer(32, false);

v::Expr parameter(std::uint32_t position) {
    v::Expr expression;
    expression.type = vUnsigned;
    expression.node = v::ParameterRef{position, {}};
    return expression;
}

v::Expr equality(v::Expr left, v::Expr right) {
    v::Expr expression;
    expression.type = v::Type::boolean();
    expression.node = v::Binary{v::BinaryOp::Equal, {std::move(left), std::move(right)}};
    return expression;
}

v::Function first() {
    v::Function function;
    function.symbol = v::SymbolId{"first"};
    function.qualified_name = "first";
    function.parameters = {{"x", vUnsigned}, {"y", vUnsigned}};
    function.result = vUnsigned;
    function.returned_value = parameter(0);
    function.contract = v::Contract{std::nullopt, equality(parameter(2), parameter(0)), {}};
    return function;
}

o::Program generate(v::Function function) {
    cppl::elaboration::Result elaborated;
    elaborated.module.functions.push_back(std::move(function));
    cppl::diagnostics::Engine engine;
    auto program = o::generate(elaborated.module, elaborated, engine);
    CPPL_CHECK(!engine.has_errors());
    CPPL_CHECK_EQ(program.obligations.size(), std::size_t{1});
    return program;
}

k::Proposition conditional(bool same) {
    const auto x = k::Term::variable(k::VarIndex{0});
    const auto zero = k::Term::literal(kUnsigned.integer_type(), 0);
    const auto one = k::Term::literal(kUnsigned.integer_type(), 1);
    const auto sum = k::Term::primitive(k::PrimOp::AddWrap, kUnsigned.integer_type(), {x, one});
    return k::Proposition::for_all(
        kUnsigned,
        k::Proposition::implication(k::Proposition::equality(kUnsigned, x, same ? zero : one),
                                   k::Proposition::equality(kUnsigned, sum, one)));
}

}  // namespace

CPPL_TEST(result_substitution_closes_over_the_correct_parameter) {
    const auto program = generate(first());
    const auto& obligation = program.obligations.front();
    const auto x = k::Term::variable(k::VarIndex{1});
    const auto expected = k::Proposition::for_all(
        kUnsigned, k::Proposition::for_all(kUnsigned, k::Proposition::equality(kUnsigned, x, x)));
    CPPL_CHECK(obligation.goal == expected);
    CPPL_CHECK(obligation.origin == o::Origin::FunctionContract);
    CPPL_CHECK(!obligation.law.has_value());
    CPPL_CHECK(program.proof_for(obligation) == nullptr);
    CPPL_CHECK(k::check(program.context, obligation.goal,
                        o::automatic_evidence(obligation.goal), {}).has_value());
}

CPPL_TEST(changing_the_return_changes_the_obligation_and_invalidates_the_proof) {
    const auto correct = generate(first());
    auto wrong = first();
    wrong.returned_value = parameter(1);
    const auto changed = generate(std::move(wrong));
    CPPL_CHECK(correct.obligations.front().id.text() != changed.obligations.front().id.text());
    const auto& goal = changed.obligations.front().goal;
    CPPL_CHECK(!k::check(changed.context, goal, o::automatic_evidence(goal), {}).has_value());
}

CPPL_TEST(automatic_equality_rewrite_requires_kernel_checked_hypothesis_evidence) {
    const k::Context context;
    const auto goal = conditional(true);
    auto evidence = o::automatic_evidence(goal);
    CPPL_CHECK(k::check(context, goal, evidence, {}).has_value());
    auto& forall = std::get<k::ForallIntroduction>(evidence.node);
    auto& implies = std::get<k::ImplicationIntroduction>(forall.body->node);
    auto& rewrite = std::get<k::EqualityElimination>(implies.body->node);
    const auto forged = k::ProofTerm::forall_introduction(
        forall.binder, k::ProofTerm::implication_introduction(
                           *implies.premise,
                           k::ProofTerm::equality_elimination(
                               rewrite.type, rewrite.lhs, rewrite.rhs, *rewrite.motive,
                               k::ProofTerm::hypothesis(k::HypothesisIndex{1}), *rewrite.evidence)));
    CPPL_CHECK(!k::check(context, goal, forged, {}).has_value());
    const auto wrong = conditional(false);
    CPPL_CHECK(!k::check(context, wrong, o::automatic_evidence(wrong), {}).has_value());
    CPPL_CHECK(!k::check(context, goal, o::definitional_evidence(goal), {}).has_value());
}

CPPL_TEST(automatic_hypotheses_are_shifted_under_later_quantifiers) {
    const k::Context context;
    const auto zero = k::Term::literal(kUnsigned.integer_type(), 0);
    const auto premise = k::Proposition::equality(kUnsigned, k::Term::variable(k::VarIndex{0}), zero);
    const auto outer = k::Proposition::equality(kUnsigned, k::Term::variable(k::VarIndex{1}), zero);
    const auto goal = k::Proposition::for_all(
        kUnsigned, k::Proposition::implication(premise, k::Proposition::for_all(kUnsigned, outer)));
    CPPL_CHECK(k::check(context, goal, o::automatic_evidence(goal), {}).has_value());
    const auto captured = k::Proposition::for_all(
        kUnsigned, k::Proposition::implication(premise, k::Proposition::for_all(kUnsigned, premise)));
    CPPL_CHECK(!k::check(context, captured, o::automatic_evidence(captured), {}).has_value());
}
