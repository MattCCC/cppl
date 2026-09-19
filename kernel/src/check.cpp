#include "cppl/kernel/check.hpp"

#include <utility>
#include <variant>
#include <vector>

#include "cppl/kernel/substitution.hpp"

namespace cppl::kernel {

namespace {

std::unexpected<Rejection> reject(RejectionKind kind, std::string detail) {
    return std::unexpected(Rejection{kind, std::move(detail)});
}

// A goal is checked only after it is known to be a well-formed proposition:
// both sides of every equality must type-check at the stated type under the
// binders that enclose them.
[[nodiscard]] std::expected<void, Rejection> validate_proposition(const Context& context,
                                                                  std::vector<Type>& locals,
                                                                  const Proposition& proposition,
                                                                  const CoreLimits& limits,
                                                                  std::uint32_t depth) {
    if (depth > limits.max_term_depth) {
        return reject(RejectionKind::MalformedProposition,
                      "proposition nests deeper than the core allows");
    }

    if (const auto* quantified = std::get_if<Forall>(&proposition.node)) {
        locals.push_back(quantified->binder);
        auto body = validate_proposition(context, locals, *quantified->body, limits, depth + 1);
        locals.pop_back();
        return body;
    }

    const auto& equality = std::get<Eq>(proposition.node);
    for (const Term* side : {&equality.lhs, &equality.rhs}) {
        auto type = type_of(context, locals, *side, limits);
        if (!type) {
            return reject(RejectionKind::MalformedProposition,
                          describe(type.error().kind) + ": " + type.error().detail);
        }
        if (!(*type == equality.type)) {
            return reject(RejectionKind::MalformedProposition,
                          "equality is stated at " + describe(equality.type) +
                              " but one side has type " + describe(*type));
        }
    }
    return {};
}

[[nodiscard]] std::expected<void, Rejection> check_under(const Context& context,
                                                         std::vector<Type>& locals,
                                                         const Proposition& proposition,
                                                         const ProofTerm& proof,
                                                         const CoreLimits& limits,
                                                         std::uint32_t depth) {
    if (depth > limits.max_term_depth) {
        return reject(RejectionKind::MalformedProofTerm,
                      "proof term nests deeper than the core allows");
    }

    // Universal elimination closes a goal of any shape, because instantiating
    // quantified evidence can leave either an equality or a smaller quantifier.
    // It is therefore decided on the evidence rather than on the goal, and the
    // proposition it yields is derived here and compared with the goal.
    if (const auto* elimination = std::get_if<ForallElimination>(&proof.node)) {
        if (auto well_formed = validate_proposition(context, locals, *elimination->quantified,
                                                    limits, depth + 1);
            !well_formed) {
            return well_formed;
        }

        const auto* eliminated = std::get_if<Forall>(&elimination->quantified->node);
        if (eliminated == nullptr) {
            return reject(RejectionKind::ProofShapeMismatch,
                          "an argument was applied to evidence for " +
                              describe(*elimination->quantified) +
                              ", which quantifies over nothing");
        }

        if (auto evidence = check_under(context, locals, *elimination->quantified,
                                        *elimination->evidence, limits, depth + 1);
            !evidence) {
            return evidence;
        }

        auto argument = type_of(context, locals, elimination->argument, limits);
        if (!argument) {
            return reject(RejectionKind::MalformedProofTerm,
                          describe(argument.error().kind) + ": " + argument.error().detail);
        }
        if (!(*argument == eliminated->binder)) {
            return reject(RejectionKind::ProofShapeMismatch,
                          "evidence quantifying over " + describe(eliminated->binder) +
                              " is instantiated at " + describe(context, elimination->argument) +
                              ", which has type " + describe(*argument));
        }

        const Proposition instantiated = instantiate(*eliminated->body, elimination->argument);
        if (!(instantiated == proposition)) {
            return reject(RejectionKind::ProofShapeMismatch,
                          "instantiating that evidence establishes " + describe(instantiated) +
                              ", which is not the goal " + describe(proposition));
        }
        return {};
    }

    if (const auto* quantified = std::get_if<Forall>(&proposition.node)) {
        const auto* introduction = std::get_if<ForallIntroduction>(&proof.node);
        if (introduction == nullptr) {
            return reject(RejectionKind::ProofShapeMismatch,
                          "a universally quantified goal requires forall-introduction");
        }
        if (!(introduction->binder == quantified->binder)) {
            return reject(RejectionKind::ProofShapeMismatch,
                          "evidence introduces a binder of type " + describe(introduction->binder) +
                              " but the goal quantifies over " + describe(quantified->binder));
        }

        locals.push_back(quantified->binder);
        auto body = check_under(context, locals, *quantified->body, *introduction->body, limits,
                                depth + 1);
        locals.pop_back();
        return body;
    }

    const auto& equality = std::get<Eq>(proposition.node);
    if (!std::holds_alternative<Reflexivity>(proof.node)) {
        return reject(RejectionKind::ProofShapeMismatch,
                      "an equality goal is not introduced by forall-introduction");
    }

    auto lhs = normalize(context, equality.lhs, limits);
    if (!lhs) {
        return reject(RejectionKind::CoreFailure,
                      describe(lhs.error().kind) + ": " + lhs.error().detail);
    }
    auto rhs = normalize(context, equality.rhs, limits);
    if (!rhs) {
        return reject(RejectionKind::CoreFailure,
                      describe(rhs.error().kind) + ": " + rhs.error().detail);
    }

    if (!(*lhs == *rhs)) {
        return reject(RejectionKind::NotDefinitionallyEqual,
                      "reflexivity requires definitionally equal terms, but " +
                          describe(context, equality.lhs) + " reduces to " +
                          describe(context, *lhs) + " while " + describe(context, equality.rhs) +
                          " reduces to " + describe(context, *rhs));
    }
    return {};
}

}  // namespace

std::string describe(RejectionKind kind) {
    switch (kind) {
        case RejectionKind::MalformedProposition:
            return "malformed proposition";
        case RejectionKind::MalformedProofTerm:
            return "malformed proof term";
        case RejectionKind::ProofShapeMismatch:
            return "proof shape mismatch";
        case RejectionKind::NotDefinitionallyEqual:
            return "terms are not definitionally equal";
        case RejectionKind::CoreFailure:
            return "core failure";
    }
    return "unknown rejection";
}

std::expected<Acceptance, Rejection> check(const Context& context,
                                           const Proposition& proposition,
                                           const ProofTerm& proof,
                                           const CoreLimits& limits) {
    std::vector<Type> locals;

    if (auto well_formed = validate_proposition(context, locals, proposition, limits, 0);
        !well_formed) {
        return std::unexpected(well_formed.error());
    }

    locals.clear();
    if (auto checked = check_under(context, locals, proposition, proof, limits, 0); !checked) {
        return std::unexpected(checked.error());
    }

    return Acceptance{proposition};
}

}  // namespace cppl::kernel
