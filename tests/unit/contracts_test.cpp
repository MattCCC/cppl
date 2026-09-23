#include "composition.hpp"
#include "cppl/automation/evidence.hpp"
#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/elaboration/elaborate.hpp"
#include "cppl/kernel/check.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/obligations/generate.hpp"
#include "cppl/obligations/obligation.hpp"
#include "cppl/testing/test.hpp"
#include "cppl/vir/expr.hpp"
#include "cppl/vir/module.hpp"
#include "cppl/vir/place.hpp"
#include "cppl/vir/types.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

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
    function.contract = v::Contract{.postcondition = equality(parameter(2), parameter(0))};
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
        kUnsigned, k::Proposition::implication(k::Proposition::equality(kUnsigned, x, same ? zero : one),
                                               k::Proposition::equality(kUnsigned, sum, one)));
}

o::Program composed(bool weak = false) {
    auto callee = first();
    callee.id = v::FunctionId{0};
    CPPL_CHECK(callee.contract.has_value());
    callee.contract->preconditions.push_back(equality(parameter(0), parameter(1)));
    if (weak) {
        callee.contract->postcondition = equality(parameter(0), parameter(0));
    }
    auto caller = first();
    caller.id = v::FunctionId{1};
    caller.symbol = v::SymbolId{"swapped"};
    caller.qualified_name = "swapped";
    caller.contract = v::Contract{.preconditions = {equality(parameter(0), parameter(1))},
                                  .postcondition = equality(parameter(2), parameter(1))};
    v::Expr call;
    call.id = v::ExprId{1};
    call.type = vUnsigned;
    call.node = v::Call{callee.symbol, callee.qualified_name, {parameter(1), parameter(0)}};
    caller.returned_value = std::move(call);
    cppl::elaboration::Result elaborated;
    elaborated.module.functions = {std::move(caller), std::move(callee)};
    cppl::diagnostics::Engine engine;
    auto program = o::generate(elaborated.module, elaborated, engine);
    CPPL_CHECK(!engine.has_errors());
    CPPL_CHECK_EQ(program.contracts.size(), std::size_t{2});
    CPPL_CHECK_EQ(program.obligations.size(), std::size_t{3});
    return program;
}

k::Proposition abstract_caller(bool summary, bool forged = false) {
    const auto x = k::Term::variable(k::VarIndex{2});
    const auto y = k::Term::variable(k::VarIndex{1});
    const auto result = k::Term::variable(k::VarIndex{0});
    const auto post =
        k::Proposition::equality(kUnsigned, result, forged ? k::Term::literal(kUnsigned.integer_type(), 0) : y);
    auto goal = summary ? k::Proposition::implication(post, post) : post;
    goal = k::Proposition::implication(k::Proposition::equality(kUnsigned, x, y), goal);
    for (int index = 0; index < 3; ++index) {
        goal = k::Proposition::for_all(kUnsigned, std::move(goal));
    }
    return goal;
}

o::Program branching() {
    auto function = first();
    CPPL_CHECK(function.returned_value.has_value());
    function.returned_value->node = v::Conditional{{equality(parameter(0), parameter(1)), parameter(1), parameter(0)}};
    cppl::elaboration::Result elaborated;
    elaborated.module.functions.push_back(std::move(function));
    cppl::diagnostics::Engine engine;
    auto program = o::generate(elaborated.module, elaborated, engine);
    CPPL_CHECK(!engine.has_errors());
    CPPL_CHECK_EQ(program.obligations.size(), std::size_t{3});
    CPPL_CHECK_EQ(program.contracts.front().paths.size(), std::size_t{2});
    return program;
}

v::Expr number(std::int64_t value) {
    v::Expr expression;
    expression.type = vUnsigned;
    expression.node = v::IntLiteral{value};
    return expression;
}

// The place these tests version: one local of the body under test.
v::Place named(std::string spelling = "y") {
    v::Place place;
    place.root.kind = v::PlaceRoot::Kind::Local;
    place.root.id = 0;
    place.spelling = std::move(spelling);
    return place;
}

