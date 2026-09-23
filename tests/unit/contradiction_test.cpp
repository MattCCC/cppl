// The checked-contradiction mechanism, below the surface syntax (SPEC.md
// CASE-011, CASE-012).
//
// Three properties are pinned here that the end-to-end tests can only imply:
//
//   - an omitted case is discharged by ordinary kernel evidence, never by a
//     rule of its own or by the frontend's say-so;
//   - that evidence is believed only because the kernel accepts it, so every
//     way of corrupting it is refused;
//   - one contradiction, claimed as an omitted case and as an unreachable
//     runtime path, stays two claims: two origins, two identities, two
//     reporting names and two diagnostics.
//
// The proof is built by hand, so nothing here depends on Clang:
//
//     proof omitting(E s) proves (s == 1 -> s == s) {
//         assume is_a : s == 1;
//         cases s {
//             E::a => { refl; }
//             omit unnamed by contradiction is_a;
//         }
//     }
#include "cppl/automation/evidence.hpp"
#include "cppl/kernel/check.hpp"
#include "cppl/obligations/generate.hpp"
#include "cppl/obligations/status.hpp"
#include "cppl/testing/test.hpp"

#include <algorithm>
#include <string>
#include <variant>

namespace {
namespace v = cppl::vir;
namespace k = cppl::kernel;
namespace o = cppl::obligations;

// `enum class E : unsigned { a = 1 }`: one named state and the residual one.
v::Type representation() {
    auto type = v::Type::integer(32, false);
    type.representation.identity = "c:@E@E";
    type.representation.name = "E";
    type.representation.enumerators = {{"a", 1}};
    return type;
}
v::Expr subject() {
    v::Expr value;
    value.type = representation();
    value.node = v::ParameterRef{0, "s"};
    return value;
}
v::Expr literal(std::int64_t value) {
    v::Expr constant = subject();
    constant.node = v::IntLiteral{value};
    return constant;
}
v::Expr equals(v::Expr lhs, v::Expr rhs) {
    v::Expr equality;
    equality.type = v::Type::proposition();
    equality.node = v::FormalEquality{representation(), {std::move(lhs), std::move(rhs)}};
    return equality;
}
v::Expr implies(v::Expr premise, v::Expr conclusion) {
    v::Expr implication;
    implication.type = v::Type::proposition();
    implication.node = v::Implication{{std::move(premise), std::move(conclusion)}};
    return implication;
}

v::Proof omitting() {
    v::Proof result;
    result.name = "omitting";
    result.parameters = {{"s", representation()}};
    result.proposition = implies(equals(subject(), literal(1)), equals(subject(), subject()));
    const v::ProofStep refl{v::ReflexivityStep{}, {}};
    const v::ProofStep discharge{v::ContradictionStep{v::Reference{v::HypothesisRef{0}, "is_a"}, {}}, {}};
    v::CasesStep split{subject(), {{0U, "E::a", false, {refl}, {}}, {std::nullopt, "unnamed", true, {discharge}, {}}}};
    result.steps = {{v::AssumeStep{"is_a", equals(subject(), literal(1))}, {}}, {std::move(split), {}}};
    return result;
}

o::Program lower(v::Proof proof, cppl::diagnostics::Engine& engine) {
    cppl::elaboration::Result elaborated;
    elaborated.module.proofs.push_back(std::move(proof));
    return o::generate(elaborated.module, elaborated, engine);
}

const o::Obligation* omission_in(const o::Program& program) {
    const auto found = std::ranges::find(program.obligations, o::Origin::OmittedCase, &o::Obligation::origin);
    return found == program.obligations.end() ? nullptr : &*found;
}

// The refutation at the heart of an omission's evidence, underneath the
// binders and premises it is closed over.
const k::LinearArithmetic& refutation_of(const k::ProofTerm& evidence) {
    if (const auto* quantified = std::get_if<k::ForallIntroduction>(&evidence.node)) {
        return refutation_of(*quantified->body);
    }
    if (const auto* supposed = std::get_if<k::ImplicationIntroduction>(&evidence.node)) {
        return refutation_of(*supposed->body);
    }
    return std::get<k::LinearArithmetic>(evidence.node);
}

// The same evidence with its refutation corrupted by `corrupt`.
template <typename Corrupt> k::ProofTerm corrupted(const k::ProofTerm& evidence, Corrupt corrupt) {
    if (const auto* quantified = std::get_if<k::ForallIntroduction>(&evidence.node)) {
        return k::ProofTerm::forall_introduction(quantified->binder, corrupted(*quantified->body, corrupt));
    }
    if (const auto* supposed = std::get_if<k::ImplicationIntroduction>(&evidence.node)) {
        return k::ProofTerm::implication_introduction(*supposed->premise, corrupted(*supposed->body, corrupt));
    }
    k::LinearArithmetic step = std::get<k::LinearArithmetic>(evidence.node);
    corrupt(step);
    return k::ProofTerm::linear_arithmetic(std::move(step.facts), std::move(step.certificate));
}
} // namespace

