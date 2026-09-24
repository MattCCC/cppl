// The trust closure of a proven claim is every trusted law its accepted
// evidence rests on, directly or through what it uses (TRUST.md 2.8, 35).
//
// These cases build the verified program directly, so propagation through
// verified calls is exercised for call graphs no source fixture produces, a
// recursive one included, and every way the closure can fail to be computed is
// shown to be reported rather than left out.

#include "cppl/kernel/check.hpp"
#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/obligations/contracts.hpp"
#include "cppl/obligations/obligation.hpp"
#include "cppl/obligations/status.hpp"
#include "cppl/obligations/trust.hpp"
#include "cppl/source/location.hpp"
#include "cppl/testing/test.hpp"
#include "cppl/vir/ids.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace k = cppl::kernel;
namespace o = cppl::obligations;

const k::IntType kSigned32{32, k::Signedness::Signed};

k::Type signed32() {
    return k::Type::integer(32, k::Signedness::Signed);
}

// A closed, reflexive proposition. Distinct values keep goals and premises
// apart, so an acceptance for one is never mistaken for another.
k::Proposition fact(std::int64_t value) {
    return k::Proposition::equality(signed32(), k::Term::literal(kSigned32, value), k::Term::literal(kSigned32, value));
}

struct Builder {
    o::Program program;
    std::vector<o::ObligationResult> results;

    // Declares trusted law `law`, which is assumed and never proven.
    o::TrustedPremise trusted(std::uint32_t law) {
        o::Obligation obligation;
        obligation.origin = o::Origin::LawProposition;
        obligation.subject = "assumed_" + std::to_string(law);
        obligation.law = cppl::vir::LawId{law};
        obligation.trusted = true;
        obligation.goal = fact(1000 + law);
        obligation.range.begin = cppl::source::SourceLocation{"unit.cpp", law + 1, 1};
        program.obligations.push_back(obligation);
        results.push_back(o::ObligationResult{obligation, o::Verdict::trusted("unit.cpp"), {}});
        return o::TrustedPremise{*obligation.law,        obligation.subject, obligation.id,
                                 obligation.range.begin, obligation.goal,    false};
    }

    // An obligation the kernel proves relative to `premises`, which the
    // evidence supposes and does not otherwise use.
    std::size_t proven(o::Origin origin, const std::string& subject, std::vector<o::TrustedPremise> premises = {}) {
        o::Obligation obligation;
        obligation.origin = origin;
        obligation.subject = subject;
        obligation.goal = fact(static_cast<std::int64_t>(program.obligations.size()));
        k::ProofTerm evidence = k::ProofTerm::reflexivity();
        for (const o::TrustedPremise& premise : std::views::reverse(premises)) {
            evidence = k::ProofTerm::implication_introduction(premise.proposition, std::move(evidence));
        }
        const auto accepted =
            k::check(program.context, o::relative_to(premises, obligation.goal), evidence, k::CoreLimits{});
        CPPL_CHECK(accepted.has_value());
        auto verdict = o::Verdict::proven(*accepted, obligation, std::move(premises));
        CPPL_CHECK(verdict.is_proven());
        program.obligations.push_back(obligation);
        results.push_back(o::ObligationResult{obligation, std::move(verdict), "test"});
        return program.obligations.size() - 1;
    }

    std::size_t unresolved(o::Origin origin, const std::string& subject) {
        o::Obligation obligation;
        obligation.origin = origin;
        obligation.subject = subject;
        obligation.goal = fact(static_cast<std::int64_t>(program.obligations.size()));
        program.obligations.push_back(obligation);
        results.push_back(o::ObligationResult{obligation, o::Verdict::unresolved("no evidence"), {}});
        return program.obligations.size() - 1;
    }

    // A total contract for function `function`, stated by obligation
    // `obligation`, whose one return path calls `callees`.
    void total_contract(std::uint32_t function, std::size_t obligation, const std::vector<std::uint32_t>& callees = {},
                        std::optional<std::size_t> path = std::nullopt) {
        o::ContractVerification contract;
        contract.function = cppl::vir::FunctionId{function};
        contract.name = "f" + std::to_string(function);
        contract.obligation = obligation;
        o::ReturnPath returned;
        returned.obligation = path.value_or(obligation);
        for (const std::uint32_t callee : callees) {
            o::CallVerification call;
            call.callee = cppl::vir::FunctionId{callee};
            call.callee_name = "f" + std::to_string(callee);
            returned.calls.push_back(call);
        }
        contract.paths.push_back(returned);
        program.contracts.push_back(contract);
    }