v::Expr local(std::uint32_t version) {
    v::Expr expression;
    expression.type = vUnsigned;
    expression.node = v::PlaceRef{version, named()};
    return expression;
}

v::Expr versioned(std::uint32_t version, v::Expr value, v::Expr body) {
    v::Expr expression;
    expression.type = body.type;
    expression.node = v::PlaceVersion{version, named(), {std::move(value), std::move(body)}, {}};
    return expression;
}

// `y = 7; y = x; return y;` against `result == x`. Reading the version the
// assignment established proves the contract; reading the one the initializer
// established would prove a false one.
o::Program assigned(std::uint32_t observed) {
    auto function = first();
    function.returned_value = versioned(0, number(7), versioned(1, parameter(0), local(observed)));
    cppl::elaboration::Result elaborated;
    elaborated.module.functions.push_back(std::move(function));
    cppl::diagnostics::Engine engine;
    auto program = o::generate(elaborated.module, elaborated, engine);
    CPPL_CHECK(!engine.has_errors());
    return program;
}

// `y = callee(y, x); if (x == y) return y; else return y;` - the call belongs
// to the declaration, not to either arm.
o::Program anchored() {
    auto callee = first();
    callee.id = v::FunctionId{0};
    CPPL_CHECK(callee.contract.has_value());
    callee.contract->preconditions.push_back(equality(parameter(0), parameter(1)));
    auto caller = first();
    caller.id = v::FunctionId{1};
    caller.symbol = v::SymbolId{"anchor"};
    caller.qualified_name = "anchor";
    caller.contract = v::Contract{.preconditions = {equality(parameter(0), parameter(1))},
                                  .postcondition = equality(parameter(2), parameter(1))};
    v::Expr call;
    call.id = v::ExprId{1};
    call.type = vUnsigned;
    call.node = v::Call{callee.symbol, callee.qualified_name, {parameter(1), parameter(0)}};
    v::Expr branch;
    branch.id = v::ExprId{2};
    branch.type = vUnsigned;
    branch.node = v::Conditional{{equality(parameter(0), parameter(1)), local(0), local(0)}};
    caller.returned_value = versioned(0, std::move(call), std::move(branch));
    cppl::elaboration::Result elaborated;
    elaborated.module.functions = {std::move(caller), std::move(callee)};
    cppl::diagnostics::Engine engine;
    auto program = o::generate(elaborated.module, elaborated, engine);
    CPPL_CHECK(!engine.has_errors());
    return program;
}

} // namespace

CPPL_TEST(a_return_observes_the_version_its_path_established) {
    const auto latest = assigned(1);
    cppl::diagnostics::Engine engine;
    for (const auto& result : cppl::automation::verify(latest, engine)) {
        CPPL_CHECK(result.verdict.is_proven());
    }
    CPPL_CHECK(!engine.has_errors());

    const auto stale = assigned(0);
    CPPL_CHECK(!(latest.obligations.front().id == stale.obligations.front().id));
    cppl::diagnostics::Engine stale_engine;
    const auto results = cppl::automation::verify(stale, stale_engine);
    CPPL_CHECK(!results.front().verdict.is_proven());
}

CPPL_TEST(a_call_bound_to_a_local_is_proven_before_the_guards_that_follow_it) {
    const auto program = anchored();
    const auto& caller = program.contracts.back();
    CPPL_CHECK_EQ(caller.paths.size(), std::size_t{2});
    for (const auto& path : caller.paths) {
        CPPL_CHECK_EQ(path.conditions.size(), std::size_t{1});
        CPPL_CHECK_EQ(path.calls.size(), std::size_t{1});
        // No condition is in scope where the call is made, so no guard can
        // justify its precondition.
        CPPL_CHECK_EQ(path.calls.front().conditions, std::size_t{0});
    }
    cppl::diagnostics::Engine engine;
    for (const auto& result : cppl::automation::verify(program, engine)) {
        CPPL_CHECK(result.verdict.is_proven());
    }
    CPPL_CHECK(!engine.has_errors());
}

