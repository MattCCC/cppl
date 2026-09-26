// Contracts of other translation units, in the obligation layer (SPEC.md
// TUBOUND-003, TUBOUND-004, TUBOUND-007, TUBOUND-008; RFC 0017).
//
// These cases build VIR directly, so they reach what no build that writes its
// interfaces honestly produces: an entry claiming that a function of this unit
// is among what another unit's proof rested on, which would make recursion
// cross the boundary. Every accepted case is paired with the one change that
// refuses it.

#include "cppl/artifact/interface.hpp"
#include "cppl/automation/evidence.hpp"
#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/elaboration/elaborate.hpp"
#include "cppl/obligations/contracts.hpp"
#include "cppl/obligations/generate.hpp"
#include "cppl/obligations/interface.hpp"
#include "cppl/obligations/obligation.hpp"
#include "cppl/obligations/status.hpp"
#include "cppl/obligations/trust.hpp"
#include "cppl/source/digest.hpp"
#include "cppl/source/representation.hpp"
#include "cppl/source/storage.hpp"
#include "cppl/testing/test.hpp"
#include "cppl/vir/expr.hpp"
#include "cppl/vir/ids.hpp"
#include "cppl/vir/module.hpp"
#include "cppl/vir/types.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace v = cppl::vir;
namespace o = cppl::obligations;
namespace artifact = cppl::artifact;

const auto kUnsigned = v::Type::integer(32, false);

v::Expr parameter(std::uint32_t position, std::string name) {
    v::Expr expression;
    expression.type = kUnsigned;
    expression.node = v::ParameterRef{position, std::move(name)};
    return expression;
}

v::Expr literal(std::int64_t value) {
    v::Expr expression;
    expression.type = kUnsigned;
    expression.node = v::IntLiteral{value};
    return expression;
}

v::Expr less(v::Expr left, v::Expr right) {
    v::Expr expression;
    expression.type = v::Type::boolean();
    expression.node = v::Binary{v::BinaryOp::Less, {std::move(left), std::move(right)}};
    return expression;
}

v::Expr call(const std::string& callee, v::Expr argument, std::uint32_t id) {
    v::Expr expression;
    expression.id = v::ExprId{id};
    expression.type = kUnsigned;
    expression.node = v::Call{v::SymbolId{callee}, callee, {std::move(argument)}};
    return expression;
}

struct Stated {
    std::string parameter = "x";
    std::int64_t bound = 4;
    std::int64_t requires_below = 100;
    bool second_precondition = false;
    cppl::source::ParameterPassing passing = cppl::source::ParameterPassing::Value;
    bool measured = false;      // states `decreases (x)`, asking that it terminate
    bool other_measure = false; // states `decreases (9u)` instead
};

// `expects (x < 100u) ensures (result < 4u)`, as elaboration would read it.
v::Contract stated_contract(const Stated& stated) {
    v::Contract contract;
    contract.preconditions.push_back(less(parameter(0, stated.parameter), literal(stated.requires_below)));
    if (stated.second_precondition) {
        contract.preconditions.push_back(less(parameter(0, stated.parameter), literal(50)));
    }
    contract.postcondition = less(parameter(1, "result"), literal(stated.bound));
    if (stated.measured) {
        contract.measures.push_back(stated.other_measure ? literal(9) : parameter(0, stated.parameter));
    }
    return contract;
}

// `unsigned name(unsigned x)` with that contract, with the body `returns` when
// it has one.
v::Function function(std::uint32_t id, const std::string& symbol, std::optional<v::Expr> returns,
                     const Stated& stated = {}) {
    v::Function declared;
    declared.id = v::FunctionId{id};
    declared.symbol = v::SymbolId{symbol};
    declared.qualified_name = symbol;
    declared.parameters = {{stated.parameter, kUnsigned, stated.passing}};
    declared.result = kUnsigned;
    declared.external_linkage = true;
    declared.contract = stated_contract(stated);
    declared.defined_elsewhere = !returns.has_value();
    declared.returned_value = std::move(returns);
    return declared;
}

struct Generated {
    o::Program program;
    cppl::diagnostics::Engine engine;
};

