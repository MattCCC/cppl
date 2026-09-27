#include "cppl/kernel/proof.hpp"

#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/substitution.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"

#include <cstddef>
#include <string>
#include <utility>
#include <variant>

namespace cppl::kernel {

std::string describe(const ProofTerm& proof) {
    if (const auto* arithmetic = std::get_if<LinearArithmetic>(&proof.node)) {
        std::string text = "linear_arithmetic(";
        for (std::size_t index = 0; index < arithmetic->facts.size(); ++index) {
            if (index != 0) {
                text += ", ";
            }
            text += describe(*arithmetic->facts[index].evidence);
        }
        return text + ")";
    }
    if (const auto* branch = std::get_if<ConditionalElimination>(&proof.node)) {
        return "conditional_elim(" + describe(branch->condition) + ", " + describe(*branch->true_case) + ", " +
               describe(*branch->false_case) + ")";
    }
    if (const auto* introduction = std::get_if<ForallIntroduction>(&proof.node)) {
        return "forall_intro(" + describe(introduction->binder) + ", " + describe(*introduction->body) + ")";
    }
    if (const auto* elimination = std::get_if<ForallElimination>(&proof.node)) {
        return "forall_elim(" + describe(*elimination->evidence) + ", " + describe(elimination->argument) + ")";
    }
    if (const auto* assumed = std::get_if<Hypothesis>(&proof.node)) {
        return "hypothesis(" + std::to_string(assumed->index.value) + ")";
    }
    if (const auto* introduction = std::get_if<ImplicationIntroduction>(&proof.node)) {
        return "implies_intro(" + describe(*introduction->premise) + ", " + describe(*introduction->body) + ")";
    }
    if (const auto* application = std::get_if<ImplicationElimination>(&proof.node)) {
        return "implies_elim(" + describe(*application->evidence) + ", " + describe(*application->premise) + ")";
    }
    if (const auto* transport = std::get_if<EqualityElimination>(&proof.node)) {
        return "eq_elim(" + describe(*transport->equality) + ", " + describe(*transport->evidence) + ")";
    }
    if (const auto* introduction = std::get_if<ConjunctionIntroduction>(&proof.node)) {
        return "and_intro(" + describe(*introduction->left) + ", " + describe(*introduction->right) + ")";
    }
    if (const auto* taken = std::get_if<ConjunctionElimination>(&proof.node)) {
        return std::string(taken->right ? "and_elim_right(" : "and_elim_left(") + describe(*taken->evidence) + ")";
    }
    if (const auto* introduction = std::get_if<DisjunctionIntroduction>(&proof.node)) {
        return std::string(introduction->right ? "or_intro_right(" : "or_intro_left(") +
               describe(*introduction->evidence) + ")";
    }
    if (const auto* cases = std::get_if<DisjunctionElimination>(&proof.node)) {
        return "or_elim(" + describe(*cases->evidence) + ", " + describe(*cases->left_case) + ", " +
               describe(*cases->right_case) + ")";
    }
    if (const auto* absurd = std::get_if<FalsityElimination>(&proof.node)) {
        return "false_elim(" + describe(*absurd->evidence) + ")";
    }
    if (const auto* induction = std::get_if<UnsignedInduction>(&proof.node)) {
        return "unsigned_induction(" + describe(induction->binder) + ", " + describe(*induction->base) + ", " +
               describe(*induction->step) + ")";
    }
    return "refl";
}

Proposition induction_base(const IntType& binder, const Proposition& body) {
    return instantiate(body, Term::literal(binder, 0));
}

// `body` states P with its variable as the innermost binder. The step restates
// it underneath one more binder, the `n` it quantifies over, in two ways: as
// P(n), which is `body` itself because the new binder stands exactly where the
// goal's did, and as P(n + 1), which is `body` with its variable replaced by
// `n + 1`. For the latter the body is first lifted past the new binder while
// its own variable is kept as the hole (`shift` above the cutoff), and the
// hole is then filled with `n + 1` by the same capture-safe substitution
// universal elimination uses.
Proposition induction_step(const IntType& binder, const Proposition& body) {
    const Term n = Term::variable(VarIndex{0});
    const Term successor = Term::primitive(PrimOp::AddWrap, binder, {n, Term::literal(binder, 1)});
    const Term below_maximum = Term::primitive(PrimOp::Less, binder, {n, Term::literal(binder, maximum_value(binder))});
    Proposition next = instantiate(shift(body, 1, 1), successor);
    return Proposition::for_all(
        Type{binder},
        Proposition::implication(predicate(below_maximum, true), Proposition::implication(body, std::move(next))));
}

} // namespace cppl::kernel
