#include "cppl/automation/evidence.hpp"
#include "cppl/obligations/generate.hpp"
#include "cppl/testing/test.hpp"

namespace {
namespace v = cppl::vir;
namespace k = cppl::kernel;
const auto integer = v::Type::integer(32, false);

v::Expr value(std::uint32_t index) {
    v::Expr result;
    result.type = integer;
    result.node = v::ParameterRef{index, "x"};
    return result;
}

v::Expr equality() {
    v::Expr result;
    result.type = v::Type::proposition();
    result.node = v::FormalEquality{integer, {value(0), value(0)}};
    return result;
}

cppl::obligations::Program generate(v::Expr proposition, cppl::diagnostics::Engine& engine) {
    cppl::elaboration::Result elaborated;
    v::Law law;
    law.name = "formal";
    law.parameters = {{"x", integer}};
    law.proposition = std::move(proposition);
    elaborated.module.laws.push_back(std::move(law));
    return cppl::obligations::generate(elaborated.module, elaborated, engine);
}
} // namespace

CPPL_TEST(formal_equality_lowers_to_existing_kernel_equality) {
    cppl::diagnostics::Engine engine;
    const auto program = generate(equality(), engine);
    CPPL_CHECK(!engine.has_errors());
    CPPL_CHECK_EQ(program.obligations.size(), std::size_t{1});
    const auto& quantified = std::get<k::Forall>(program.obligations[0].goal.node);
    CPPL_CHECK(std::holds_alternative<k::Eq>(quantified.body->node));
    const auto checked = cppl::automation::verify(program, engine);
    CPPL_CHECK(!engine.has_errors());
    CPPL_CHECK(checked[0].verdict.is_proven());
}

CPPL_TEST(malformed_formal_equality_never_becomes_an_obligation) {
    for (unsigned attack = 0; attack < 4; ++attack) {
        auto expression = equality();
        auto& eq = std::get<v::FormalEquality>(expression.node);
        if (attack == 0)
            eq.operands.pop_back();
        if (attack == 1)
            eq.operands.push_back(value(0));
        if (attack == 2)
            eq.operand_type = v::Type::integer(64, true);
        if (attack == 3)
            expression.type = v::Type::boolean();
        cppl::diagnostics::Engine engine;
        const auto program = generate(expression, engine);
        CPPL_CHECK(engine.has_errors());
        CPPL_CHECK(program.obligations.empty());
    }
}

CPPL_TEST(formal_equality_does_not_capture_out_of_scope_variables) {
    auto expression = equality();
    std::get<v::FormalEquality>(expression.node).operands = {value(1), value(1)};
    cppl::diagnostics::Engine engine;
    const auto program = generate(expression, engine);
    CPPL_CHECK(engine.has_errors());
    CPPL_CHECK(program.obligations.empty());
}

CPPL_TEST(false_formal_equality_is_rejected_by_the_kernel) {
    auto expression = equality();
    v::Expr zero;
    zero.type = integer;
    zero.node = v::IntLiteral{0};
    std::get<v::FormalEquality>(expression.node).operands[1] = zero;
    cppl::diagnostics::Engine engine;
    const auto program = generate(expression, engine);
    const auto checked = cppl::automation::verify(program, engine);
    CPPL_CHECK(engine.has_errors());
    CPPL_CHECK(!checked[0].verdict.is_proven());
}