// Malformed VIR in which the false arm reads a version only the true arm
// established: `if (x == y) { v0 = 0; return v0; } return v0;`.
CPPL_TEST(a_version_never_escapes_the_arm_that_established_it) {
    auto function = first();
    CPPL_CHECK(function.returned_value.has_value());
    function.returned_value->node =
        v::Conditional{{equality(parameter(0), parameter(1)), versioned(0, number(0), local(0)), local(0)}};
    cppl::elaboration::Result elaborated;
    elaborated.module.functions.push_back(std::move(function));
    cppl::diagnostics::Engine engine;
    const auto program = o::generate(elaborated.module, elaborated, engine);
    CPPL_CHECK(engine.has_errors());
    CPPL_CHECK(program.contracts.empty());
}

// A version bound twice on one path is malformed, not a reassignment.
CPPL_TEST(a_version_is_bound_once) {
    auto function = first();
    function.returned_value = versioned(0, parameter(0), versioned(0, number(0), local(0)));
    cppl::elaboration::Result elaborated;
    elaborated.module.functions.push_back(std::move(function));
    cppl::diagnostics::Engine engine;
    const auto program = o::generate(elaborated.module, elaborated, engine);
    CPPL_CHECK(engine.has_errors());
    CPPL_CHECK(program.contracts.empty());
}