// SPEC: CASE-012, CASE-016
CPPL_TEST(an_omitted_case_is_an_obligation_of_its_own_that_the_kernel_checks) {
    cppl::diagnostics::Engine engine;
    const auto program = lower(omitting(), engine);
    CPPL_CHECK(!engine.has_errors());
    CPPL_CHECK_EQ(program.proofs.size(), std::size_t{1});
    CPPL_CHECK(k::check(program.context, program.proofs[0].goal, program.proofs[0].term, {}).has_value());

    // One omission, one obligation, stated apart from the proof it occurs in.
    CPPL_CHECK_EQ(std::ranges::count(program.obligations, o::Origin::OmittedCase, &o::Obligation::origin), 1);
    const o::Obligation* omission = omission_in(program);
    CPPL_CHECK(omission != nullptr);
    if (omission == nullptr) {
        return;
    }
    CPPL_CHECK(omission->subject == "case 'unnamed' of proof 'omitting'");
    // It names no law and no proof, so nothing else's evidence is looked up for
    // it: it carries its own.
    CPPL_CHECK(!omission->law.has_value());
    CPPL_CHECK(!omission->proof.has_value());
    CPPL_CHECK(omission->evidence.has_value());
    if (!omission->evidence.has_value()) {
        return;
    }
    CPPL_CHECK(k::check(program.context, omission->goal, *omission->evidence, {}).has_value());
}

// SPEC: CASE-013, CASE-014
CPPL_TEST(an_omission_concludes_false_by_ordinary_linear_arithmetic) {
    cppl::diagnostics::Engine engine;
    const auto program = lower(omitting(), engine);
    CPPL_CHECK(!engine.has_errors());
    const o::Obligation* omission = omission_in(program);
    CPPL_CHECK(omission != nullptr);
    if (omission == nullptr || !omission->evidence.has_value()) {
        return;
    }

    // The claim is that the premises standing in the omitted case cannot all
    // hold: underneath its binders and premises it concludes `False`, rather
    // than the proof's own goal.
    const k::Proposition* claim = &omission->goal;
    while (true) {
        if (const auto* quantified = std::get_if<k::Forall>(&claim->node)) {
            claim = &*quantified->body;
        } else if (const auto* implication = std::get_if<k::Implies>(&claim->node)) {
            claim = &*implication->conclusion;
        } else {
            break;
        }
    }
    CPPL_CHECK(std::holds_alternative<k::Falsity>(claim->node));

    // And the evidence is introductions around one linear-arithmetic step whose
    // facts are the named evidence and the standing premises: hypotheses, or
    // sides taken from a conjunction of them. No node here is specific to cases
    // or to contradiction, and no goal takes part in the step.
    const k::LinearArithmetic& step = refutation_of(*omission->evidence);
    CPPL_CHECK(step.facts.size() >= 2);
    for (const k::ArithmeticFact& fact : step.facts) {
        const k::ProofTerm& support = *fact.evidence;
        if (const auto* side = std::get_if<k::ConjunctionElimination>(&support.node)) {
            CPPL_CHECK(std::holds_alternative<k::Hypothesis>(side->evidence->node));
        } else {
            CPPL_CHECK(std::holds_alternative<k::Hypothesis>(support.node));
        }
    }
}

