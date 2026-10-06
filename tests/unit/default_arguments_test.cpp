// Default arguments of verified functions in the generated declarations
// (SPEC.md R.16).
//
// A call relying on a default argument evaluates it where the call stands, so
// a default belongs to the call and never to the contract. The probes a
// verified function's clauses are read back from restate its parameter list
// without the defaults, everything else kept, while both texts keep the
// function's own declaration as written. A default whose end cannot be told
// before lookup is refused rather than guessed. And elaboration reads a clause
// only from a probe whose parameters are the function's own, `result` after
// them for a postcondition, since a position in the clause names the
// function's parameter at that position only while the two lists agree.
//
// SPEC: CONTRACTCOMP-002, EDGECASE-038
// TRUST.md TCB-CORR-004, TCB-CALL-001

#include "cppl/clang/ast.hpp"
#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/elaboration/elaborate.hpp"
#include "cppl/frontend/projection.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/source/location.hpp"
#include "cppl/testing/test.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace cppl;

namespace {

// A unit lexed, recognized and projected, kept together and in place because
// the tokens refer into the text.
struct Projected {
    std::string text;
    std::unique_ptr<frontend::TokenStream> stream;
    frontend::Syntax syntax;
    frontend::Projection projection;
    bool recognition_errors = false;
};

std::unique_ptr<Projected> projected(std::string_view written) {
    auto unit = std::make_unique<Projected>();
    unit->text = written;
    unit->stream = std::make_unique<frontend::TokenStream>(frontend::lex(unit->text, "main.cpp"));
    diagnostics::Engine engine;
    unit->syntax = frontend::recognize(*unit->stream, engine);
    unit->recognition_errors = engine.has_errors();
    unit->projection = frontend::project(*unit->stream, unit->syntax, {});
    return unit;
}

bool reports(const std::vector<diagnostics::Diagnostic>& diagnostics, std::string_view message) {
    return std::ranges::any_of(diagnostics, [message](const diagnostics::Diagnostic& diagnostic) {
        return diagnostic.message.find(message) != std::string::npos;
    });
}

constexpr std::string_view kDefaults =
    "# 1 \"main.cpp\"\n"
    "template <int A, int B> int first() { return A; }\n"
    "int f(int a, int b) { return a + b; }\n"
    "verified int take(int p /* p */ = 50, int q = (first<1, 2>()), [[maybe_unused]] int = f(1, 2))\n"
    "    expects (q > 0)\n"
    "    ensures (result == p)\n"
    "{\n"
    "    return p;\n"
    "}\n";

} // namespace

CPPL_TEST(a_probe_restates_the_parameters_without_their_defaults) {
    const auto unit = projected(kDefaults);
    CPPL_CHECK(!unit->recognition_errors);
    CPPL_CHECK(unit->projection.diagnostics.empty());
    CPPL_CHECK(unit->projection.contract_functions.size() == 1);
    const frontend::ContractFunctions& contract = unit->projection.contract_functions.front();
    // Every comment, attribute and name stays; each default goes, the one
    // holding a template-id's comma within its parentheses whole.
    const std::string parameters = "(int p /* p */ , int q , [[maybe_unused]] int ";
    CPPL_CHECK(unit->projection.analysis.find(contract.postcondition_name + parameters + ", int  result)") !=
               std::string::npos);
    CPPL_CHECK(contract.precondition_names.size() == 1);
    CPPL_CHECK(unit->projection.analysis.find(contract.precondition_names.front() + parameters + ")") !=
               std::string::npos);
    // The function itself keeps its defaults in both texts.
    const std::string declared =
        "int take(int p /* p */ = 50, int q = (first<1, 2>()), [[maybe_unused]] int = f(1, 2))";
    CPPL_CHECK(unit->projection.analysis.find(declared) != std::string::npos);
    CPPL_CHECK(unit->projection.runtime.find(declared) != std::string::npos);
}

CPPL_TEST(a_template_probe_declared_before_the_function_restates_no_default) {
    const auto unit = projected("# 1 \"main.cpp\"\n"
                                "template <class T> verified T below(T v, T hi = T(10))\n"
                                "    expects (v < hi)\n"
                                "    ensures (result == v)\n"
                                "{\n"
                                "    return v;\n"
                                "}\n");
    CPPL_CHECK(!unit->recognition_errors);
    CPPL_CHECK(unit->projection.contract_functions.size() == 1);
    const frontend::ContractFunctions& contract = unit->projection.contract_functions.front();
    CPPL_CHECK(contract.precondition_names.size() == 1);
    // Declared once before the function and defined after it: a default stated
    // twice would be a redefinition, so neither states one.
    const std::string declared = contract.precondition_names.front() + "(T v, T hi );";
    CPPL_CHECK(unit->projection.analysis.find(declared) != std::string::npos);
    CPPL_CHECK(unit->projection.analysis.find("T hi = T(10)") == unit->projection.analysis.rfind("T hi = T(10)"));
}