    // A partial-correctness contract whose conditions suppose `callees`, given
    // as contract indices.
    void partial_contract(std::uint32_t function, const std::vector<std::size_t>& conditions,
                          const std::vector<std::size_t>& callees = {}) {
        o::ContractVerification contract;
        contract.function = cppl::vir::FunctionId{function};
        contract.name = "f" + std::to_string(function);
        contract.partial = true;
        for (const std::size_t condition : conditions) {
            contract.conditions.push_back(o::VerificationCondition{condition, callees});
        }
        program.contracts.push_back(contract);
    }

    [[nodiscard]] o::TrustClosure close() const {
        return o::close_trust(program, results);
    }
};

o::TrustedPremise direct(o::TrustedPremise premise) {
    premise.direct = true;
    return premise;
}

const o::ClaimClosure* claim(const o::TrustClosure& closure, o::ClaimKind kind, const std::string& subject) {
    const auto found = std::ranges::find_if(closure.claims, [&](const o::ClaimClosure& candidate) {
        return candidate.kind == kind && candidate.subject == subject;
    });
    return found == closure.claims.end() ? nullptr : &*found;
}

std::vector<std::uint32_t> laws(const o::ClaimClosure* claim) {
    std::vector<std::uint32_t> result;
    if (claim != nullptr) {
        for (const auto& premise : claim->premises) {
            result.push_back(premise.law.value);
        }
    }
    return result;
}

bool faulted(const o::TrustClosure& closure, const std::string& fragment) {
    return std::ranges::any_of(closure.faults,
                               [&](const std::string& fault) { return fault.find(fragment) != std::string::npos; });
}

} // namespace

// SPEC: TRUSTED-002, PROOFSRC-005
CPPL_TEST(a_law_proven_relative_to_a_trusted_law_names_it) {
    Builder unit;
    const auto base = unit.trusted(0);
    unit.proven(o::Origin::LawProposition, "derived", {direct(base)});

    const auto closure = unit.close();

    CPPL_CHECK(closure.faults.empty());
    const auto* derived = claim(closure, o::ClaimKind::Law, "derived");
    CPPL_CHECK(derived != nullptr);
    CPPL_CHECK(laws(derived) == std::vector<std::uint32_t>{0});
    CPPL_CHECK(derived->premises.front().direct);
    CPPL_CHECK_EQ(derived->premises.front().name, std::string("assumed_0"));
    CPPL_CHECK_EQ(derived->premises.front().location.line, std::uint32_t{1});
}

// A build that assumes nothing reports nothing assumed (TRUST.md 3.2).
CPPL_TEST(a_claim_proven_outright_rests_on_nothing) {
    Builder unit;
    unit.proven(o::Origin::LawProposition, "outright");
    unit.proven(o::Origin::ProofProposition, "lemma");

    const auto closure = unit.close();

    CPPL_CHECK(closure.faults.empty());
    CPPL_CHECK(closure.assumptions.empty());
    CPPL_CHECK_EQ(closure.claims.size(), std::size_t{2});
    CPPL_CHECK(std::ranges::all_of(closure.claims, [](const auto& each) { return each.premises.empty(); }));
}

// A trusted law is itself TRUSTED, never a proven claim (TRUST.md TCB-REPORT-003),
// and one nothing rests on is still listed so it can be audited.
CPPL_TEST(every_trusted_law_is_listed_in_declaration_order_and_none_is_a_claim) {
    Builder unit;
    const auto later = unit.trusted(1);
    const auto earlier = unit.trusted(0);
    unit.proven(o::Origin::LawProposition, "uses_both", {direct(earlier), direct(later)});

    const auto closure = unit.close();

    CPPL_CHECK(closure.faults.empty());
    CPPL_CHECK_EQ(closure.assumptions.size(), std::size_t{2});
    CPPL_CHECK_EQ(closure.assumptions[0].law.value, std::uint32_t{0});
    CPPL_CHECK_EQ(closure.assumptions[1].law.value, std::uint32_t{1});
    CPPL_CHECK_EQ(closure.claims.size(), std::size_t{1});
    CPPL_CHECK(laws(claim(closure, o::ClaimKind::Law, "uses_both")) == (std::vector<std::uint32_t>{0, 1}));
}

CPPL_TEST(an_unproven_claim_has_no_closure) {
    Builder unit;
    unit.trusted(0);
    unit.unresolved(o::Origin::LawProposition, "open");

    const auto closure = unit.close();

    CPPL_CHECK(closure.faults.empty());
    CPPL_CHECK(closure.claims.empty());
}

