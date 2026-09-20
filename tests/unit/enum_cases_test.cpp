#include "cppl/kernel/check.hpp"
#include "cppl/obligations/generate.hpp"
#include "cppl/testing/test.hpp"

namespace {
namespace v = cppl::vir;
namespace k = cppl::kernel;
v::Type enumeration() {
    auto type = v::Type::integer(32, false);
    type.enumeration = "enum:E";
    type.enumerators = {1};
    return type;
}
v::Expr subject() {
    v::Expr value;
    value.type = enumeration();
    value.node = v::ParameterRef{0, "s"};
    return value;
}
v::Proof proof() {
    v::Proof result;
    result.name = "cases";
    result.parameters = {{"s", enumeration()}};
    result.proposition.type = v::Type::proposition();
    result.proposition.node = v::FormalEquality{enumeration(), {subject(), subject()}};
    const v::ProofStep refl{v::ReflexivityStep{}, {}};
    result.steps = {{v::CasesStep{subject(), {{1, {refl}, {}}, {std::nullopt, {refl}, {}}}}, {}}};
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

CPPL_TEST(enum_cases_generate_only_existing_kernel_evidence) {
    cppl::diagnostics::Engine engine;
    const auto program = lower(proof(), engine);
    CPPL_CHECK(!engine.has_errors());
    CPPL_CHECK_EQ(program.proofs.size(), std::size_t{1});
    const auto& written = program.proofs[0];
    CPPL_CHECK(k::check(program.context, written.goal, written.term, {}).has_value());
    const auto& quantified = std::get<k::ForallIntroduction>(written.term.node);
    CPPL_CHECK(std::holds_alternative<k::ConditionalElimination>(quantified.body->node));
}

CPPL_TEST(malformed_enum_case_partitions_are_rejected_before_evidence) {
    auto value = proof();
    cases(value).arms.pop_back();
    refused(value);
    value = proof();
    cases(value).arms.push_back(cases(value).arms.front());
    refused(value);
    value = proof();
    cases(value).arms.front().value = 2;
    refused(value);
    value = proof();
    cases(value).subject.type.enumerators = {1, 1};
    refused(value);
    value = proof();
    cases(value).subject.type.enumeration.clear();
    refused(value);
    value = proof();
    cases(value).subject.node = v::ParameterRef{8, "outside"};
    refused(value);
}

CPPL_TEST(enum_residual_values_cannot_be_discarded_to_prove_a_false_claim) {
    auto value = proof();
    auto constant = subject();
    constant.node = v::IntLiteral{1};
    value.proposition.node = v::FormalEquality{enumeration(), {subject(), constant}};
    refused(value);
}

CPPL_TEST(enum_arms_cannot_use_forged_or_missing_hypotheses) {
    auto value = proof();
    cases(value).arms[0].steps = {{v::ExactStep{v::Reference{v::HypothesisRef{0}, "invented"}, {}}, {}}};
    refused(value);
    value = proof();
    cases(value).arms[0].steps.clear();
    refused(value);
}

CPPL_TEST(the_kernel_rechecks_enum_case_premises_types_and_capture) {
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
