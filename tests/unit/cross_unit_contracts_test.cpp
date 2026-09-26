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
#include "cppl/source/digest.hpp"
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
    bool measured = false; // states `decreases (x)`, asking that it terminate
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
        contract.measures.push_back(parameter(0, stated.parameter));
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

o::Imports recording(const std::string& symbol, cppl::source::Digest statement, artifact::Correctness correctness,
                     std::vector<artifact::Dependency> depends = {}) {
    artifact::Entry entry;
    entry.symbol = symbol;
    entry.name = symbol;
    entry.statement = statement;
    entry.contract = "recorded";
    entry.correctness = correctness;
    entry.depends = std::move(depends);
    o::Imports imports;
    imports.entries.push_back(o::ImportedEntry{"other.cppli", entry, artifact::identify(entry)});
    return imports;
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
    CPPL_CHECK(!(statement_of({.measured = true}) == base));
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
    CPPL_CHECK(mentions(cycle.engine, "recursion across translation units is not verified") ||
               std::ranges::any_of(cycle.engine.diagnostics(), [](const auto& diagnostic) {
                   return std::ranges::any_of(diagnostic.notes, [](const auto& note) {
                       return note.message.find("recursion across translation units is not verified") !=
                              std::string::npos;
                   });
               }));
    CPPL_CHECK(contract_of(cycle.program, "c:@F@f#i#") == nullptr);

    // The same record resting on a function of this unit that does not reach f.
    Generated acyclic = build("c:@F@leaf#i#");
    CPPL_CHECK(!acyclic.engine.has_errors());
    CPPL_CHECK(contract_of(acyclic.program, "c:@F@f#i#") != nullptr);
    CPPL_CHECK(contract_of(acyclic.program, "c:@F@h#i#") != nullptr);
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
}