// SPEC: CASE-012, CASE-016
CPPL_TEST(each_kind_of_claim_is_reported_under_its_own_kind) {
    Builder unit;
    const auto base = unit.trusted(0);
    unit.proven(o::Origin::LawProposition, "same", {base});
    unit.proven(o::Origin::ProofProposition, "same", {base});
    unit.proven(o::Origin::OmittedCase, "same", {base});
    unit.proven(o::Origin::ImpossiblePath, "same", {base});

    const auto closure = unit.close();

    CPPL_CHECK(closure.faults.empty());
    for (const auto kind :
         {o::ClaimKind::Law, o::ClaimKind::Proof, o::ClaimKind::OmittedCase, o::ClaimKind::ImpossiblePath}) {
        CPPL_CHECK(laws(claim(closure, kind, "same")) == std::vector<std::uint32_t>{0});
    }
}

// A proof of one instance of a law is checked where it is lowered and has no
// obligation; a proof that closes its law is that law's evidence, not a claim
// of its own.
CPPL_TEST(a_proof_of_a_law_instance_is_a_claim_of_its_own) {
    Builder unit;
    const auto base = unit.trusted(0);
    o::WrittenProof instance;
    instance.name = "instance";
    instance.law = cppl::vir::LawId{0};
    instance.assumptions = {base};
    unit.program.proofs.push_back(instance);
    o::WrittenProof closing = instance;
    closing.name = "closing";
    closing.closes_law = true;
    unit.program.proofs.push_back(closing);

    const auto closure = unit.close();

    CPPL_CHECK(closure.faults.empty());
    CPPL_CHECK(laws(claim(closure, o::ClaimKind::LawInstance, "instance")) == std::vector<std::uint32_t>{0});
    CPPL_CHECK(claim(closure, o::ClaimKind::LawInstance, "closing") == nullptr);
}

// SPEC: TRUSTED-002 (TRUST.md 35: trust follows the evidence graph)
CPPL_TEST(a_contract_rests_on_what_the_obligations_of_its_body_rest_on) {
    Builder unit;
    const auto base = unit.trusted(0);
    const auto statement = unit.proven(o::Origin::FunctionContract, "f7");
    const auto path = unit.proven(o::Origin::ReturnPath, "f7 path 1", {direct(base)});
    unit.total_contract(7, statement, {}, path);

    const auto closure = unit.close();

    CPPL_CHECK(closure.faults.empty());
    const auto* contract = claim(closure, o::ClaimKind::Contract, "f7");
    CPPL_CHECK(laws(contract) == std::vector<std::uint32_t>{0});
    CPPL_CHECK(contract->premises.front().direct);
}

CPPL_TEST(a_caller_rests_on_what_the_contracts_it_calls_rest_on) {
    Builder unit;
    const auto base = unit.trusted(0);
    const auto callee = unit.proven(o::Origin::FunctionContract, "f1", {base});
    unit.total_contract(1, callee);
    const auto caller = unit.proven(o::Origin::FunctionContract, "f2");
    unit.total_contract(2, caller, {1});
    const auto outer = unit.proven(o::Origin::FunctionContract, "f3");
    unit.total_contract(3, outer, {2});
    const auto unrelated = unit.proven(o::Origin::FunctionContract, "f4");
    unit.total_contract(4, unrelated);

    const auto closure = unit.close();

    CPPL_CHECK(closure.faults.empty());
    CPPL_CHECK(laws(claim(closure, o::ClaimKind::Contract, "f2")) == std::vector<std::uint32_t>{0});
    CPPL_CHECK(laws(claim(closure, o::ClaimKind::Contract, "f3")) == std::vector<std::uint32_t>{0});
    CPPL_CHECK(!claim(closure, o::ClaimKind::Contract, "f3")->premises.front().direct);
    CPPL_CHECK(laws(claim(closure, o::ClaimKind::Contract, "f4")).empty());
}

// TRUST.md TCB-PROV-005: cycle-safe and deterministic.
CPPL_TEST(a_recursive_call_graph_is_closed_to_a_fixed_point) {
    Builder unit;
    const auto first = unit.trusted(0);
    const auto second = unit.trusted(1);
    const auto a = unit.proven(o::Origin::ReturnPath, "f10 path", {first});
    const auto b = unit.proven(o::Origin::ReturnPath, "f11 path", {second});
    const auto c = unit.proven(o::Origin::ReturnPath, "f12 path");
    unit.partial_contract(10, {a}, {1});    // f10 calls f11
    unit.partial_contract(11, {b}, {0, 1}); // f11 calls f10 and itself
    unit.partial_contract(12, {c}, {1});    // f12 calls f11

    const auto closure = unit.close();

    CPPL_CHECK(closure.faults.empty());
    for (const auto* name : {"f10", "f11", "f12"}) {
        CPPL_CHECK(laws(claim(closure, o::ClaimKind::Contract, name)) == (std::vector<std::uint32_t>{0, 1}));
    }
}

