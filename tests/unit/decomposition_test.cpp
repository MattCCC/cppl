// The generic case engine, exercised through one provider.
//
// Nothing here is about enumerations: the assertions are about arm matching,
// exhaustiveness, binder scope and the evidence the engine builds. A scoped
// enum is only the subject that makes a partition exist. Provider-specific
// state modeling is tested separately, once per provider.
#include "cppl/kernel/check.hpp"
#include "cppl/obligations/generate.hpp"
#include "cppl/testing/test.hpp"

namespace {
namespace v = cppl::vir;
namespace k = cppl::kernel;

// A subject with two proof-visible states: one named case and a residual.
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
v::Proof proof() {
    v::Proof result;
    result.name = "cases";
    result.parameters = {{"s", representation()}};
    result.proposition.type = v::Type::proposition();
    result.proposition.node = v::FormalEquality{representation(), {subject(), subject()}};
    const v::ProofStep refl{v::ReflexivityStep{}, {}};
    result.steps = {{v::CasesStep{subject(), {{0U, "E::a", {refl}, {}}, {std::nullopt, "unnamed", {refl}, {}}}}, {}}};
    return result;
}
cppl::obligations::Program lower(v::Proof proof, cppl::diagnostics::Engine& engine) {
    cppl::elaboration::Result elaborated;
    elaborated.module.proofs.push_back(std::move(proof));
    return cppl::obligations::generate(elaborated.module, elaborated, engine);
}
v::CasesStep& cases(v::Proof& proof) {
    return std::get<v::CasesStep>(proof.steps[0].node);
}
void refused(v::Proof proof) {
    cppl::diagnostics::Engine engine;
    const auto program = lower(std::move(proof), engine);
    CPPL_CHECK(engine.has_errors());
    CPPL_CHECK(program.proofs.empty());
}
} // namespace

CPPL_TEST(case_splitting_builds_only_existing_kernel_evidence) {
    cppl::diagnostics::Engine engine;
    const auto program = lower(proof(), engine);
    CPPL_CHECK(!engine.has_errors());
    CPPL_CHECK_EQ(program.proofs.size(), std::size_t{1});
    const auto& written = program.proofs[0];
    CPPL_CHECK(k::check(program.context, written.goal, written.term, {}).has_value());
    // Conditional elimination and the rules it composes with - no case rule.
    const auto& quantified = std::get<k::ForallIntroduction>(written.term.node);
    CPPL_CHECK(std::holds_alternative<k::ConditionalElimination>(quantified.body->node));
}

CPPL_TEST(arms_that_do_not_cover_the_providers_partition_are_refused) {
    // A missing residual arm, a duplicated case, a case index that names
    // nothing, and a residual arm where the partition allows only one.
    auto value = proof();
    cases(value).arms.pop_back();
    refused(value);
    value = proof();
    cases(value).arms.push_back(cases(value).arms.front());
    refused(value);
    value = proof();
    cases(value).arms.front().descriptor = 7;
    refused(value);
    value = proof();
    cases(value).arms.push_back(cases(value).arms.back());
    refused(value);
}

CPPL_TEST(a_subject_without_a_decomposition_provider_is_refused) {
    // The engine asks the provider for the partition rather than trusting the
    // arms, so a subject no provider models cannot be split at all.
    auto value = proof();
    cases(value).subject.type.representation = {};
    refused(value);
    value = proof();
    cases(value).subject.node = v::ParameterRef{8, "outside"};
    refused(value);
}

CPPL_TEST(residual_states_cannot_be_discarded_to_prove_a_false_claim) {
    // `s` is not the one named state, so a proof that every `s` equals it must
    // fail in the residual branch.
    auto value = proof();
    auto constant = subject();
    constant.node = v::IntLiteral{1};
    value.proposition.node = v::FormalEquality{representation(), {subject(), constant}};
    refused(value);
}

CPPL_TEST(arms_cannot_use_forged_or_missing_hypotheses) {
    auto value = proof();
    cases(value).arms[0].steps = {{v::ExactStep{v::Reference{v::HypothesisRef{0}, "invented"}, {}}, {}}};
    refused(value);
    value = proof();
    cases(value).arms[0].steps.clear();
    refused(value);
}

CPPL_TEST(the_kernel_rechecks_case_premises_types_and_capture) {
    cppl::diagnostics::Engine engine;
    const auto program = lower(proof(), engine);
    CPPL_CHECK(!engine.has_errors());
    const auto& written = program.proofs[0];
    const auto rejected_mutation = [&](auto mutate) {
        auto term = written.term;
        auto& quantified = std::get<k::ForallIntroduction>(term.node);
        auto inner = *quantified.body;
        auto& branch = std::get<k::ConditionalElimination>(inner.node);
        mutate(branch);
        quantified.body = k::Box<k::ProofTerm>{std::move(inner)};
        CPPL_CHECK(!k::check(program.context, written.goal, term, {}));
    };
    rejected_mutation([](auto& branch) { branch.false_case = branch.true_case; });
    rejected_mutation(
        [](auto& branch) { branch.condition = k::Term::literal(k::IntType{32, k::Signedness::Unsigned}, 1); });
    rejected_mutation([](auto& branch) { branch.when_true = k::Term::variable(k::VarIndex{3}); });
    rejected_mutation(
        [](auto& branch) { branch.true_case = k::Box<k::ProofTerm>{k::ProofTerm::hypothesis(k::HypothesisIndex{0})}; });
}
