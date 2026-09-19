// The Verification IR is typed, identified by content rather than by address,
// and carries provenance on every node (ARCHITECTURE.md 17).

#include "cppl/testing/test.hpp"
#include "cppl/vir/module.hpp"

#include <string>

namespace {

using cppl::vir::Binary;
using cppl::vir::BinaryOp;
using cppl::vir::Call;
using cppl::vir::Expr;
using cppl::vir::ExprId;
using cppl::vir::Function;
using cppl::vir::FunctionId;
using cppl::vir::IntLiteral;
using cppl::vir::Law;
using cppl::vir::LawId;
using cppl::vir::Module;
using cppl::vir::Parameter;
using cppl::vir::ParameterRef;
using cppl::vir::Purity;
using cppl::vir::SymbolId;
using cppl::vir::Type;

Expr parameter(std::uint32_t index, std::string name, std::uint32_t id) {
    Expr expr;
    expr.id = ExprId{id};
    expr.type = Type::integer(32, true);
    expr.provenance.range.begin = cppl::source::SourceLocation{"main.cpp", 7, 5};
    expr.node = ParameterRef{index, std::move(name)};
    return expr;
}

Expr call(SymbolId callee, std::string name, Expr argument, std::uint32_t id) {
    Expr expr;
    expr.id = ExprId{id};
    expr.type = Type::integer(32, true);
    expr.provenance.range.begin = cppl::source::SourceLocation{"main.cpp", 7, 13};
    expr.node = Call{std::move(callee), std::move(name), {std::move(argument)}};
    return expr;
}

}  // namespace

CPPL_TEST(vir_nodes_carry_their_source_position) {
    const Expr expr = parameter(0, "x", 1);

    CPPL_CHECK_EQ(expr.provenance.range.begin.file, std::string("main.cpp"));
    CPPL_CHECK_EQ(expr.provenance.range.begin.line, 7u);
    CPPL_CHECK_EQ(expr.provenance.range.begin.column, 5u);
}

CPPL_TEST(identity_is_content_not_address) {
    const Expr first = parameter(0, "x", 1);
    const Expr second = parameter(0, "x", 1);

    CPPL_CHECK(&first != &second);
    CPPL_CHECK(first == second);

    const Expr other = parameter(1, "y", 1);
    CPPL_CHECK(!(first == other));
}

CPPL_TEST(symbols_identify_functions_across_structures) {
    Module module;

    Function function;
    function.id = FunctionId{0};
    function.symbol = SymbolId{"c:@F@identity#I#"};
    function.qualified_name = "identity";
    function.parameters = {Parameter{"x", Type::integer(32, true)}};
    function.result = Type::integer(32, true);
    function.purity = Purity::Pure;
    function.returned_value = parameter(0, "x", 1);
    module.functions.push_back(std::move(function));

    CPPL_CHECK(module.find(SymbolId{"c:@F@identity#I#"}) != nullptr);
    CPPL_CHECK(module.find(SymbolId{"c:@F@other#I#"}) == nullptr);
    CPPL_CHECK(module.find(SymbolId{"c:@F@identity#I#"})->purity == Purity::Pure);
}

CPPL_TEST(operators_are_typed_variants_not_text) {
    Expr equality;
    equality.id = ExprId{4};
    equality.type = Type::boolean();
    equality.node = Binary{BinaryOp::Equal,
                           {call(SymbolId{"c:@F@identity#I#"}, "identity", parameter(0, "x", 1), 2),
                            parameter(0, "x", 3)}};

    CPPL_CHECK(equality.type.is_boolean());
    CPPL_CHECK(std::get<Binary>(equality.node).op == BinaryOp::Equal);
    CPPL_CHECK_EQ(cppl::vir::describe(equality), std::string("(identity(x) == x)"));
}

CPPL_TEST(a_law_quantifies_over_its_parameters) {
    Law law;
    law.id = LawId{0};
    law.name = "identity_returns_input";
    law.parameters = {Parameter{"x", Type::integer(32, true)}};
    law.proposition = parameter(0, "x", 1);
    law.range.begin = cppl::source::SourceLocation{"main.cpp", 6, 5};

    CPPL_CHECK_EQ(law.parameters.size(), std::size_t{1});
    CPPL_CHECK_EQ(law.parameters[0].name, std::string("x"));
    CPPL_CHECK(law.parameters[0].type.is_integer());
    CPPL_CHECK_EQ(law.parameters[0].type.integer_type().width, std::uint16_t{32});
    CPPL_CHECK_EQ(law.range.begin.line, 6u);
}

CPPL_TEST(integer_types_record_width_and_signedness) {
    CPPL_CHECK_EQ(cppl::vir::describe(Type::integer(32, true)), std::string("i32"));
    CPPL_CHECK_EQ(cppl::vir::describe(Type::integer(64, false)), std::string("u64"));
    CPPL_CHECK_EQ(cppl::vir::describe(Type::boolean()), std::string("bool"));
    CPPL_CHECK(!(Type::integer(32, true) == Type::integer(32, false)));
}