Generated generate(std::vector<v::Function> functions, const o::Imports& imports = {}) {
    Generated generated;
    cppl::elaboration::Result elaborated;
    elaborated.module.functions = std::move(functions);
    generated.program = o::generate(elaborated.module, elaborated, generated.engine, imports);
    return generated;
}

const o::ContractVerification* contract_of(const o::Program& program, std::string_view symbol) {
    const auto found = std::ranges::find(program.contracts, symbol, &o::ContractVerification::symbol);
    return found == program.contracts.end() ? nullptr : &*found;
}

// What this unit states for a contract, as its interface would record it.
cppl::source::Digest statement_of(const Stated& stated) {
    Generated generated = generate({function(0, "c:@F@probe#i#", literal(0), stated)});
    CPPL_CHECK(!generated.engine.has_errors());
    const o::ContractVerification* contract = contract_of(generated.program, "c:@F@probe#i#");
    CPPL_CHECK(contract != nullptr);
    CPPL_CHECK(contract->statement.has_value());
    return contract->statement.value();
}

bool mentions(const cppl::diagnostics::Engine& engine, std::string_view text) {
    return std::ranges::any_of(engine.diagnostics(), [text](const cppl::diagnostics::Diagnostic& diagnostic) {
        return diagnostic.message.find(text) != std::string::npos;
    });
}

o::ImportedEntry record(const std::string& symbol, cppl::source::Digest statement, artifact::Correctness correctness,
                        std::vector<artifact::Dependency> depends = {}, std::vector<artifact::Model> models = {}) {
    artifact::Entry entry;
    entry.symbol = symbol;
    entry.name = symbol;
    entry.statement = statement;
    entry.contract = "recorded";
    entry.correctness = correctness;
    entry.depends = std::move(depends);
    entry.models = std::move(models);
    return o::ImportedEntry{"other.cppli", entry, artifact::identify(entry)};
}

o::Imports recording(const std::string& symbol, cppl::source::Digest statement, artifact::Correctness correctness,
                     std::vector<artifact::Dependency> depends = {}, std::vector<artifact::Model> models = {}) {
    o::Imports imports;
    imports.entries.push_back(record(symbol, statement, correctness, std::move(depends), std::move(models)));
    return imports;
}

// A dependency on another record, by that record's result identity.
artifact::Dependency on(const std::string& symbol) {
    return artifact::Dependency{symbol, cppl::source::hash_bytes(symbol)};
}

bool refused_for_recursion(const cppl::diagnostics::Engine& engine) {
    return std::ranges::any_of(engine.diagnostics(), [](const cppl::diagnostics::Diagnostic& diagnostic) {
        return std::ranges::any_of(diagnostic.notes, [](const auto& note) {
            return note.message.find("recursion across translation units is not verified") != std::string::npos;
        });
    });
}

} // namespace

// SPEC: TUBOUND-004
CPPL_TEST(a_statement_is_identified_by_meaning_not_by_name) {
    const cppl::source::Digest base = statement_of({});
    CPPL_CHECK(statement_of({.parameter = "renamed"}) == base);
    CPPL_CHECK(!(statement_of({.bound = 5}) == base));
    CPPL_CHECK(!(statement_of({.requires_below = 101}) == base));
    CPPL_CHECK(!(statement_of({.second_precondition = true}) == base));
    CPPL_CHECK(!(statement_of({.passing = cppl::source::ParameterPassing::ConstReference}) == base));
    // Asking to terminate is part of the contract; which measure the proving
    // unit used is not.
    CPPL_CHECK(!(statement_of({.measured = true}) == base));
    CPPL_CHECK(statement_of({.measured = true, .other_measure = true}) == statement_of({.measured = true}));
}