// SPEC: UNSAFE-003
// TRUST.md TCB-REPORT-005: an unsafe block travels the same edges as a trusted
// law, through a recursive call graph too, and a claim that a path of the body
// holding it cannot occur rests on it as well. It is never a trusted law.
CPPL_TEST(an_unsafe_block_reaches_every_caller_and_the_claims_of_its_body) {
    Builder unit;
    const auto a = unit.proven(o::Origin::ReturnPath, "f30 path");
    const auto claimed = unit.proven(o::Origin::ImpossiblePath, "f30 path 2");
    const auto b = unit.proven(o::Origin::ReturnPath, "f31 path");
    const auto c = unit.proven(o::Origin::ReturnPath, "f32 path");
    const auto d = unit.proven(o::Origin::ReturnPath, "f33 path");
    unit.partial_contract(30, {a, claimed}, {1}); // f30 calls f31
    unit.partial_contract(31, {b}, {0});          // f31 calls f30
    unit.partial_contract(32, {c}, {1});          // f32 calls f31
    unit.partial_contract(33, {d});               // f33 calls nothing
    const cppl::source::SourceLocation block{"unit.cpp", 40, 5};
    unit.program.contracts[1].unsafe_regions.push_back(block);

    const auto closure = unit.close();

    CPPL_CHECK(closure.faults.empty());
    CPPL_CHECK(closure.assumptions.empty());
    const auto rests_on_block = [&](o::ClaimKind kind, const std::string& subject, bool direct) {
        const auto* found = claim(closure, kind, subject);
        return found != nullptr && found->premises.empty() && found->unsafe.size() == 1 &&
               found->unsafe.front().location == block && found->unsafe.front().direct == direct;
    };
    CPPL_CHECK(rests_on_block(o::ClaimKind::Contract, "f31", true));
    CPPL_CHECK(rests_on_block(o::ClaimKind::Contract, "f30", false));
    CPPL_CHECK(rests_on_block(o::ClaimKind::Contract, "f32", false));
    CPPL_CHECK(rests_on_block(o::ClaimKind::ImpossiblePath, "f30 path 2", false));
    CPPL_CHECK(claim(closure, o::ClaimKind::Contract, "f33")->unsafe.empty());
}

CPPL_TEST(an_unproven_contract_is_not_a_claim) {
    Builder unit;
    const auto base = unit.trusted(0);
    const auto proven = unit.proven(o::Origin::ReturnPath, "f20 path", {base});
    const auto open = unit.unresolved(o::Origin::ReturnPath, "f20 other path");
    unit.partial_contract(20, {proven, open});

    const auto closure = unit.close();

    CPPL_CHECK(closure.faults.empty());
    CPPL_CHECK(claim(closure, o::ClaimKind::Contract, "f20") == nullptr);
}

// --- What cannot be attributed is a fault, never an omission -----------------

CPPL_TEST(a_premise_that_is_not_a_trusted_law_of_the_unit_is_a_fault) {
    Builder unit;
    auto stranger = unit.trusted(0);
    stranger.law = cppl::vir::LawId{9};
    stranger.name = "stranger";
    unit.proven(o::Origin::LawProposition, "derived", {stranger});

    CPPL_CHECK(faulted(unit.close(), "'stranger', which is not a trusted law"));
}

CPPL_TEST(a_dependency_that_belongs_to_no_claim_is_a_fault) {
    Builder unit;
    const auto base = unit.trusted(0);
    unit.proven(o::Origin::RefinementIntroduction, "f30 -> Positive", {base});

    CPPL_CHECK(faulted(unit.close(), "belongs to no claim"));
}

CPPL_TEST(a_contract_proven_through_a_call_to_an_unknown_function_is_a_fault) {
    Builder unit;
    const auto statement = unit.proven(o::Origin::FunctionContract, "f40");
    unit.total_contract(40, statement, {41});

    CPPL_CHECK(faulted(unit.close(), "through a call to 'f41'"));
}

CPPL_TEST(an_obligation_claimed_by_two_contracts_is_a_fault) {
    Builder unit;
    const auto shared = unit.proven(o::Origin::FunctionContract, "f50");
    unit.total_contract(50, shared);
    unit.total_contract(51, shared);

    CPPL_CHECK(faulted(unit.close(), "belongs to the contracts of both"));
}

CPPL_TEST(a_contract_naming_an_obligation_that_does_not_exist_is_a_fault) {
    Builder unit;
    unit.total_contract(60, 5);

    CPPL_CHECK(faulted(unit.close(), "does not exist"));
}

CPPL_TEST(results_that_do_not_match_the_obligations_are_a_fault) {
    Builder unit;
    unit.proven(o::Origin::LawProposition, "derived");
    unit.results.pop_back();

    const auto closure = unit.close();

    CPPL_CHECK(faulted(closure, "do not correspond"));
    CPPL_CHECK(closure.claims.empty());
}