// A value that reads its own version, directly or through a later one, is a
// cycle; it is refused instead of replayed without end.
CPPL_TEST(a_version_cannot_read_itself) {
    for (auto body : {versioned(0, local(0), local(0)), versioned(1, number(0), versioned(0, local(1), local(0)))}) {
        auto function = first();
        function.returned_value = std::move(body);
        cppl::elaboration::Result elaborated;
        elaborated.module.functions.push_back(std::move(function));
        cppl::diagnostics::Engine engine;
        const auto program = o::generate(elaborated.module, elaborated, engine);
        CPPL_CHECK(engine.has_errors());
        CPPL_CHECK(program.contracts.empty());
    }
}

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
    CPPL_CHECK(k::check(program.context, obligation.goal, o::automatic_evidence(obligation.goal), {}).has_value());
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
                           *implies.premise, k::ProofTerm::equality_elimination(
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

CPPL_TEST(call_composition_preserves_parameter_and_result_scope) {
    const auto program = composed();
    const auto& caller = program.contracts.back();
    CPPL_CHECK(caller.function == v::FunctionId{1});
    CPPL_CHECK(caller.paths.front().reasoning_goal == abstract_caller(true));
    const auto x = k::Term::variable(k::VarIndex{1});
    const auto y = k::Term::variable(k::VarIndex{0});
    const auto expected = k::Proposition::for_all(
        kUnsigned,
        k::Proposition::for_all(kUnsigned, k::Proposition::implication(k::Proposition::equality(kUnsigned, x, y),
                                                                       k::Proposition::equality(kUnsigned, y, x))));
    CPPL_CHECK(program.obligations[1].origin == o::Origin::CallPrecondition);
    CPPL_CHECK(program.obligations[1].goal == expected);
    cppl::diagnostics::Engine engine;
    for (const auto& result : cppl::automation::verify(program, engine)) {
        CPPL_CHECK(result.verdict.is_proven());
    }
    CPPL_CHECK(!engine.has_errors());
}

CPPL_TEST(caller_cannot_replace_missing_summary_evidence_with_body_unfolding) {
    auto program = composed();
    const auto& goal = program.obligations.back().goal;
    CPPL_CHECK(k::check(program.context, goal, o::automatic_evidence(goal), {}).has_value());
    program.contracts.back().paths.front().reasoning_goal = abstract_caller(false);
    cppl::diagnostics::Engine engine;
    const auto results = cppl::automation::verify(program, engine);
    CPPL_CHECK(!results.back().verdict.is_proven());
    CPPL_CHECK(engine.has_errors());
}

CPPL_TEST(a_proved_abstract_goal_cannot_import_a_forged_callee_summary) {
    auto program = composed();
    program.contracts.back().paths.front().reasoning_goal = abstract_caller(true, true);
    const auto& goal = program.contracts.back().paths.front().reasoning_goal;
    CPPL_CHECK(k::check(program.context, goal, o::automatic_evidence(goal), {}).has_value());
    cppl::diagnostics::Engine engine;
    const auto results = cppl::automation::verify(program, engine);
    CPPL_CHECK(!results.back().verdict.is_proven());
    CPPL_CHECK(results.back().verdict.reason().find("required premise") != std::string::npos);
}

CPPL_TEST(exported_contract_must_be_linked_to_the_actual_definition) {
    auto program = composed();
    program.contracts.front().returned_value = k::Term::literal(kUnsigned.integer_type(), 0);
    cppl::diagnostics::Engine engine;
    const auto results = cppl::automation::verify(program, engine);
    CPPL_CHECK(!results.front().verdict.is_proven());
    CPPL_CHECK(results.front().verdict.reason().find("contract linkage") != std::string::npos);
    CPPL_CHECK(!results.back().verdict.is_proven());
    CPPL_CHECK(results.back().verdict.reason().find("callee") != std::string::npos);
}

// Call precondition safety: unproven premises cannot authorize a callee
// summary.
//
// The call-site precondition is what the caller owes the callee, and the
// callee's postcondition holds only where that precondition did. A caller that
// does not establish it must not be able to reason from the summary.
CPPL_TEST(a_caller_that_does_not_establish_a_precondition_cannot_use_the_summary) {
    auto program = composed();
    // The caller's own precondition is what discharges the one it owes the
    // callee. Dropping it leaves the call-site obligation unprovable, so the
    // callee's postcondition is not the caller's to use.
    program.contracts.back().preconditions.clear();
    cppl::diagnostics::Engine engine;
    const auto results = cppl::automation::verify(program, engine);
    CPPL_CHECK(!results.back().verdict.is_proven());
    CPPL_CHECK(results.back().verdict.reason().find("required premise") != std::string::npos);
}

// Call precondition progress: an unproven precondition cannot cause the search
// to revisit the same unresolved state indefinitely.
//
// Safety and progress are separate requirements, and both are stated here. The
// test above says an unproven premise cannot authorize a summary; this one says
// that reaching for one cannot cost unbounded work. It is measured in search
// transitions rather than seconds, so the bound means the same thing on every
// machine: a valid program resolves well inside a budget derived from its own
// size, and never approaches it.
CPPL_TEST(composing_a_valid_program_completes_well_inside_its_transition_budget) {
    const auto program = composed();
    std::size_t transitions = 0;
    cppl::diagnostics::Engine engine;
    const auto results = cppl::automation::verify(program, engine, &transitions);
    for (const auto& result : results) {
        CPPL_CHECK(result.verdict.is_proven());
    }
    CPPL_CHECK(!engine.has_errors());
    CPPL_CHECK(transitions > 0);
    CPPL_CHECK(transitions < cppl::automation::Composition::budget_for(program));
    // Far below the bound, not merely under it: the budget exists to stop a
    // search that is not progressing, never to constrain one that is.
    CPPL_CHECK(transitions < 64);
}

// The dependency invariant, at the boundary that enforces it. A transition is
// charged against the dependency whose evidence it is about to read, so an
// unproven one stops the search there rather than reading evidence that was
// never established. This is what makes removing every enforcement of the
// invariant -- not only the gate above it -- a caught mutation.
CPPL_TEST(an_unproven_dependency_is_refused_before_its_evidence_is_read) {
    const auto program = composed();
    const cppl::automation::Composition composition(program);
    // Nothing has been accepted, so no obligation is proven yet. Charging a
    // transition against one must refuse rather than admit the read.
    const auto refused = composition.spend(program.obligations.size() - 1);
    CPPL_CHECK(!refused.has_value());
    CPPL_CHECK(refused.error().find("not proven") != std::string::npos);

    // A transition that names no dependency is ordinary progress, and is spent
    // without complaint until the budget itself runs out.
    CPPL_CHECK(composition.spend().has_value());
}

CPPL_TEST(caller_identity_includes_summary_changes_when_the_body_goal_is_unchanged) {
    const auto strong = composed();
    const auto repeat = composed();
    const auto weak = composed(true);
    CPPL_CHECK(strong.obligations.back().goal == weak.obligations.back().goal);
    CPPL_CHECK(strong.obligations.back().id == repeat.obligations.back().id);
    CPPL_CHECK(!(strong.obligations.back().id == weak.obligations.back().id));
    cppl::diagnostics::Engine engine;
    const auto results = cppl::automation::verify(weak, engine);
    CPPL_CHECK(!results.back().verdict.is_proven());
}

CPPL_TEST(branch_paths_are_independently_proven_before_export) {
    const auto program = branching();
    cppl::diagnostics::Engine engine;
    const auto results = cppl::automation::verify(program, engine);
    for (const auto& result : results)
        CPPL_CHECK(result.verdict.is_proven());
    CPPL_CHECK(!engine.has_errors());
    CPPL_CHECK(program.obligations[0].origin == o::Origin::ReturnPath);
    CPPL_CHECK(program.obligations[1].origin == o::Origin::ReturnPath);
    CPPL_CHECK(program.obligations[2].origin == o::Origin::FunctionContract);
    CPPL_CHECK(!(program.obligations[0].id == program.obligations[1].id));
}

CPPL_TEST(a_missing_path_cannot_export_a_contract) {
    auto program = branching();
    program.contracts.front().paths.pop_back();
    cppl::diagnostics::Engine engine;
    const auto results = cppl::automation::verify(program, engine);
    CPPL_CHECK(!results.back().verdict.is_proven());
}

CPPL_TEST(a_path_cannot_borrow_the_other_arms_premise) {
    auto program = branching();
    auto& paths = program.contracts.front().paths;
    paths[1].reasoning_goal = paths[0].reasoning_goal;
    cppl::diagnostics::Engine engine;
    const auto results = cppl::automation::verify(program, engine);
    CPPL_CHECK(!results[1].verdict.is_proven());
    CPPL_CHECK(!results.back().verdict.is_proven());
}

CPPL_TEST(a_changed_conditional_body_cannot_reuse_assembled_evidence) {
    auto program = branching();
    auto& body = std::get<k::Prim>(program.contracts.front().returned_value.node);
    body.arguments[1] = k::Term::literal(kUnsigned.integer_type(), 0);
    cppl::diagnostics::Engine engine;
    const auto results = cppl::automation::verify(program, engine);
    CPPL_CHECK(!results.back().verdict.is_proven());
}

namespace {

v::Expr compare(v::BinaryOp op, v::Expr left, v::Expr right) {
    v::Expr expression;
    expression.type = v::Type::boolean();
    expression.node = v::Binary{op, {std::move(left), std::move(right)}};
    return expression;
}

v::Expr control(decltype(v::Expr::node) node) {
    v::Expr expression;
    expression.type = vUnsigned;
    expression.node = std::move(node);
    return expression;
}

// count(n) ensures (result == n) { unsigned i = 0u; while (i < n) invariant (I) { i = i + 1u; } return i; }
v::Function counting(v::Expr invariant, std::uint32_t loop_id = 0) {
    v::Function function;
    function.id = v::FunctionId{0};
    function.symbol = v::SymbolId{"count"};
    function.qualified_name = "count";
    function.parameters = {{"n", vUnsigned}};
    function.result = vUnsigned;
    v::Expr step;
    step.type = vUnsigned;
    step.node = v::Binary{v::BinaryOp::Add, {local(1), number(1)}};
    auto iteration =
        control(v::PlaceVersion{2, named("i"), {std::move(step), control(v::Iterate{loop_id, {local(2)}})}, {}});
    auto head =
        control(v::Conditional{{compare(v::BinaryOp::Less, local(1), parameter(0)), std::move(iteration), local(1)}});
    auto loop = control(v::Loop{0, {1}, {named("i")}, 1, 0, {local(0), std::move(invariant), std::move(head)}});
    function.returned_value = control(v::PlaceVersion{0, named("i"), {number(0), std::move(loop)}, {}});
    function.contract = v::Contract{.postcondition = equality(parameter(1), parameter(0))};
    return function;
}

o::Program generate_all(std::vector<v::Function> functions, bool errors = false) {
    cppl::elaboration::Result elaborated;
    elaborated.module.functions = std::move(functions);
    cppl::diagnostics::Engine engine;
    auto program = o::generate(elaborated.module, elaborated, engine);
    CPPL_CHECK_EQ(engine.has_errors(), errors);
    return program;
}

} // namespace

CPPL_TEST(a_loop_yields_entry_preservation_and_exit_conditions) {
    const auto program = generate_all({counting(compare(v::BinaryOp::LessEqual, local(1), parameter(0)))});
    CPPL_CHECK_EQ(program.contracts.size(), std::size_t{1});
    CPPL_CHECK(program.contracts.front().partial);
    CPPL_CHECK_EQ(program.obligations.size(), std::size_t{3});
    CPPL_CHECK(program.obligations[0].origin == o::Origin::LoopEntry);
    CPPL_CHECK(program.obligations[1].origin == o::Origin::LoopPreservation);
    CPPL_CHECK(program.obligations[2].origin == o::Origin::ReturnPath);
    cppl::diagnostics::Engine engine;
    for (const auto& result : cppl::automation::verify(program, engine)) {
        CPPL_CHECK(result.verdict.is_proven());
    }
}

CPPL_TEST(a_function_with_a_loop_is_never_a_core_definition) {
    const auto program = generate_all({counting(compare(v::BinaryOp::LessEqual, local(1), parameter(0)))});
    CPPL_CHECK_EQ(program.context.definition_count(), std::size_t{0});
}

CPPL_TEST(an_invariant_that_does_not_hold_on_entry_leaves_the_contract_unproven) {
    auto callee = counting(compare(v::BinaryOp::Equal, local(1), parameter(0)));
    auto caller = first();
    caller.id = v::FunctionId{1};
    caller.symbol = v::SymbolId{"caller"};
    caller.qualified_name = "caller";
    caller.contract = v::Contract{.postcondition = equality(parameter(2), parameter(0))};
    v::Expr call;
    call.id = v::ExprId{9};
    call.type = vUnsigned;
    call.node = v::Call{callee.symbol, "count", {parameter(0)}};
    caller.returned_value = std::move(call);
    const auto program = generate_all({std::move(caller), std::move(callee)});
    CPPL_CHECK_EQ(program.contracts.size(), std::size_t{2});
    CPPL_CHECK(program.contracts[1].partial); // a caller of a partial contract is partial too
    cppl::diagnostics::Engine engine;
    const auto results = cppl::automation::verify(program, engine);
    CPPL_CHECK(!results[0].verdict.is_proven()); // i == n does not hold for i = 0
    CPPL_CHECK(!results.back().verdict.is_proven());
    CPPL_CHECK(results.back().verdict.reason().find("is not proven") != std::string::npos);
}

CPPL_TEST(an_iteration_outside_its_loop_is_refused) {
    const auto program = generate_all({counting(compare(v::BinaryOp::LessEqual, local(1), parameter(0)), 7)}, true);
    CPPL_CHECK(program.contracts.empty());
    CPPL_CHECK(program.obligations.empty());
}

CPPL_TEST(a_head_version_is_not_readable_before_its_loop) {
    auto function = counting(compare(v::BinaryOp::LessEqual, local(1), parameter(0)));
    CPPL_CHECK(function.returned_value.has_value());
    auto& first_version = std::get<v::PlaceVersion>(function.returned_value->node);
    first_version.operands[0] = local(1);
    const auto program = generate_all({std::move(function)}, true);
    CPPL_CHECK(program.contracts.empty());
}

CPPL_TEST(a_loop_rebinding_a_live_version_is_refused) {
    auto function = counting(compare(v::BinaryOp::LessEqual, local(1), parameter(0)));
    CPPL_CHECK(function.returned_value.has_value());
    auto& first_version = std::get<v::PlaceVersion>(function.returned_value->node);
    std::get<v::Loop>(first_version.operands[1].node).heads = {0};
    const auto program = generate_all({std::move(function)}, true);
    CPPL_CHECK(program.contracts.empty());
}

CPPL_TEST(unknown_refinement_metadata_cannot_drop_a_local_obligation) {
    auto function = counting(compare(v::BinaryOp::LessEqual, local(1), parameter(0)));
    CPPL_CHECK(function.returned_value.has_value());
    auto& first_version = std::get<v::PlaceVersion>(function.returned_value->node);
    first_version.declared = vUnsigned;
    first_version.declared.refinements.push_back({"missing", {}});
    const auto program = generate_all({std::move(function)}, true);
    CPPL_CHECK(program.contracts.empty());
    CPPL_CHECK(program.obligations.empty());
}

CPPL_TEST(unknown_refinement_metadata_cannot_drop_a_parameter_or_result_obligation) {
    for (const bool result : {false, true}) {
        auto function = first();
        auto& type = result ? function.result : function.parameters.front().type;
        type.refinements.push_back({"missing", {}});
        const auto program = generate_all({std::move(function)}, true);
        CPPL_CHECK(program.contracts.empty());
        CPPL_CHECK(program.obligations.empty());
    }
}

CPPL_TEST(mismatched_refinement_indices_fail_closed) {
    cppl::elaboration::Result elaborated;
    v::RefinementDeclaration refinement;
    refinement.name = "Index";
    refinement.base = vUnsigned;
    refinement.indices = {{"n", vUnsigned}};
    refinement.predicate = compare(v::BinaryOp::Less, parameter(1), parameter(0));
    elaborated.module.refinements.push_back(std::move(refinement));
    auto function = first();
    function.result.refinements.push_back({"Index", {}});
    elaborated.module.functions.push_back(std::move(function));
    cppl::diagnostics::Engine engine;
    const auto program = o::generate(elaborated.module, elaborated, engine);
    CPPL_CHECK(engine.has_errors());
    CPPL_CHECK(program.contracts.empty());
    CPPL_CHECK(program.obligations.empty());
}

CPPL_TEST(post_state_requires_all_parameters_and_the_return_value) {
    for (const auto count : {0u, 1u, 2u, 4u}) {
        auto function = first();
        v::Expr state;
        state.type = vUnsigned;
        state.node = v::ReturnState{std::vector<v::Expr>(count, parameter(0))};
        function.returned_value = std::move(state);
        cppl::elaboration::Result elaborated;
        elaborated.module.functions.push_back(std::move(function));
        cppl::diagnostics::Engine engine;
        const auto program = o::generate(elaborated.module, elaborated, engine);
        CPPL_CHECK(engine.has_errors());
        CPPL_CHECK(program.contracts.empty());
    }
}

CPPL_TEST(an_unknown_version_does_not_inherit_the_old_value) {
    auto function = first();
    CPPL_CHECK(function.contract.has_value());
    function.contract->postcondition = equality(parameter(2), number(1));
    v::Expr unknown;
    unknown.type = vUnsigned;
    unknown.node = v::UnknownVersion{1, named(), vUnsigned, {local(1)}};
    function.returned_value = versioned(0, number(1), std::move(unknown));
    auto program = generate(std::move(function));
    CPPL_CHECK(program.contracts.front().partial);
    cppl::diagnostics::Engine engine;
    const auto results = cppl::automation::verify(program, engine);
    CPPL_CHECK(!results.front().verdict.is_proven());
}

CPPL_TEST(an_unknown_version_cannot_rebind_a_previous_version) {
    auto function = first();
    v::Expr unknown;
    unknown.type = vUnsigned;
    unknown.node = v::UnknownVersion{0, named(), vUnsigned, {local(0)}};
    function.returned_value = versioned(0, number(1), std::move(unknown));
    cppl::elaboration::Result elaborated;
    elaborated.module.functions.push_back(std::move(function));
    cppl::diagnostics::Engine engine;
    const auto program = o::generate(elaborated.module, elaborated, engine);
    CPPL_CHECK(engine.has_errors());
    CPPL_CHECK(program.contracts.empty());
}