// SPEC: TUBOUND-004, TUBOUND-007
CPPL_TEST(a_total_contract_is_matched_whatever_measure_its_prover_used) {
    const std::string callee = "c:@F@callee#i#";
    const auto build = [&](const Stated& declared, const Stated& proven) {
        return generate({function(0, callee, std::nullopt, declared),
                         function(1, "c:@F@caller#i#", call(callee, parameter(0, "x"), 7))},
                        recording(callee, statement_of(proven), artifact::Correctness::Total));
    };
    Generated other_measure = build({.measured = true, .other_measure = true}, {.measured = true});
    CPPL_CHECK(!other_measure.engine.has_errors());
    CPPL_CHECK(contract_of(other_measure.program, callee) != nullptr);
    const o::ContractVerification* caller = contract_of(other_measure.program, "c:@F@caller#i#");
    CPPL_CHECK(caller != nullptr);
    CPPL_CHECK(caller->total);

    // Total against partial, in either direction, is two contracts.
    Generated asks_less = build({}, {.measured = true});
    CPPL_CHECK(mentions(asks_less.engine, "is not the one 'other.cppli' records as verified"));
    CPPL_CHECK(contract_of(asks_less.program, "c:@F@caller#i#") == nullptr);
    Generated asks_more = build({.measured = true}, {});
    CPPL_CHECK(mentions(asks_more.engine, "is not the one 'other.cppli' records as verified"));
    CPPL_CHECK(contract_of(asks_more.program, "c:@F@caller#i#") == nullptr);
}

// SPEC: TUBOUND-007, TERMINATION-006
CPPL_TEST(a_declaration_asking_to_terminate_is_never_matched_to_a_partial_record) {
    const std::string callee = "c:@F@callee#i#";
    const auto build = [&](artifact::Correctness correctness) {
        return generate({function(0, callee, std::nullopt, {.measured = true}),
                         function(1, "c:@F@caller#i#", call(callee, parameter(0, "x"), 7))},
                        recording(callee, statement_of({.measured = true}), correctness));
    };
    Generated total = build(artifact::Correctness::Total);
    CPPL_CHECK(!total.engine.has_errors());
    CPPL_CHECK(contract_of(total.program, callee) != nullptr);

    Generated partial = build(artifact::Correctness::Partial);
    CPPL_CHECK(mentions(partial.engine, "as partial correctness, but its declaration asks that it terminate"));
    CPPL_CHECK(contract_of(partial.program, callee) == nullptr);
    CPPL_CHECK(contract_of(partial.program, "c:@F@caller#i#") == nullptr);
}

// SPEC: TUBOUND-004
CPPL_TEST(a_function_with_internal_linkage_states_nothing_another_unit_can_match) {
    v::Function hidden = function(0, "c:@F@hidden#i#", literal(0));
    hidden.external_linkage = false;
    Generated generated = generate({std::move(hidden)});
    CPPL_CHECK(!generated.engine.has_errors());
    const o::ContractVerification* contract = contract_of(generated.program, "c:@F@hidden#i#");
    CPPL_CHECK(contract != nullptr);
    CPPL_CHECK(!contract->statement.has_value());
}

// SPEC: TUBOUND-003, TUBOUND-004
CPPL_TEST(a_caller_is_proven_from_a_recorded_contract_only_when_it_states_the_same_one) {
    const std::string callee = "c:@F@callee#i#";
    const auto build = [&](const o::Imports& imports) {
        return generate(
            {function(0, callee, std::nullopt), function(1, "c:@F@caller#i#", call(callee, parameter(0, "x"), 7))},
            imports);
    };

    Generated matched = build(recording(callee, statement_of({}), artifact::Correctness::Total));
    CPPL_CHECK(!matched.engine.has_errors());
    const o::ContractVerification* imported = contract_of(matched.program, callee);
    CPPL_CHECK(imported != nullptr);
    CPPL_CHECK(imported->imported.has_value());
    CPPL_CHECK(imported->total);
    CPPL_CHECK(imported->conditions.empty());
    CPPL_CHECK(contract_of(matched.program, "c:@F@caller#i#") != nullptr);
    cppl::diagnostics::Engine verified;
    for (const o::ObligationResult& result : cppl::automation::verify(matched.program, verified)) {
        CPPL_CHECK(result.verdict.is_proven());
    }

    // The same record, for a contract with a weaker postcondition than the one
    // this unit states: refused, and the caller has nothing to rest on.
    Generated mismatched = build(recording(callee, statement_of({.bound = 5}), artifact::Correctness::Total));
    CPPL_CHECK(mentions(mismatched.engine, "is not the one 'other.cppli' records as verified"));
    CPPL_CHECK(contract_of(mismatched.program, callee) == nullptr);
    CPPL_CHECK(contract_of(mismatched.program, "c:@F@caller#i#") == nullptr);

    Generated absent = build({});
    CPPL_CHECK(mentions(absent.engine, "no imported verification interface records its contract"));
    CPPL_CHECK(contract_of(absent.program, "c:@F@caller#i#") == nullptr);
}