CPPL_TEST(a_default_whose_end_lookup_decides_is_refused) {
    for (const std::string_view parameters :
         {"int q = first<1, 2>(), int r = 3", "int q = x < y, int r = 3", "int q = first<1, 2>()"}) {
        const auto unit = projected("# 1 \"main.cpp\"\n"
                                    "verified int take(" +
                                    std::string(parameters) + ") ensures (result == 0) { return 0; }\n");
        CPPL_CHECK(!unit->recognition_errors);
        CPPL_CHECK(
            reports(unit->projection.diagnostics, "a default argument of verified function 'take' is not delimited"));
    }
    // Every `<` closed before the comma: the comma separates the parameters.
    const auto closed = projected("# 1 \"main.cpp\"\n"
                                  "verified int take(int q = first<1>(), int r = (a < b), int s = c<d>>(e))"
                                  " ensures (result == 0) { return 0; }\n");
    CPPL_CHECK(!closed->recognition_errors);
    CPPL_CHECK(closed->projection.diagnostics.empty());
}

namespace {

clangbridge::Type integer_type() {
    clangbridge::Type type;
    type.kind = clangbridge::TypeKind::Int;
    type.width = 32;
    type.is_signed = true;
    type.spelling = "int";
    return type;
}

clangbridge::Type boolean_type() {
    clangbridge::Type type;
    type.kind = clangbridge::TypeKind::Bool;
    type.width = 1;
    type.is_signed = false;
    type.spelling = "bool";
    return type;
}

clangbridge::Expr parameter_reference(std::uint32_t index, std::string name, const source::SourceLocation& at) {
    clangbridge::Expr expression;
    expression.type = integer_type();
    expression.location = at;
    expression.node = clangbridge::ParameterRef{index, std::move(name)};
    return expression;
}

// The diagnostics elaboration reports for `take` read back through a
// postcondition probe Clang reported with `probe_parameters`, as the bridge
// would have resolved the analysis text if the projector had written them.
std::vector<diagnostics::Diagnostic> elaborated(const std::vector<clangbridge::Parameter>& probe_parameters) {
    const auto unit = projected("# 1 \"main.cpp\"\n"
                                "verified int take(int p = 50) ensures (result == p) { return p; }\n");
    CPPL_CHECK(!unit->recognition_errors);
    CPPL_CHECK(unit->syntax.verified_functions.size() == 1);
    CPPL_CHECK(unit->projection.contract_functions.size() == 1);
    const frontend::VerifiedFunction& declared = unit->syntax.verified_functions.front();
    const std::optional<std::size_t> offset = unit->projection.declaration_offset(declared.function_offset);
    CPPL_CHECK(offset.has_value());
    const source::SourceLocation at = declared.function_location;

    clangbridge::TranslationUnit resolved;
    clangbridge::Function take;
    take.usr = "c:@F@take#I#";
    take.name = "take";
    take.qualified_name = "take";
    take.parameters = {clangbridge::Parameter{"p", integer_type()}};
    take.result = integer_type();
    take.location = at;
    take.has_body = true;
    take.analysis_offset = *offset;
    take.external_linkage = true;
    take.returned_value = parameter_reference(0, "p", at);
    resolved.functions.push_back(take);

    clangbridge::Function probe;
    probe.name = unit->projection.contract_functions.front().postcondition_name;
    probe.usr = "c:@F@" + probe.name;
    probe.qualified_name = probe.name;
    probe.parameters = probe_parameters;
    probe.result = boolean_type();
    probe.location = declared.postcondition()->location;
    probe.has_body = true;
    probe.analysis_offset = *offset + 1000;
    clangbridge::Binary equal;
    equal.op = clangbridge::BinaryOp::Equal;
    equal.operands.push_back(parameter_reference(static_cast<std::uint32_t>(probe_parameters.size()) - 1,
                                                 probe_parameters.back().name, probe.location));
    equal.operands.push_back(parameter_reference(0, "p", probe.location));
    clangbridge::Expr stated;
    stated.type = boolean_type();
    stated.location = probe.location;
    stated.node = std::move(equal);
    probe.returned_value = std::move(stated);
    resolved.functions.push_back(probe);

    diagnostics::Engine engine;
    (void)elaboration::elaborate(elaboration::Request{unit->syntax, unit->projection, resolved}, engine);
    return engine.diagnostics();
}

constexpr std::string_view kNotRestated =
    "the postcondition of verified function 'take' was not stated over the parameters of verified function 'take'";

} // namespace

CPPL_TEST(a_clause_is_read_from_a_probe_restating_the_parameters) {
    const std::vector<diagnostics::Diagnostic> diagnostics =
        elaborated({clangbridge::Parameter{"p", integer_type()}, clangbridge::Parameter{"result", integer_type()}});
    CPPL_CHECK(!reports(diagnostics, kNotRestated));
}

CPPL_TEST(a_clause_whose_probe_lost_a_parameter_is_refused) {
    // `result` would stand where `p` does: the clause would state p == p.
    CPPL_CHECK(reports(elaborated({clangbridge::Parameter{"result", integer_type()}}), kNotRestated));
    // A parameter of another type at a position is another parameter.
    clangbridge::Type narrow = integer_type();
    narrow.width = 16;
    CPPL_CHECK(
        reports(elaborated({clangbridge::Parameter{"p", narrow}, clangbridge::Parameter{"result", integer_type()}}),
                kNotRestated));
}