// SPEC: CASE-014, CASE-015
CPPL_TEST(the_kernel_alone_decides_whether_an_omission_holds) {
    cppl::diagnostics::Engine engine;
    const auto program = lower(omitting(), engine);
    CPPL_CHECK(!engine.has_errors());
    const o::Obligation* omission = omission_in(program);
    CPPL_CHECK(omission != nullptr);
    if (omission == nullptr || !omission->evidence.has_value()) {
        return;
    }
    const k::ProofTerm& original = *omission->evidence;

    const auto refused = [&](auto corrupt) {
        const k::ProofTerm evidence = corrupted(original, corrupt);
        CPPL_CHECK(!k::check(program.context, omission->goal, evidence, {}).has_value());
    };
    // The uncorrupted evidence is accepted, so each refusal below is the
    // corruption's doing.
    CPPL_CHECK(k::check(program.context, omission->goal, corrupted(original, [](auto&) {}), {}));
    // Only the named evidence, with every standing premise taken away. `s == 1`
    // alone is satisfiable, so no certificate refutes it, and the one kept is
    // now about constraints that are not there.
    refused([](k::LinearArithmetic& step) { step.facts.erase(step.facts.begin() + 1, step.facts.end()); });
    // A certificate that combines nothing contradictory.
    refused([](k::LinearArithmetic& step) {
        k::FarkasSum sum;
        sum.multipliers.emplace_back(0U, k::Wide{1});
        step.certificate = k::ArithmeticCertificate{std::move(sum)};
    });
    // A fact stated as something its evidence does not establish.
    refused([](k::LinearArithmetic& step) {
        step.facts.back().proposition = k::Proposition::equality(k::Type{k::kBoolean}, k::Term::literal(k::kBoolean, 1),
                                                                 k::Term::literal(k::kBoolean, 0));
    });
    // The evidence does not stand for any other claim, including the proof's
    // own goal.
    CPPL_CHECK(!k::check(program.context, program.proofs[0].goal, original, {}).has_value());
}