// SPEC: TUBOUND-007
CPPL_TEST(a_recorded_partial_contract_makes_its_callers_partial) {
    const std::string callee = "c:@F@callee#i#";
    for (const auto correctness : {artifact::Correctness::Total, artifact::Correctness::Partial}) {
        Generated generated = generate(
            {function(0, callee, std::nullopt), function(1, "c:@F@caller#i#", call(callee, parameter(0, "x"), 7))},
            recording(callee, statement_of({}), correctness));
        CPPL_CHECK(!generated.engine.has_errors());
        const o::ContractVerification* caller = contract_of(generated.program, "c:@F@caller#i#");
        CPPL_CHECK(caller != nullptr);
        CPPL_CHECK_EQ(caller->total, correctness == artifact::Correctness::Total);
    }
}

// SPEC: TUBOUND-008
CPPL_TEST(recursion_through_another_unit_is_refused) {
    const std::string callee = "c:@F@elsewhere#i#";
    // f calls a function of another unit; h calls f. A record of that function
    // claiming it was proven through h closes the cycle f -> elsewhere -> h -> f.
    const auto build = [&](const std::string& through) {
        return generate({function(0, callee, std::nullopt),
                         function(1, "c:@F@f#i#", call(callee, parameter(0, "x"), 7)),
                         function(2, "c:@F@h#i#", call("c:@F@f#i#", parameter(0, "x"), 8)),
                         function(3, "c:@F@leaf#i#", literal(0))},
                        recording(callee, statement_of({}), artifact::Correctness::Total,
                                  {artifact::Dependency{through, cppl::source::hash_bytes("some entry")}}));
    };

    Generated cycle = build("c:@F@h#i#");
    CPPL_CHECK(refused_for_recursion(cycle.engine));
    CPPL_CHECK(mentions(cycle.engine, "'c:@F@f#i#' -> 'c:@F@elsewhere#i#' -> 'c:@F@h#i#' -> 'c:@F@f#i#'"));
    CPPL_CHECK(contract_of(cycle.program, "c:@F@f#i#") == nullptr);
    CPPL_CHECK(contract_of(cycle.program, "c:@F@h#i#") == nullptr);
    CPPL_CHECK(contract_of(cycle.program, callee) == nullptr);

    // The same record resting on a function of this unit that does not reach f.
    Generated acyclic = build("c:@F@leaf#i#");
    CPPL_CHECK(!acyclic.engine.has_errors());
    CPPL_CHECK(contract_of(acyclic.program, "c:@F@f#i#") != nullptr);
    CPPL_CHECK(contract_of(acyclic.program, "c:@F@h#i#") != nullptr);
}

// SPEC: TUBOUND-008
// A -> B -> C -> A: a function of this unit, and two contracts of two other
// units, one resting on the next, the last on the first. Refused whether B's
// record names only C, as a forged one might, or the whole chain, as an honest
// transitive one would.
CPPL_TEST(a_cycle_through_two_other_units_is_refused) {
    const std::string b = "c:@F@b#i#";
    const std::string c = "c:@F@c#i#";
    const auto build = [&](std::vector<artifact::Dependency> through_b) {
        o::Imports imports;
        imports.entries.push_back(record(b, statement_of({}), artifact::Correctness::Total, std::move(through_b)));
        imports.entries.push_back(record(c, statement_of({}), artifact::Correctness::Total, {on("c:@F@a#i#")}));
        return generate({function(0, b, std::nullopt), function(1, "c:@F@a#i#", call(b, parameter(0, "x"), 7))},
                        imports);
    };
    for (auto through_b : {std::vector{on(c)}, std::vector{on(c), on("c:@F@a#i#")}}) {
        Generated cycle = build(std::move(through_b));
        CPPL_CHECK(refused_for_recursion(cycle.engine));
        CPPL_CHECK(contract_of(cycle.program, "c:@F@a#i#") == nullptr);
        CPPL_CHECK(contract_of(cycle.program, b) == nullptr);
    }
}

