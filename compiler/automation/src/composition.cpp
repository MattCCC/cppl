#include "composition.hpp"

#include <variant>

#include "cppl/kernel/substitution.hpp"

namespace cppl::automation {
namespace {

struct Step {
    kernel::Proposition goal;
    kernel::ProofTerm proof;
};

std::vector<kernel::Term> parameters_of(const obligations::ContractVerification& function) {
    std::vector<kernel::Term> parameters;
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
        parameters.push_back(kernel::Term::variable(
            kernel::parameter_reference(function.parameters.size(), index)));
    }
    return parameters;
}

std::expected<Step, std::string> instantiate(Step step,
                                            const std::vector<kernel::Term>& arguments) {
    for (const auto& argument : arguments) {
        const auto* quantified = std::get_if<kernel::Forall>(&step.goal.node);
        if (quantified == nullptr) {
            return std::unexpected("contract evidence has too few parameter binders");
        }
        auto goal = kernel::instantiate(*quantified->body, argument);
        step.proof = kernel::ProofTerm::forall_elimination(std::move(step.goal),
                                                           std::move(step.proof), argument);
        step.goal = std::move(goal);
    }
    return step;
}

std::expected<Step, std::string> discharge(Step step, const Step& premise) {
    const auto* implication = std::get_if<kernel::Implies>(&step.goal.node);
    if (implication == nullptr || !(*implication->premise == premise.goal)) {
        return std::unexpected("contract evidence does not establish the required premise");
    }
    auto goal = *implication->conclusion;
    step.proof = kernel::ProofTerm::implication_elimination(std::move(step.goal),
                                                           std::move(step.proof), premise.proof);
    step.goal = std::move(goal);
    return step;
}

std::expected<Step, std::string> under_caller(
    Step step, const obligations::ContractVerification& caller) {
    auto opened = instantiate(std::move(step), parameters_of(caller));
    if (!opened || !caller.precondition.has_value()) {
        return opened;
    }
    return discharge(std::move(*opened), Step{*caller.precondition,
        kernel::ProofTerm::hypothesis(kernel::HypothesisIndex{0})});
}

kernel::ProofTerm close(const obligations::ContractVerification& function, kernel::ProofTerm proof) {
    if (function.precondition.has_value()) {
        proof = kernel::ProofTerm::implication_introduction(*function.precondition, std::move(proof));
    }
    for (auto parameter = function.parameters.rbegin(); parameter != function.parameters.rend();
         ++parameter) {
        proof = kernel::ProofTerm::forall_introduction(*parameter, std::move(proof));
    }
    return proof;
}

}  // namespace

Composition::Composition(const obligations::Program& program) : program_(program) {
    for (const auto& function : program.contracts) {
        for (std::size_t prefix = 0; prefix < function.calls.size(); ++prefix) {
            const auto& call = function.calls[prefix];
            if (call.precondition_obligation.has_value() && call.reasoning_goal.has_value()) {
                stages_.emplace(*call.precondition_obligation,
                                Stage{&function, prefix, &*call.reasoning_goal, false});
            }
        }
        stages_.emplace(function.obligation,
                        Stage{&function, function.calls.size(), &function.reasoning_goal, true});
    }
}

bool Composition::owns(std::size_t obligation) const {
    return stages_.contains(obligation);
}

std::expected<Evidence, std::string> Composition::propose(std::size_t obligation) const {
    const auto& stage = stages_.at(obligation);
    const auto& function = *stage.function;
    const std::size_t dependencies = stage.prefix + (stage.final ? 0 : 1);
    for (std::size_t index = 0; index < dependencies; ++index) {
        const auto& call = function.calls[index];
        if (!callees_.contains(call.callee.value)) {
            return std::unexpected("callee '" + call.callee_name + "' is not proven");
        }
        if (index < stage.prefix && call.precondition_obligation.has_value() &&
            !proven_.contains(*call.precondition_obligation)) {
            return std::unexpected("call-site precondition for '" + call.callee_name +
                                   "' is not proven");
        }
    }

    const auto candidate = automation::propose(program_.context, *stage.reasoning);
    if (!candidate.has_value()) {
        return std::unexpected("no evidence for reasoning from the callee contracts");
    }
    const auto checked = kernel::check(program_.context, *stage.reasoning, candidate->proof, {});
    if (!checked) {
        return std::unexpected("contract-only goal: " + kernel::describe(*stage.reasoning) +
                               "; " + checked.error().detail);
    }

    auto reasoning = instantiate(Step{*stage.reasoning, candidate->proof}, parameters_of(function));
    std::vector<kernel::Term> values;
    for (std::size_t index = 0; index < stage.prefix; ++index) {
        values.push_back(function.calls[index].value);
    }
    if (reasoning) {
        reasoning = instantiate(std::move(*reasoning), values);
    }
    if (reasoning && function.precondition.has_value()) {
        reasoning = discharge(std::move(*reasoning), Step{*function.precondition,
            kernel::ProofTerm::hypothesis(kernel::HypothesisIndex{0})});
    }
    if (!reasoning) {
        return std::unexpected(reasoning.error());
    }

    for (std::size_t index = 0; index < stage.prefix; ++index) {
        const auto& call = function.calls[index];
        const auto& theorem = callees_.at(call.callee.value);
        auto postcondition = instantiate(Step{theorem.goal, theorem.proof}, call.arguments);
        if (postcondition && call.precondition_obligation.has_value()) {
            const auto precondition_index = *call.precondition_obligation;
            auto precondition = under_caller(
                Step{program_.obligations[precondition_index].goal, proven_.at(precondition_index)},
                function);
            if (!precondition) {
                return std::unexpected(precondition.error());
            }
            postcondition = discharge(std::move(*postcondition), *precondition);
        }
        if (!postcondition) {
            return std::unexpected(postcondition.error());
        }
        reasoning = discharge(std::move(*reasoning), *postcondition);
        if (!reasoning) {
            return std::unexpected(reasoning.error());
        }
    }
    return Evidence{close(function, std::move(reasoning->proof)), "verified-call-composition"};
}

std::expected<void, std::string> Composition::accept(
    std::size_t obligation, const kernel::ProofTerm& proof, const kernel::Acceptance& acceptance) {
    if (!(acceptance.proposition() == program_.obligations[obligation].goal)) {
        return std::unexpected("the kernel accepted a different contract obligation");
    }
    const auto& stage = stages_.at(obligation);
    if (stage.final) {
        const auto& function = *stage.function;
        auto body = under_caller(Step{program_.obligations[obligation].goal, proof}, function);
        if (!body) {
            return std::unexpected(body.error());
        }
        auto exported = close(function, kernel::ProofTerm::equality_elimination(
            function.result, function.named_value, function.returned_value, function.postcondition,
            kernel::ProofTerm::reflexivity(), std::move(body->proof)));
        const auto checked = kernel::check(program_.context, function.theorem, exported, {});
        if (!checked) {
            return std::unexpected("callee contract linkage failed: " + checked.error().detail);
        }
        callees_.emplace(function.function.value, Theorem{function.theorem, std::move(exported)});
    }
    proven_.emplace(obligation, proof);
    return {};
}

}  // namespace cppl::automation
