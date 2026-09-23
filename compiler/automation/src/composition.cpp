#include "composition.hpp"

#include "cppl/kernel/substitution.hpp"

#include <algorithm>
#include <ranges>
#include <variant>

namespace cppl::automation {
namespace {

struct Step {
    kernel::Proposition goal;
    kernel::ProofTerm proof;
};

std::vector<kernel::Term> parameters_of(const obligations::ContractVerification& function) {
    std::vector<kernel::Term> parameters;
    parameters.reserve(function.parameters.size());
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
        parameters.push_back(kernel::Term::variable(kernel::parameter_reference(function.parameters.size(), index)));
    }
    return parameters;
}

std::expected<Step, std::string> instantiate(Step step, const std::vector<kernel::Term>& arguments) {
    for (const auto& argument : arguments) {
        const auto* quantified = std::get_if<kernel::Forall>(&step.goal.node);
        if (quantified == nullptr) {
            return std::unexpected("contract evidence has too few parameter binders");
        }
        auto goal = kernel::instantiate(*quantified->body, argument);
        step.proof = kernel::ProofTerm::forall_elimination(std::move(step.goal), std::move(step.proof), argument);
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
    step.proof = kernel::ProofTerm::implication_elimination(std::move(step.goal), std::move(step.proof), premise.proof);
    step.goal = std::move(goal);
    return step;
}

// Discharges the caller's own preconditions, which are in scope as hypotheses
// introduced before the `conditions` path conditions that follow them.
std::expected<Step, std::string> discharge_preconditions(std::expected<Step, std::string> step,
                                                         const obligations::ContractVerification& caller,
                                                         std::size_t conditions) {
    const std::size_t count = caller.preconditions.size();
    for (std::size_t index = 0; step && index < count; ++index) {
        step = discharge(std::move(*step), Step{caller.preconditions[index],
                                                kernel::ProofTerm::hypothesis(kernel::HypothesisIndex{
                                                    static_cast<std::uint32_t>(conditions + count - 1 - index)})});
    }
    return step;
}

std::expected<Step, std::string> under_caller(Step step, const obligations::ContractVerification& caller,
                                              const obligations::ReturnPath* path = nullptr, std::size_t needed = 0,
                                              std::size_t available = 0) {
    auto opened = discharge_preconditions(instantiate(std::move(step), parameters_of(caller)), caller, available);
    for (std::size_t index = 0; opened && index < needed; ++index) {
        opened = discharge(std::move(*opened), Step{path->conditions[index].actual,
                                                    kernel::ProofTerm::hypothesis(kernel::HypothesisIndex{
                                                        static_cast<std::uint32_t>(available - 1 - index)})});
    }
    return opened;
}

kernel::ProofTerm close(const obligations::ContractVerification& function, kernel::ProofTerm proof,
                        const obligations::ReturnPath* path = nullptr, std::size_t conditions = 0) {
    for (std::size_t index = conditions; index > 0; --index) {
        proof = kernel::ProofTerm::implication_introduction(path->conditions[index - 1].actual, std::move(proof));
    }
    for (const auto& precondition : std::views::reverse(function.preconditions)) {
        proof = kernel::ProofTerm::implication_introduction(precondition, std::move(proof));
    }
    for (const auto& parameter : std::views::reverse(function.parameters)) {
        proof = kernel::ProofTerm::forall_introduction(parameter, std::move(proof));
    }
    return proof;
}

std::expected<kernel::ProofTerm, std::string> assemble(const obligations::Program& program,
                                                       const obligations::ContractVerification& function,
                                                       const kernel::Term& value, std::size_t depth, std::size_t& leaf,
                                                       const std::map<std::size_t, kernel::ProofTerm>& proven) {
    if (const auto* branch = std::get_if<kernel::Prim>(&value.node);
        branch != nullptr && branch->op == kernel::PrimOp::Select && branch->arguments.size() == 3) {
        auto when_true = assemble(program, function, branch->arguments[1], depth + 1, leaf, proven);
        if (!when_true)
            return when_true;
        auto when_false = assemble(program, function, branch->arguments[2], depth + 1, leaf, proven);
        if (!when_false)
            return when_false;
        return kernel::ProofTerm::conditional_elimination(
            function.result, branch->arguments[0], branch->arguments[1], branch->arguments[2], function.postcondition,
            kernel::ProofTerm::implication_introduction(kernel::predicate(branch->arguments[0], true),
                                                        std::move(*when_true)),
            kernel::ProofTerm::implication_introduction(kernel::predicate(branch->arguments[0], false),
                                                        std::move(*when_false)));
    }
    if (leaf >= function.paths.size())
        return std::unexpected("missing return path");
    const auto& path = function.paths[leaf++];
    if (path.conditions.size() != depth || !proven.contains(path.obligation)) {
        return std::unexpected("return path is not proven");
    }
    auto body = under_caller(Step{program.obligations[path.obligation].goal, proven.at(path.obligation)}, function,
                             &path, depth, depth);
    if (!body)
        return std::unexpected(body.error());
    return std::move(body->proof);
}

} // namespace

// Composing one obligation's evidence charges a transition per dependency it
// consumes, so a well-formed program's whole search costs at most one
// transition per (obligation, call) pair. The factor above that is slack: a
// valid program never approaches the bound, so it never has to be tuned as
// programs grow. The constant floor covers programs with no calls at all.
std::size_t Composition::budget_for(const obligations::Program& program) {
    std::size_t calls = 0;
    for (const auto& contract : program.contracts) {
        for (const auto& path : contract.paths) {
            calls += path.calls.size() + 1;
        }
    }
    return 64 + 64 * (program.obligations.size() + calls);
}

std::expected<void, std::string> Composition::spend(std::optional<std::size_t> dependency) const {
    if (++transitions_ > budget_) {
        return std::unexpected("the search for evidence exceeded its budget of " + std::to_string(budget_) +
                               " transitions without resolving this obligation");
    }
    // Reaching a dependency that is not proven means the search advanced into a
    // state it had no evidence to enter. Left alone it would revisit that state
    // for as long as it is allowed to, and would read evidence that does not
    // exist. It stops here instead, and says which obligation was missing.
    if (dependency.has_value() && !proven_.contains(*dependency)) {
        return std::unexpected("obligation " + std::to_string(*dependency) +
                               " is not proven, so the search cannot make progress from here");
    }
    return {};
}

Composition::Composition(const obligations::Program& program) : program_(program), budget_(budget_for(program)) {
    for (std::size_t index = 0; index < program.contracts.size(); ++index) {
        const auto& function = program.contracts[index];
        if (function.partial) {
            for (const auto& condition : function.conditions) {
                conditions_.emplace(condition.obligation, Condition{index, &condition});
            }
            continue;
        }
        for (const auto& path : function.paths) {
            for (std::size_t prefix = 0; prefix < path.calls.size(); ++prefix) {
                const auto& call = path.calls[prefix];
                for (const auto& precondition : call.preconditions) {
                    stages_.emplace(precondition.obligation, Stage{&function, &path, prefix, call.conditions,
                                                                   &precondition.reasoning_goal, false});
                }
            }
            stages_.emplace(path.obligation, Stage{&function, &path, path.calls.size(), path.conditions.size(),
                                                   &path.reasoning_goal, true});
        }
        if (!stages_.contains(function.obligation)) {
            stages_.emplace(function.obligation, Stage{&function, nullptr, 0, 0, nullptr, false});
        }
    }
}

bool Composition::owns(std::size_t obligation) const {
    return stages_.contains(obligation) || conditions_.contains(obligation);
}

// A total contract is established once its theorem about the callee's
// definition has been exported; a partial one once every one of its
// conditions has been accepted.
bool Composition::established(std::size_t contract) const {
    const auto& function = program_.contracts[contract];
    return function.partial ? partial_established_.contains(contract) : callees_.contains(function.function.value);
}

// A condition supposes the postconditions of the contracts its path calls, so
// it is not offered to the kernel until each of those is established. What it
// states is then decided by the kernel like any other goal.
std::expected<Evidence, std::string> Composition::propose_condition(const Condition& condition,
                                                                    std::size_t obligation) const {
    for (const std::size_t callee : condition.condition->callees) {
        if (!established(callee)) {
            return std::unexpected("callee '" + program_.contracts[callee].name + "' is not proven");
        }
    }
    // A path claimed not to occur is established by the contradiction written
    // for it, never by a strategy (SPEC.md VERIFIED-023, CASE-005).
    const obligations::Obligation& stated = program_.obligations[obligation];
    if (stated.evidence.has_value()) {
        return Evidence{*stated.evidence, "written contradiction"};
    }
    if (stated.origin == obligations::Origin::ImpossiblePath) {
        return std::unexpected("a runtime path is shown not to occur only by the contradiction written for it");
    }
    const auto candidate = automation::propose(program_.context, stated.goal);
    if (!candidate.has_value()) {
        return std::unexpected("no strategy produced candidate evidence");
    }
    return Evidence{candidate->proof, "partial-correctness condition by " + candidate->strategy};
}

std::expected<Evidence, std::string> Composition::propose(std::size_t obligation) const {
    if (const auto condition = conditions_.find(obligation); condition != conditions_.end()) {
        return propose_condition(condition->second, obligation);
    }
    const auto& stage = stages_.at(obligation);
    const auto& function = *stage.function;
    if (stage.path == nullptr) {
        std::size_t leaf = 0;
        auto proof = assemble(program_, function, function.returned_value, 0, leaf, proven_);
        if (!proof)
            return std::unexpected(proof.error());
        if (leaf != function.paths.size())
            return std::unexpected("return tree does not cover every path");
        return Evidence{close(function, std::move(*proof)), "verified-path-composition"};
    }
    const auto& path = *stage.path;
    const std::size_t dependencies = stage.prefix + (stage.path_end ? 0 : 1);
    for (std::size_t index = 0; index < dependencies; ++index) {
        const auto& call = path.calls[index];
        if (!callees_.contains(call.callee.value)) {
            return std::unexpected("callee '" + call.callee_name + "' is not proven");
        }
        if (index < stage.prefix && !std::ranges::all_of(
                                        call.preconditions,
                                        [this](const obligations::CallPrecondition& precondition) {
                                            return proven_.contains(precondition.obligation);
                                        })) {
            return std::unexpected("call-site precondition for '" + call.callee_name + "' is not proven");
        }
    }

    const auto candidate = automation::propose(program_.context, *stage.reasoning);
    if (!candidate.has_value()) {
        return std::unexpected("no evidence for reasoning from the callee contracts");
    }
    const auto checked = kernel::check(program_.context, *stage.reasoning, candidate->proof, {});
    if (!checked) {
        return std::unexpected("contract-only goal: " + kernel::describe(*stage.reasoning) + "; " +
                               checked.error().detail);
    }

    auto reasoning = instantiate(Step{*stage.reasoning, candidate->proof}, parameters_of(function));
    std::vector<kernel::Term> values;
    values.reserve(stage.prefix);
    for (std::size_t index = 0; index < stage.prefix; ++index) {
        values.push_back(path.calls[index].value);
    }
    if (reasoning) {
        reasoning = instantiate(std::move(*reasoning), values);
    }
    reasoning = discharge_preconditions(std::move(reasoning), function, stage.conditions);
    for (std::size_t index = 0; reasoning && index < stage.conditions; ++index) {
        reasoning =
            discharge(std::move(*reasoning), Step{path.conditions[index].actual,
                                                  kernel::ProofTerm::hypothesis(kernel::HypothesisIndex{
                                                      static_cast<std::uint32_t>(stage.conditions - 1 - index)})});
    }
    if (!reasoning) {
        return std::unexpected(reasoning.error());
    }

    for (std::size_t index = 0; index < stage.prefix; ++index) {
        if (auto fuel = spend(); !fuel) {
            return std::unexpected(fuel.error());
        }
        const auto& call = path.calls[index];
        const auto& theorem = callees_.at(call.callee.value);
        auto postcondition = instantiate(Step{theorem.goal, theorem.proof}, call.arguments);
        for (const auto& required : call.preconditions) {
            if (!postcondition) {
                break;
            }
            // Charged against the dependency whose evidence is read on the very
            // next line, so an unproven one stops the search before that read.
            if (auto fuel = spend(required.obligation); !fuel) {
                return std::unexpected(fuel.error());
            }
            auto precondition =
                under_caller(Step{program_.obligations[required.obligation].goal, proven_.at(required.obligation)},
                             function, &path, call.conditions, stage.conditions);
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
    return Evidence{close(function, std::move(reasoning->proof), &path, stage.conditions), "verified-call-composition"};
}

std::expected<void, std::string> Composition::accept(std::size_t obligation, const kernel::ProofTerm& proof,
                                                     const kernel::Acceptance& acceptance) {
    if (!(acceptance.proposition() == program_.obligations[obligation].goal)) {
        return std::unexpected("the kernel accepted a different contract obligation");
    }
    if (const auto condition = conditions_.find(obligation); condition != conditions_.end()) {
        proven_.emplace(obligation, proof);
        const auto& contract = program_.contracts[condition->second.contract];
        if (std::ranges::all_of(contract.conditions, [this](const obligations::VerificationCondition& each) {
                return proven_.contains(each.obligation);
            })) {
            partial_established_.insert(condition->second.contract);
        }
        return {};
    }
    const auto& stage = stages_.at(obligation);
    if (obligation == stage.function->obligation) {
        const auto& function = *stage.function;
        auto body = under_caller(Step{program_.obligations[obligation].goal, proof}, function);
        if (!body) {
            return std::unexpected(body.error());
        }
        auto exported =
            close(function, kernel::ProofTerm::equality_elimination(
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

} // namespace cppl::automation