// SPEC: TUBOUND-008
// B -> C -> B, two contracts of other units resting on each other, reached from
// this unit without this unit being on the cycle: B rests on a proof no unit
// could have made, and so does everything resting on B.
CPPL_TEST(a_cycle_among_other_units_alone_is_refused) {
    const std::string b = "c:@F@b#i#";
    const std::string c = "c:@F@c#i#";
    o::Imports imports;
    imports.entries.push_back(record(b, statement_of({}), artifact::Correctness::Total, {on(c)}));
    imports.entries.push_back(record(c, statement_of({}), artifact::Correctness::Total, {on(b)}));
    Generated cycle =
        generate({function(0, b, std::nullopt), function(1, "c:@F@a#i#", call(b, parameter(0, "x"), 7))}, imports);
    CPPL_CHECK(refused_for_recursion(cycle.engine));
    CPPL_CHECK(mentions(cycle.engine, "rests on a cycle of verified contracts across units"));
    CPPL_CHECK(contract_of(cycle.program, b) == nullptr);
    CPPL_CHECK(contract_of(cycle.program, "c:@F@a#i#") == nullptr);

    // B -> C, and C -> D -> C: B is on no cycle, and rests on one all the same.
    const std::string d = "c:@F@d#i#";
    o::Imports reaching;
    reaching.entries.push_back(record(b, statement_of({}), artifact::Correctness::Total, {on(c)}));
    reaching.entries.push_back(record(c, statement_of({}), artifact::Correctness::Total, {on(d)}));
    reaching.entries.push_back(record(d, statement_of({}), artifact::Correctness::Total, {on(c)}));
    Generated reached =
        generate({function(0, b, std::nullopt), function(1, "c:@F@a#i#", call(b, parameter(0, "x"), 7))}, reaching);
    CPPL_CHECK(mentions(reached.engine, "'c:@F@c#i#' -> 'c:@F@d#i#' -> 'c:@F@c#i#'"));
    CPPL_CHECK(contract_of(reached.program, b) == nullptr);
    CPPL_CHECK(contract_of(reached.program, "c:@F@a#i#") == nullptr);
}

// SPEC: TUBOUND-008
// A -> B -> D and A -> C -> D: two paths to one contract are not a cycle.
CPPL_TEST(a_diamond_of_contracts_across_units_is_accepted) {
    const std::string b = "c:@F@b#i#";
    const std::string c = "c:@F@c#i#";
    const std::string d = "c:@F@d#i#";
    o::Imports imports;
    imports.entries.push_back(record(b, statement_of({}), artifact::Correctness::Total, {on(d)}));
    imports.entries.push_back(record(c, statement_of({}), artifact::Correctness::Total, {on(d)}));
    imports.entries.push_back(record(d, statement_of({}), artifact::Correctness::Total));
    v::Function caller = function(3, "c:@F@a#i#", call(b, call(c, parameter(0, "x"), 8), 7));
    caller.contract = stated_contract({.requires_below = 4});
    Generated diamond = generate(
        {function(0, b, std::nullopt), function(1, c, std::nullopt), function(2, d, std::nullopt), std::move(caller)},
        imports);
    CPPL_CHECK(!diamond.engine.has_errors());
    CPPL_CHECK(!refused_for_recursion(diamond.engine));
    for (const std::string& symbol : {b, c, d, std::string("c:@F@a#i#")}) {
        CPPL_CHECK(contract_of(diamond.program, symbol) != nullptr);
    }
}

