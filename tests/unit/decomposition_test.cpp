// The generic case engine, exercised through one provider.
//
// Nothing here is about enumerations: the assertions are about arm matching,
// exhaustiveness, binder scope and the evidence the engine builds. A scoped
// enum is only the subject that makes a partition exist. Provider-specific
// state modeling is tested separately, once per provider.
#include "cppl/kernel/check.hpp"
#include "cppl/obligations/generate.hpp"
#include "cppl/testing/test.hpp"

#include <string>

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
    result.steps = {
        {v::CasesStep{subject(), {{0U, "E::a", false, {refl}, {}}, {std::nullopt, "unnamed", false, {refl}, {}}}}, {}}};
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

// The correspondence between a C++ representation and its logical partition is
// trust-sensitive: a bug there is a soundness bug even when the proof term it
// produces is locally well typed (SPEC.md CASE-010). The tests above attack the
// arms and the evidence, which the kernel rechecks. These attack the partition
// itself, which the kernel cannot recheck because it never sees the C++ type --
// so the engine has to refuse a representation that does not describe one.

CPPL_TEST(a_representation_that_describes_no_state_cannot_be_split) {
    // An enum whose model lists no enumerator has one residual state and
    // nothing else. Splitting it would claim the subject has a named state the
    // representation never described.
    auto value = proof();
    cases(value).subject.type.representation.enumerators.clear();
    for (auto& parameter : value.parameters)
        parameter.type.representation.enumerators.clear();

    cppl::diagnostics::Engine engine;
    const auto program = lower(std::move(value), engine);
    CPPL_CHECK(engine.has_errors());
    CPPL_CHECK(program.proofs.empty());
    // The arm claims a named case the partition no longer has, rather than the
    // split being allowed and answered against a partition of one state.
    CPPL_CHECK(!engine.diagnostics().empty());
    CPPL_CHECK(engine.diagnostics()[0].message.find("malformed case evidence") != std::string::npos);
}

CPPL_TEST(a_partition_that_contradicts_the_subjects_own_type_is_refused) {
    // The arms are left alone, and the subject keeps a consistent type, so the
    // only disagreement is between the partition and the arms: the
    // representation now has a second named state that no arm claims. The
    // engine asks the provider for the partition rather than trusting the
    // arms, so this is a missing case rather than a silent answer about the
    // wrong states.
    auto value = proof();
    cases(value).subject.type.representation.enumerators = {{"a", 1}, {"b", 2}};
    for (auto& parameter : value.parameters)
        parameter.type.representation.enumerators = {{"a", 1}, {"b", 2}};

    cppl::diagnostics::Engine engine;
    const auto program = lower(std::move(value), engine);
    CPPL_CHECK(engine.has_errors());
    CPPL_CHECK(program.proofs.empty());
    // Refused for the reason claimed above, not for some unrelated mismatch a
    // bare "it errored" assertion would also accept. This layer names the
    // uncovered case generically; the arm-by-name wording belongs to
    // elaboration, which a hand-built VIR proof does not go through.
    CPPL_CHECK(engine.diagnostics()[0].message.find("incomplete case evidence") != std::string::npos);
}

CPPL_TEST(a_discriminator_naming_a_different_value_changes_which_case_is_proven) {
    // The same number of states, but the named one is a different value. The
    // arm still claims descriptor 0, so if the discriminator were taken on
    // trust the proof would be accepted while meaning something else. The
    // kernel sees the changed condition, so the evidence no longer closes the
    // goal it is checked against.
    cppl::diagnostics::Engine engine;
    const auto honest = lower(proof(), engine);
    CPPL_CHECK(!engine.has_errors());

    auto value = proof();
    cases(value).subject.type.representation.enumerators = {{"a", 9}};
    for (auto& parameter : value.parameters)
        parameter.type.representation.enumerators = {{"a", 9}};
    cppl::diagnostics::Engine other;
    const auto shifted = lower(std::move(value), other);

    // The discriminator reaches the kernel, so changing it must change the
    // evidence. The same proof term standing for both partitions would mean
    // the named value never took part in what was checked.
    CPPL_CHECK(!other.has_errors());
    CPPL_CHECK_EQ(shifted.proofs.size(), std::size_t{1});
    CPPL_CHECK(!(shifted.proofs[0].term == honest.proofs[0].term));
}