// SPEC: CASE-012, CASE-015, CASE-016, VERIFIED-023
CPPL_TEST(one_contradiction_claimed_under_two_origins_stays_two_claims) {
    cppl::diagnostics::Engine engine;
    const auto program = lower(omitting(), engine);
    CPPL_CHECK(!engine.has_errors());
    const o::Obligation* found = omission_in(program);
    CPPL_CHECK(found != nullptr);
    if (found == nullptr) {
        return;
    }
    const o::Obligation omission = *found;

    // Identity: the origin is part of it, the same inputs always give the same
    // identity, and two claims with one subject and goal are told apart by the
    // order they were written in.
    const auto identity = [&](o::Origin origin, std::uint64_t position) {
        return o::identify_impossibility(origin, program.context, omission.subject, omission.goal, position);
    };
    CPPL_CHECK(omission.id == identity(o::Origin::OmittedCase, 0));
    CPPL_CHECK(!(identity(o::Origin::OmittedCase, 0) == identity(o::Origin::ImpossiblePath, 0)));
    CPPL_CHECK(!(identity(o::Origin::OmittedCase, 0) == identity(o::Origin::OmittedCase, 1)));
    CPPL_CHECK(o::describe(o::Origin::OmittedCase) != o::describe(o::Origin::ImpossiblePath));

    // The same proposition and the same evidence, claimed as an unreachable
    // runtime path. Its final proof term has exactly the omission's shape, which
    // is the situation CASE-012 is about.
    o::Obligation path = omission;
    path.origin = o::Origin::ImpossiblePath;
    path.subject = "else branch of f";
    path.id = identity(o::Origin::ImpossiblePath, 0);

    o::Program both;
    both.context = program.context;
    both.obligations = {omission, path};
    cppl::diagnostics::Engine verified;
    const auto results = cppl::automation::verify(both, verified);
    CPPL_CHECK(!verified.has_errors());
    CPPL_CHECK_EQ(results.size(), std::size_t{2});
    CPPL_CHECK(results[0].verdict.is_proven());
    CPPL_CHECK(results[1].verdict.is_proven());
    CPPL_CHECK(results[0].obligation.origin == o::Origin::OmittedCase);
    CPPL_CHECK(results[1].obligation.origin == o::Origin::ImpossiblePath);
    CPPL_CHECK(!(results[0].obligation.id == results[1].obligation.id));
    CPPL_CHECK(results[0].strategy == "written contradiction");
    CPPL_CHECK(results[1].strategy == "written contradiction");

    // Refused, each is reported as the claim it made. The goal is provable by
    // arithmetic from its own premises, so a verifier that searched for other
    // evidence when the written contradiction failed would prove both; the
    // written evidence is the only evidence ever submitted (CASE-005).
    o::Program broken = both;
    broken.obligations[0].evidence = k::ProofTerm::reflexivity();
    broken.obligations[1].evidence = k::ProofTerm::reflexivity();
    cppl::diagnostics::Engine refused;
    const auto rejected = cppl::automation::verify(broken, refused);
    CPPL_CHECK(!rejected[0].verdict.is_proven());
    CPPL_CHECK(!rejected[1].verdict.is_proven());
    CPPL_CHECK_EQ(refused.diagnostics().size(), std::size_t{2});
    CPPL_CHECK(refused.diagnostics()[0].message ==
               "omitted case 'unnamed' of proof 'omitting' is not shown to be impossible");
    CPPL_CHECK(refused.diagnostics()[1].message == "runtime path 'else branch of f' is not shown to be unreachable");
}

// SPEC: VERIFIED-023, CASE-005, CASE-015
CPPL_TEST(a_claimed_impossibility_is_never_established_by_a_strategy) {
    // A path whose facts contradict each other: `x == 0` and `x == 1`. A
    // strategy would prove `False` from them, which is exactly what a claim may
    // not rest on. Only the contradiction written for it establishes it.
    const auto u32 = k::Type::integer(32, k::Signedness::Unsigned);
    const auto x = k::Term::variable(k::VarIndex{0});
    const auto is = [&](std::int64_t value) {
        return k::Proposition::equality(u32, x, k::Term::literal(u32.integer_type(), value));
    };
    const auto goal = k::Proposition::for_all(
        u32, k::Proposition::implication(is(0), k::Proposition::implication(is(1), k::Proposition::falsity())));

    o::Program program;
    const auto automatic = cppl::automation::propose(program.context, goal);
    CPPL_CHECK(automatic.has_value());
    if (!automatic.has_value()) {
        return;
    }
    CPPL_CHECK(k::check(program.context, goal, automatic->proof, {}).has_value());

    for (const o::Origin origin : {o::Origin::ImpossiblePath, o::Origin::OmittedCase}) {
        o::Obligation claim;
        claim.origin = origin;
        claim.subject = "f path 1";
        claim.goal = goal;
        claim.id = o::identify_impossibility(origin, program.context, claim.subject, claim.goal, 0);
        program.obligations = {claim};

        // No evidence written: unproven, and said so.
        cppl::diagnostics::Engine unwritten;
        const auto open = cppl::automation::verify(program, unwritten);
        CPPL_CHECK(!open[0].verdict.is_proven());
        CPPL_CHECK_EQ(unwritten.diagnostics().size(), std::size_t{1});

        // Evidence refused where it was built: unproven, and not reported twice.
        program.obligations[0].refusal = "the reason, reported where it was found";
        cppl::diagnostics::Engine refused;
        const auto still_open = cppl::automation::verify(program, refused);
        CPPL_CHECK(!still_open[0].verdict.is_proven());
        CPPL_CHECK(refused.diagnostics().empty());
    }
}