// A model another unit's proof rested on is not re-affirmed here: it arrives
// with the record, stays in the closure of every claim proven through it, and
// is recorded again for a unit further on. The pair differs only in whether the
// record names one.
//
// SPEC: TUBOUND-006, TUBOUND-002, STDMODEL-018
CPPL_TEST(a_model_a_recorded_contract_rests_on_reaches_every_claim_proven_through_it) {
    const std::string callee = "c:@F@callee#i#";
    const std::string caller = "c:@F@caller#i#";
    const artifact::Model model = o::library_model(cppl::source::RepresentationKind::Vector);
    CPPL_CHECK_EQ(model.name, std::string("std::vector model"));
    CPPL_CHECK(!(model.identity == o::library_model(cppl::source::RepresentationKind::Span).identity));

    const auto closed = [&](std::vector<artifact::Model> models) {
        Generated generated =
            generate({function(0, callee, std::nullopt), function(1, caller, call(callee, parameter(0, "x"), 7))},
                     recording(callee, statement_of({}), artifact::Correctness::Total, {}, std::move(models)));
        CPPL_CHECK(!generated.engine.has_errors());
        cppl::diagnostics::Engine verified;
        const std::vector<o::ObligationResult> results = cppl::automation::verify(generated.program, verified);
        const o::TrustClosure closure =
            o::close_trust(generated.program, results, cppl::automation::classify_crossings(generated.program));
        CPPL_CHECK(closure.faults.empty());
        const auto claim = std::ranges::find(closure.claims, caller, &o::ClaimClosure::symbol);
        CPPL_CHECK(claim != closure.claims.end());
        const std::vector<artifact::Entry> exported = o::exported_contracts(generated.program, closure);
        const auto entry = std::ranges::find(exported, caller, &artifact::Entry::symbol);
        CPPL_CHECK(entry != exported.end());
        return std::pair{o::rests_on_library_models(*claim), entry->models};
    };

    const auto [rests, recorded] = closed({model});
    CPPL_CHECK(rests);
    CPPL_CHECK(recorded == std::vector<artifact::Model>{model});

    const auto [rests_without, recorded_without] = closed({});
    CPPL_CHECK(!rests_without);
    CPPL_CHECK(recorded_without.empty());
}

// A model this unit's own body used is recorded for the contract that used it,
// and for no other.
//
// SPEC: TUBOUND-002, STDMODEL-018
CPPL_TEST(a_model_a_body_uses_is_recorded_with_its_contract) {
    v::Function uses = function(0, "c:@F@uses#i#", literal(0));
    uses.library_models = {cppl::source::RepresentationKind::String};
    Generated generated = generate({std::move(uses), function(1, "c:@F@plain#i#", literal(0))});
    CPPL_CHECK(!generated.engine.has_errors());
    cppl::diagnostics::Engine verified;
    const std::vector<o::ObligationResult> results = cppl::automation::verify(generated.program, verified);
    const o::TrustClosure closure =
        o::close_trust(generated.program, results, cppl::automation::classify_crossings(generated.program));
    const std::vector<artifact::Entry> exported = o::exported_contracts(generated.program, closure);
    const auto uses_entry = std::ranges::find(exported, std::string("c:@F@uses#i#"), &artifact::Entry::symbol);
    const auto plain_entry = std::ranges::find(exported, std::string("c:@F@plain#i#"), &artifact::Entry::symbol);
    CPPL_CHECK(uses_entry != exported.end() && plain_entry != exported.end());
    CPPL_CHECK(uses_entry->models ==
               std::vector<artifact::Model>{o::library_model(cppl::source::RepresentationKind::String)});
    CPPL_CHECK(plain_entry->models.empty());
}

// SPEC: TU-003
CPPL_TEST(a_restated_contract_must_mean_the_same) {
    v::Function same = function(0, "c:@F@same#i#", literal(0));
    same.redeclared_contracts.push_back(stated_contract({.parameter = "renamed"}));
    Generated agreeing = generate({std::move(same)});
    CPPL_CHECK(!agreeing.engine.has_errors());
    CPPL_CHECK(contract_of(agreeing.program, "c:@F@same#i#") != nullptr);

    v::Function conflicting = function(0, "c:@F@same#i#", literal(0));
    conflicting.redeclared_contracts.push_back(stated_contract({.bound = 5}));
    Generated refused = generate({std::move(conflicting)});
    CPPL_CHECK(mentions(refused.engine, "states a different contract from its earlier one"));
    CPPL_CHECK(contract_of(refused.program, "c:@F@same#i#") == nullptr);

    // Within one unit the measure is compared too: it is what a recursive call
    // in this unit descends (TERMINATION-007).
    v::Function remeasured = function(0, "c:@F@same#i#", literal(0), {.measured = true});
    remeasured.redeclared_contracts.push_back(stated_contract({.measured = true, .other_measure = true}));
    Generated measure_refused = generate({std::move(remeasured)});
    CPPL_CHECK(mentions(measure_refused.engine, "states a different contract from its earlier one"));
    CPPL_CHECK(contract_of(measure_refused.program, "c:@F@same#i#") == nullptr);
}
