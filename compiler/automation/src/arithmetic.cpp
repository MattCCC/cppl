#include "arithmetic.hpp"

#include "cppl/kernel/box.hpp"
#include "cppl/kernel/context.hpp"
#include "cppl/kernel/linear.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/substitution.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/obligations/obligation.hpp"
#include "cppl/refutation/refute.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <set>
#include <utility>
#include <variant>
#include <vector>

namespace cppl::automation {

namespace {

namespace k = kernel;
using Wide = k::Wide;

constexpr std::size_t kMaxRewrites = 32;
constexpr std::size_t kMaxDerivations = 48;
// Case analyses one proof search may open. Each one doubles the work below it,
// so the bound keeps a failing search from growing without end.
constexpr std::size_t kMaxCaseSplits = 24;
// Case analyses on a selection a premise states something about, which are
// attempted only where nothing else closed a goal, so each one is bounded on
// its own and does not draw on the splits above.
constexpr std::size_t kMaxSelectionSplits = 16;
// Implications in scope whose premise is proven rather than found, which are
// likewise attempted only where nothing else closed a goal.
constexpr std::size_t kMaxImpliedPremises = 16;
// The widest machine type whose values this search will enumerate side by side.
//
// A resource threshold, not a semantic boundary. Every machine type is finite
// and its equality is decidable, so a wider type is no less decidable; covering
// one by enumeration just costs a case per value, which stops being worth
// constructing well before 2^32 of them. Raising this changes what the search
// spends, never what is true.
constexpr std::uint16_t kMaxEnumeratedWidth = 4;

bool occurs(const k::Term& inside, const k::Term& target) {
    if (inside == target) {
        return true;
    }
    if (const auto* call = std::get_if<k::Call>(&inside.node)) {
        return std::ranges::any_of(call->arguments, [&](const k::Term& argument) { return occurs(argument, target); });
    }
    if (const auto* primitive = std::get_if<k::Prim>(&inside.node)) {
        return std::ranges::any_of(primitive->arguments,
                                   [&](const k::Term& argument) { return occurs(argument, target); });
    }
    return false;
}

void variables_of(const k::Term& term, std::set<std::uint32_t>& found) {
    if (const auto* variable = std::get_if<k::Var>(&term.node)) {
        found.insert(variable->index.value);
    } else if (const auto* call = std::get_if<k::Call>(&term.node)) {
        for (const auto& argument : call->arguments)
            variables_of(argument, found);
    } else if (const auto* primitive = std::get_if<k::Prim>(&term.node)) {
        for (const auto& argument : primitive->arguments)
            variables_of(argument, found);
    }
}

struct Premise {
    k::Proposition proposition;
    std::size_t binders = 0;
    // The disjunctions of this premise already being taken cases on, by their
    // position among `disjunctions_of` it. Its hypothesis stays where it is,
    // so the indices of the others do not move, but the analysis does not open
    // one of them a second time.
    std::vector<std::size_t> opened = {};
    // Whether the selection this premise states something about is already
    // being taken cases on.
    bool selected = false;
};

// An equality usable for rewriting, with its evidence at the leaf.
struct Equality {
    k::Type type;
    k::Term lhs;
    k::Term rhs;
    k::ProofTerm evidence;
};

struct Rewrite {
    Equality equality;
    k::Proposition motive;
};

class Prover {
  public:
    Prover(const k::Context& context, bool rewriting) : context_(context), rewriting_(rewriting) {}

    std::optional<k::ProofTerm> prove(const k::Proposition& goal) {
        if (const auto* quantified = std::get_if<k::Forall>(&goal.node)) {
            binders_.push_back(quantified->binder);
            auto body = prove(*quantified->body);
            binders_.pop_back();
            if (!body)
                return std::nullopt;
            return k::ProofTerm::forall_introduction(quantified->binder, std::move(*body));
        }
        if (const auto* implication = std::get_if<k::Implies>(&goal.node)) {
            premises_.push_back(Premise{*implication->premise, binders_.size()});
            auto body = prove(*implication->conclusion);
            premises_.pop_back();
            if (!body)
                return std::nullopt;
            return k::ProofTerm::implication_introduction(*implication->premise, std::move(*body));
        }
        if (const auto* conjunction = std::get_if<k::And>(&goal.node)) {
            auto left = prove(*conjunction->left);
            if (!left)
                return std::nullopt;
            auto right = prove(*conjunction->right);
            if (!right)
                return std::nullopt;
            return k::ProofTerm::conjunction_introduction(std::move(*left), std::move(*right));
        }
        // A disjunctive premise in scope is taken apart before the goal is
        // approached, because each side may be what closes it.
        if (auto cases = by_premise_cases(goal)) {
            return cases;
        }
        if (std::holds_alternative<k::Or>(goal.node)) {
            if (auto chosen = by_disjunction(goal)) {
                return chosen;
            }
            if (auto branch = by_conditional(goal)) {
                return branch;
            }
            return by_implied_or_selection(goal);
        }
        if (auto branch = by_conditional(goal)) {
            return branch;
        }
        if (auto closed = rewriting_ ? rewrite_then_close(goal) : by_arithmetic(goal)) {
            return closed;
        }
        return by_implied_or_selection(goal);
    }

  private:
    // A premise and, by conjunction elimination, each side of it, each with the
    // evidence that reaches it.
    static void offer(const k::Proposition& proposition, k::ProofTerm evidence,
                      std::vector<std::pair<k::Proposition, k::ProofTerm>>& available) {
        available.emplace_back(proposition, evidence);
        if (const auto* conjunction = std::get_if<k::And>(&proposition.node)) {
            offer(*conjunction->left, k::ProofTerm::conjunction_elimination(proposition, evidence, false), available);
            offer(*conjunction->right, k::ProofTerm::conjunction_elimination(proposition, std::move(evidence), true),
                  available);
        }
    }

    // The premises in scope, restated at the leaf, each with its hypothesis,
    // and the conclusion of every implication among them whose premise is
    // available exactly as stated, by implication elimination: a postcondition
    // relating a Boolean result to a proposition, with the path's fact about
    // that result (SPEC.md RUNTIMECHECK-006, RUNTIMECHECK-011). Each
    // implication is used at most once. Only equality leaves enter arithmetic.
    std::vector<k::ArithmeticFact> facts() const {
        std::vector<std::pair<k::Proposition, k::ProofTerm>> available;
        for (std::size_t index = 0; index < premises_.size(); ++index) {
            const Premise& premise = premises_[index];
            offer(
                k::shift(premise.proposition, static_cast<std::uint32_t>(binders_.size() - premise.binders)),
                k::ProofTerm::hypothesis(k::HypothesisIndex{static_cast<std::uint32_t>(premises_.size() - 1 - index)}),
                available);
        }
        std::vector<bool> used(available.size(), false);
        for (bool progress = true; progress;) {
            progress = false;
            for (std::size_t index = 0; index < available.size(); ++index) {
                used.resize(available.size(), false);
                const auto* implication = std::get_if<k::Implies>(&available[index].first.node);
                if (used[index] || implication == nullptr) {
                    continue;
                }
                const auto premise = std::ranges::find_if(
                    available, [&](const auto& entry) { return entry.first == *implication->premise; });
                if (premise == available.end()) {
                    continue;
                }
                used[index] = true;
                k::Proposition conclusion = *implication->conclusion;
                k::ProofTerm evidence = k::ProofTerm::implication_elimination(available[index].first,
                                                                              available[index].second, premise->second);
                offer(conclusion, std::move(evidence), available);
                progress = true;
            }
        }
        std::vector<k::ArithmeticFact> result;
        for (auto& [proposition, evidence] : available) {
            if (std::holds_alternative<k::Eq>(proposition.node) &&
                std::ranges::none_of(
                    result, [&](const k::ArithmeticFact& known) { return known.proposition == proposition; })) {
                result.push_back(k::ArithmeticFact{proposition, k::Box<k::ProofTerm>{std::move(evidence)}});
            }
        }
        return result;
    }

    // The disjunctions a premise states, itself or as a side of a conjunction,
    // each with the evidence conjunction elimination reaches it by: a
    // postcondition `r <= a && r <= b && (r == a || r == b)` is supposed whole,
    // and its disjunction is still a case analysis to take.
    static void disjunctions_of(const k::Proposition& proposition, k::ProofTerm evidence,
                                std::vector<std::pair<k::Proposition, k::ProofTerm>>& found) {
        if (std::holds_alternative<k::Or>(proposition.node)) {
            found.emplace_back(proposition, std::move(evidence));
        } else if (const auto* conjunction = std::get_if<k::And>(&proposition.node)) {
            disjunctions_of(*conjunction->left, k::ProofTerm::conjunction_elimination(proposition, evidence, false),
                            found);
            disjunctions_of(*conjunction->right,
                            k::ProofTerm::conjunction_elimination(proposition, std::move(evidence), true), found);
        }
    }

    // Evidence for `goal` by taking cases on a disjunction a premise in scope
    // states. The goal is established under each side separately; nothing
    // here learns which side holds, and the kernel checks each case against
    // its own side.
    std::optional<k::ProofTerm> by_premise_cases(const k::Proposition& goal) {
        for (std::size_t index = premises_.size(); index > 0; --index) {
            const auto shifted = k::shift(premises_[index - 1].proposition,
                                          static_cast<std::uint32_t>(binders_.size() - premises_[index - 1].binders));
            std::vector<std::pair<k::Proposition, k::ProofTerm>> found;
            disjunctions_of(
                shifted,
                k::ProofTerm::hypothesis(k::HypothesisIndex{static_cast<std::uint32_t>(premises_.size() - index)}),
                found);
            for (std::size_t position = 0; position < found.size(); ++position) {
                if (std::ranges::contains(premises_[index - 1].opened, position)) {
                    continue;
                }
                if (++splits_ > kMaxCaseSplits) {
                    return std::nullopt;
                }
                const auto& disjunction = std::get<k::Or>(found[position].first.node);
                // The disjunction is used up by this analysis. Its premise's
                // hypothesis stays in place, so the indices of the premises
                // around it do not move, but the search does not open it again.
                premises_[index - 1].opened.push_back(position);
                auto left = under_premise(*disjunction.left, [&] { return prove(goal); });
                std::optional<k::ProofTerm> right;
                if (left) {
                    right = under_premise(*disjunction.right, [&] { return prove(goal); });
                }
                premises_[index - 1].opened.pop_back();
                if (!left || !right) {
                    continue;
                }
                return k::ProofTerm::disjunction_elimination(found[position].first, found[position].second,
                                                             std::move(*left), std::move(*right));
            }
        }
        return std::nullopt;
    }

    // Evidence for a goal about a selection, by taking cases on its condition.
    // Each branch supposes the condition's own truth and states the goal with
    // that branch's value in place of the selection, which is what the kernel
    // derives from the motive itself. A disjunction no side of which holds
    // alone is about the selection in its sides: `r == 0 || x > 0`, where `r`
    // is `x > 0 ? 1 : 0`, holds by one side in each case.
    std::optional<k::ProofTerm> by_conditional(const k::Proposition& goal) {
        const auto selection = selection_in(goal);
        if (!selection) {
            return std::nullopt;
        }
        if (++splits_ > kMaxCaseSplits) {
            return std::nullopt;
        }
        const auto& branch = std::get<k::Prim>(selection->node);
        auto motive = obligations::rewrite_context(goal, *selection);
        if (!motive) {
            return std::nullopt;
        }
        auto when_true = under_premise(k::predicate(branch.arguments[0], true),
                                       [&] { return prove(k::instantiate(*motive, branch.arguments[1])); });
        if (!when_true) {
            return std::nullopt;
        }
        auto when_false = under_premise(k::predicate(branch.arguments[0], false),
                                        [&] { return prove(k::instantiate(*motive, branch.arguments[2])); });
        if (!when_false) {
            return std::nullopt;
        }
        return k::ProofTerm::conditional_elimination(k::Type{branch.type}, branch.arguments[0], branch.arguments[1],
                                                     branch.arguments[2], *motive, std::move(*when_true),
                                                     std::move(*when_false));
    }

    // What is tried once nothing else closed a goal: an implication in scope
    // whose premise is proven, then a case analysis on a premise's selection.
    std::optional<k::ProofTerm> by_implied_or_selection(const k::Proposition& goal) {
        if (auto implied = by_implied_premise(goal)) {
            return implied;
        }
        return by_premise_selection(goal);
    }

    // Evidence for `goal` from an implication in scope, a premise or a side of
    // one, whose own premise is not in scope as stated but is proven here: a
    // callee's `(s == closed && e != button) -> r == closed`, called where
    // `s == closed` is supposed and `e` is `timeout`. Its conclusion is then
    // supposed for the goal, and the kernel checks both steps of that cut.
    // An implication being used this way is not used again inside the proof
    // of its own premise or under its conclusion.
    std::optional<k::ProofTerm> by_implied_premise(const k::Proposition& goal) {
        std::vector<std::pair<k::Proposition, k::ProofTerm>> available;
        for (std::size_t index = 0; index < premises_.size(); ++index) {
            const Premise& premise = premises_[index];
            offer(
                k::shift(premise.proposition, static_cast<std::uint32_t>(binders_.size() - premise.binders)),
                k::ProofTerm::hypothesis(k::HypothesisIndex{static_cast<std::uint32_t>(premises_.size() - 1 - index)}),
                available);
        }
        for (const auto& [proposition, evidence] : available) {
            const auto* implication = std::get_if<k::Implies>(&proposition.node);
            if (implication == nullptr || std::ranges::contains(implying_, proposition) ||
                std::ranges::any_of(available,
                                    [&](const auto& entry) { return entry.first == *implication->premise; })) {
                continue;
            }
            if (++implied_premises_ > kMaxImpliedPremises) {
                return std::nullopt;
            }
            const std::size_t spent = splits_;
            implying_.push_back(proposition);
            auto premise = prove(*implication->premise);
            std::optional<k::ProofTerm> body;
            if (premise) {
                body = under_premise(*implication->conclusion, [&] { return prove(goal); });
            }
            implying_.pop_back();
            if (!premise || !body) {
                splits_ = spent;
                continue;
            }
            return k::ProofTerm::implication_elimination(
                k::Proposition::implication(*implication->conclusion, goal), std::move(*body),
                k::ProofTerm::implication_elimination(proposition, evidence, std::move(*premise)));
        }
        return std::nullopt;
    }

    // Evidence for `goal` by taking cases on a selection a premise states
    // something about: a route taken where `any`, for `any = x > 0 ? true :
    // y > 0`, supposes `(x > 0 ? true : y > 0) == true`, which says something
    // of `x` and `y` only case by case. Each branch supposes the condition's outcome
    // and the premise with that branch's value in place of the selection,
    // and the kernel derives `premise -> goal` from the motive itself.
    //
    // Attempted only where nothing else closed the goal, so a proof that needs
    // no such split is found exactly as before. A failed attempt gives back
    // the case splits its branches spent.
    std::optional<k::ProofTerm> by_premise_selection(const k::Proposition& goal) {
        for (std::size_t index = premises_.size(); index > 0; --index) {
            if (premises_[index - 1].selected) {
                continue;
            }
            const auto premise = k::shift(premises_[index - 1].proposition,
                                          static_cast<std::uint32_t>(binders_.size() - premises_[index - 1].binders));
            const auto* equality = std::get_if<k::Eq>(&premise.node);
            const auto selection = equality != nullptr ? first_selection(*equality) : std::nullopt;
            if (!selection) {
                continue;
            }
            auto stated = obligations::rewrite_context(premise, *selection);
            if (!stated) {
                continue;
            }
            if (++selection_splits_ > kMaxSelectionSplits) {
                return std::nullopt;
            }
            const auto& branch = std::get<k::Prim>(selection->node);
            const auto arm = [&](bool taken) {
                return under_premise(k::predicate(branch.arguments[0], taken), [&] {
                    return under_premise(k::instantiate(*stated, branch.arguments[taken ? 1 : 2]),
                                         [&] { return prove(goal); });
                });
            };
            const std::size_t spent = splits_;
            premises_[index - 1].selected = true;
            auto when_true = arm(true);
            auto when_false = when_true ? arm(false) : std::nullopt;
            premises_[index - 1].selected = false;
            if (!when_true || !when_false) {
                splits_ = spent;
                continue;
            }
            auto cases = k::ProofTerm::conditional_elimination(
                k::Type{branch.type}, branch.arguments[0], branch.arguments[1], branch.arguments[2],
                k::Proposition::implication(*stated, k::shift(goal, 1)), std::move(*when_true), std::move(*when_false));
            return k::ProofTerm::implication_elimination(
                k::Proposition::implication(premise, goal), std::move(cases),
                k::ProofTerm::hypothesis(k::HypothesisIndex{static_cast<std::uint32_t>(premises_.size() - index)}));
        }
        return std::nullopt;
    }

    // The leftmost selection an equality or a disjunction of them states
    // something about.
    static std::optional<k::Term> selection_in(const k::Proposition& goal) {
        if (const auto* equality = std::get_if<k::Eq>(&goal.node)) {
            return first_selection(*equality);
        }
        if (const auto* disjunction = std::get_if<k::Or>(&goal.node)) {
            auto found = selection_in(*disjunction->left);
            return found ? found : selection_in(*disjunction->right);
        }
        return std::nullopt;
    }

    // The leftmost selection occurring in an equality, if any. Its branches are
    // what the case analysis puts in its place.
    static std::optional<k::Term> first_selection(const k::Eq& equality) {
        std::optional<k::Term> found;
        const auto visit = [&](auto&& self, const k::Term& term) -> void {
            if (found) {
                return;
            }
            if (const auto* primitive = std::get_if<k::Prim>(&term.node)) {
                for (const auto& argument : primitive->arguments) {
                    self(self, argument);
                }
                if (!found && primitive->op == k::PrimOp::Select && primitive->arguments.size() == 3) {
                    found = term;
                }
            } else if (const auto* call = std::get_if<k::Call>(&term.node)) {
                for (const auto& argument : call->arguments) {
                    self(self, argument);
                }
            }
        };
        visit(visit, equality.lhs);
        visit(visit, equality.rhs);
        return found;
    }

    // Runs `body` with `premise` in scope, and wraps what it returns in the
    // introduction that puts the premise there. The premise is recorded at the
    // current binder depth, so the hypothesis indices the leaves use match the
    // introductions the proof term actually has.
    template <typename Body> std::optional<k::ProofTerm> under_premise(k::Proposition premise, Body&& body) {
        premises_.push_back(Premise{premise, binders_.size()});
        auto proof = std::forward<Body>(body)();
        premises_.pop_back();
        if (!proof) {
            return std::nullopt;
        }
        return k::ProofTerm::implication_introduction(std::move(premise), std::move(*proof));
    }

    // Case analysis on a boolean `condition`, as a selection between one and
    // zero whose motive ignores the value. Both branch proofs are implications
    // from the condition's own truth, which is what the kernel derives for them.
    static k::ProofTerm cases_on(const k::Term& condition, const k::Proposition& goal, k::ProofTerm when_true,
                                 k::ProofTerm when_false) {
        return k::ProofTerm::conditional_elimination(k::Type{k::kBoolean}, condition, k::Term::literal(k::kBoolean, 1),
                                                     k::Term::literal(k::kBoolean, 0), k::shift(goal, 1),
                                                     std::move(when_true), std::move(when_false));
    }

    // Evidence for a disjunctive goal by establishing one of its sides. Which
    // side holds is not decided here: each is attempted and the kernel accepts
    // only evidence that really establishes the side it names.
    std::optional<k::ProofTerm> by_disjunction(const k::Proposition& goal) {
        const auto& disjunction = std::get<k::Or>(goal.node);
        for (const bool right : {false, true}) {
            const k::Proposition& side = right ? *disjunction.right : *disjunction.left;
            if (auto evidence = prove(side)) {
                return k::ProofTerm::disjunction_introduction(std::move(*evidence), right);
            }
        }
        // No side holds on its own. Where a decidability principle covers the
        // sides, the goal is still provable by taking them as cases.
        if (!can_decide(goal)) {
            return std::nullopt;
        }
        return derive_decidable_cases(goal);
    }

    // The sides of a disjunction, flattened. Nesting associates to the right,
    // so an enumeration of several values is a chain of them.
    static void sides_of(const k::Proposition& proposition, std::vector<const k::Proposition*>& found) {
        if (const auto* disjunction = std::get_if<k::Or>(&proposition.node)) {
            sides_of(*disjunction->left, found);
            sides_of(*disjunction->right, found);
            return;
        }
        found.push_back(&proposition);
    }

    // Whether the sides of `goal` are covered by the order of one pair of
    // terms. Machine order is total and decided at every width, so no size
    // threshold applies to this principle.
    static bool decided_by_order(const std::vector<const k::Proposition*>& sides) {
        std::optional<std::pair<k::Term, k::Term>> pair;
        for (const k::Proposition* side : sides) {
            const auto compared = compared_terms(*side);
            if (!compared || (pair && !(*compared == *pair))) {
                return false;
            }
            pair = compared;
        }
        return true;
    }

    // Whether the sides of `goal` name every value of one machine type: one
    // term equated to a literal by each side, with a side per value.
    //
    // Capped at `kMaxEnumeratedWidth`, which is a cost threshold of this search
    // and not a claim about decidability. A wider type is equally finite and
    // its equality equally decidable; enumerating it is merely expensive.
    static bool decided_by_enumeration(const std::vector<const k::Proposition*>& sides) {
        std::optional<k::Term> subject;
        std::optional<k::IntType> type;
        for (const k::Proposition* side : sides) {
            const auto* equality = std::get_if<k::Eq>(&side->node);
            if (equality == nullptr || !equality->type.is_integer()) {
                return false;
            }
            const k::IntType& integer = equality->type.integer_type();
            if (integer.width > kMaxEnumeratedWidth || (type && !(integer == *type))) {
                return false;
            }
            type = integer;
            const bool literal_right = std::holds_alternative<k::Literal>(equality->rhs.node);
            const bool literal_left = std::holds_alternative<k::Literal>(equality->lhs.node);
            if (literal_right == literal_left) {
                return false;
            }
            const k::Term& value = literal_right ? equality->lhs : equality->rhs;
            if (subject && !(value == *subject)) {
                return false;
            }
            subject = value;
        }
        // The values must be distinct, so a side for each one covers the type.
        return type && sides.size() >= (std::size_t{1} << type->width);
    }

    // Whether this search will construct a case analysis for `goal`, by some
    // decidability principle it knows (`SPEC.md` 7.8).
    //
    // Every principle recognized here is one about machine integers, and each
    // is checked against the goal's sides. New decidable relations should be
    // added as further principles feeding this one decision, so that what the
    // search will derive stays stated in a single place.
    //
    // This answers what the search *builds*, not what is decidable. A goal it
    // declines is not thereby false or undecidable: a proof of the same goal
    // arrived at another way is checked by the kernel on its own merits.
    static bool can_decide(const k::Proposition& goal) {
        std::vector<const k::Proposition*> sides;
        sides_of(goal, sides);
        if (sides.size() < 2) {
            return false;
        }
        return decided_by_order(sides) || decided_by_enumeration(sides) || decided_by_first_order(sides);
    }

    // Whether the first side orders two machine terms. Machine order is total,
    // so where that order fails its converse holds, and the rest of the goal
    // has to follow from the converse: `b0 > 0 || (a == a0 && b == 0)` is
    // proven where `b0 <= 0` gives the second side. Its sides need compare
    // nothing in common.
    //
    // Only an order stated to hold is split on this way. An equality, and a
    // side stating that a comparison fails, are cases only where the
    // principles above cover every side, so `x == 0 || x != 0` is still not
    // built from a split the goal itself spells (`tests/e2e/disjunction.sh`).
    static bool decided_by_first_order(const std::vector<const k::Proposition*>& sides) {
        const auto* equality = std::get_if<k::Eq>(&sides.front()->node);
        if (equality == nullptr || !(equality->type == k::Type{k::kBoolean})) {
            return false;
        }
        const auto* literal = std::get_if<k::Literal>(&equality->rhs.node);
        const auto* primitive = std::get_if<k::Prim>(&equality->lhs.node);
        if (literal == nullptr || literal->value != 1 || primitive == nullptr || primitive->arguments.size() != 2) {
            return false;
        }
        switch (primitive->op) {
            case k::PrimOp::Less:
            case k::PrimOp::LessEqual:
            case k::PrimOp::Greater:
            case k::PrimOp::GreaterEqual:
                return true;
            default:
                return false;
        }
    }

    // The two terms a proposition compares, when it is an order or equality
    // between the same pair at the same type.
    static std::optional<std::pair<k::Term, k::Term>> compared_terms(const k::Proposition& proposition) {
        const auto* equality = std::get_if<k::Eq>(&proposition.node);
        if (equality == nullptr) {
            return std::nullopt;
        }
        if (equality->type == k::Type{k::kBoolean}) {
            for (const auto* side : {&equality->lhs, &equality->rhs}) {
                const auto* other = side == &equality->lhs ? &equality->rhs : &equality->lhs;
                const auto* literal = std::get_if<k::Literal>(&other->node);
                if (literal == nullptr || literal->value != 1) {
                    continue;
                }
                if (const auto* primitive = std::get_if<k::Prim>(&side->node);
                    primitive != nullptr && is_comparison(primitive->op) && primitive->arguments.size() == 2) {
                    return std::pair{primitive->arguments[0], primitive->arguments[1]};
                }
            }
            return std::nullopt;
        }
        if (!equality->type.is_integer()) {
            return std::nullopt;
        }
        return std::pair{equality->lhs, equality->rhs};
    }

    // The case analysis `can_decide` promised. No side holds alone, so the goal
    // is proven by splitting on the first side's decision: where it holds the
    // goal is that side, and where it fails that failure is a premise for the
    // rest.
    //
    // The split is on a comparison the machine decides, so the two branches are
    // exhaustive by construction. Each branch is closed by the ordinary search,
    // and every step is a kernel rule checked independently.
    //
    // Every case comes from a decidability principle applied to a machine
    // comparison. Nothing here grants `P || not P` for an arbitrary `P`: a goal
    // that needs excluded middle over an undecided proposition stays unproven
    // (`SPEC.md` 7.8, `FOUNDATIONS.md` 2.6).
    std::optional<k::ProofTerm> derive_decidable_cases(const k::Proposition& goal) {
        if (++splits_ > kMaxCaseSplits) {
            return std::nullopt;
        }
        const auto& disjunction = std::get<k::Or>(goal.node);
        const auto decision = decide(*disjunction.left);
        if (!decision) {
            return std::nullopt;
        }
        // Each branch establishes the whole disjunction, by the side its own
        // premise settles. The introduction of that side sits inside the
        // introduction of the premise, because the kernel states each branch of
        // a case analysis as an implication from the condition's own truth.
        auto when_true = under_premise(k::predicate(*decision, true), [&] {
            auto side = prove(*disjunction.left);
            if (!side) {
                return side;
            }
            return std::optional{k::ProofTerm::disjunction_introduction(std::move(*side), false)};
        });
        if (!when_true) {
            return std::nullopt;
        }
        auto when_false = under_premise(k::predicate(*decision, false), [&] {
            // The remaining sides are the rest of the same enumeration, so they
            // are continued directly rather than judged as a domain again.
            auto side = std::holds_alternative<k::Or>(disjunction.right->node) ? by_side_or_cases(*disjunction.right)
                                                                               : prove(*disjunction.right);
            if (!side) {
                return side;
            }
            return std::optional{k::ProofTerm::disjunction_introduction(std::move(*side), true)};
        });
        if (!when_false) {
            return std::nullopt;
        }
        return cases_on(*decision, goal, std::move(*when_true), std::move(*when_false));
    }

    // One side of an enumeration, or a further split of the sides that remain.
    // The domain was judged where the whole disjunction was in hand.
    std::optional<k::ProofTerm> by_side_or_cases(const k::Proposition& goal) {
        const auto& disjunction = std::get<k::Or>(goal.node);
        for (const bool right : {false, true}) {
            const k::Proposition& side = right ? *disjunction.right : *disjunction.left;
            if (auto evidence = prove(side)) {
                return k::ProofTerm::disjunction_introduction(std::move(*evidence), right);
            }
        }
        return derive_decidable_cases(goal);
    }

    // The boolean term whose truth is exactly `proposition`, when the machine
    // decides it: a comparison stated as `comparison == true`, or an equality
    // of integers, which `Equal` decides. Anything else has no decision
    // procedure here and no case analysis is offered for it.
    static std::optional<k::Term> decide(const k::Proposition& proposition) {
        const auto* equality = std::get_if<k::Eq>(&proposition.node);
        if (equality == nullptr || !equality->type.is_integer()) {
            return std::nullopt;
        }
        if (equality->type == k::Type{k::kBoolean}) {
            for (const auto* side : {&equality->lhs, &equality->rhs}) {
                const auto* other = side == &equality->lhs ? &equality->rhs : &equality->lhs;
                const auto* literal = std::get_if<k::Literal>(&other->node);
                if (literal == nullptr || literal->value != 1) {
                    continue;
                }
                if (const auto* primitive = std::get_if<k::Prim>(&side->node);
                    primitive != nullptr && is_comparison(primitive->op)) {
                    return *side;
                }
            }
        }
        return k::Term::primitive(k::PrimOp::Equal, equality->type.integer_type(), {equality->lhs, equality->rhs});
    }

    // A goal arithmetic cannot state, such as an equality of structured values,
    // still follows from premises that are contradictory on their own: they are
    // refuted into `False` with no goal taking part, and the goal is closed from
    // that by falsity elimination.
    std::optional<k::ProofTerm> by_arithmetic(const k::Proposition& goal) const {
        const auto* equality = std::get_if<k::Eq>(&goal.node);
        if (equality == nullptr && !std::holds_alternative<k::Falsity>(goal.node)) {
            return std::nullopt;
        }
        const bool stated = equality == nullptr || equality->type.is_integer();
        std::vector<k::ArithmeticFact> used = facts();
        std::vector<k::Proposition> propositions;
        propositions.reserve(used.size());
        for (const auto& fact : used)
            propositions.push_back(fact.proposition);
        const auto system = k::arithmetic_system(context_, propositions, stated ? goal : k::Proposition::falsity(),
                                                 k::CoreLimits{}, binders_);
        if (!system) {
            return std::nullopt;
        }
        auto certificate = refutation::refute(*system);
        if (!certificate) {
            return std::nullopt;
        }
        auto proof = k::ProofTerm::linear_arithmetic(std::move(used), std::move(*certificate));
        return stated ? proof : k::ProofTerm::falsity_elimination(std::move(proof));
    }

    bool definitional(const k::Proposition& goal) const {
        const auto* equality = std::get_if<k::Eq>(&goal.node);
        if (equality == nullptr)
            return false;
        const auto lhs = k::normalize(context_, equality->lhs, k::CoreLimits{});
        const auto rhs = k::normalize(context_, equality->rhs, k::CoreLimits{});
        return lhs && rhs && *lhs == *rhs;
    }

    k::Type type_of_variable(std::uint32_t index) const {
        return binders_[binders_.size() - 1 - index];
    }

    // Rewrites with the premises' equalities, and with equalities between
    // variables that arithmetic establishes, until nothing applies; then closes
    // what is left definitionally or by arithmetic.
    std::optional<k::ProofTerm> rewrite_then_close(const k::Proposition& goal) {
        std::vector<Equality> known;
        for (const auto& fact : facts()) {
            const auto& equality = std::get<k::Eq>(fact.proposition.node);
            if (equality.type == k::Type{k::kBoolean})
                continue;
            known.push_back(Equality{equality.type, equality.lhs, equality.rhs, *fact.evidence});
        }

        std::vector<Rewrite> steps;
        k::Proposition current = goal;
        std::size_t derivations = 0;
        for (int round = 0; round < 4; ++round) {
            const bool changed = rewrite(current, known, steps);
            std::vector<Equality> derived = derive(current, known, derivations);
            if (derived.empty() && !changed)
                break;
            for (auto& equality : derived)
                known.push_back(std::move(equality));
        }

        std::optional<k::ProofTerm> proof;
        if (definitional(current)) {
            proof = k::ProofTerm::reflexivity();
        } else {
            proof = by_arithmetic(current);
        }
        if (!proof)
            return std::nullopt;
        for (auto& step : std::views::reverse(steps)) {
            proof = k::ProofTerm::equality_elimination(step.equality.type, step.equality.lhs, step.equality.rhs,
                                                       step.motive, step.equality.evidence, std::move(*proof));
        }
        return proof;
    }

    // Rewrites left to right wherever a left side occurs, never with an
    // equality whose right side contains its left side.
    static bool rewrite(k::Proposition& current, const std::vector<Equality>& known, std::vector<Rewrite>& steps) {
        bool changed = false;
        for (std::size_t budget = 0; budget < kMaxRewrites; ++budget) {
            bool applied = false;
            for (const auto& equality : std::views::reverse(known)) {
                if (equality.lhs == equality.rhs || occurs(equality.rhs, equality.lhs))
                    continue;
                auto motive = obligations::rewrite_context(current, equality.lhs);
                if (!motive)
                    continue;
                current = k::instantiate(*motive, equality.rhs);
                steps.push_back(Rewrite{equality, std::move(*motive)});
                applied = true;
                break;
            }
            if (!applied)
                break;
            changed = true;
        }
        return changed;
    }

    // Equalities between a variable of the goal and another variable of the
    // same type, where arithmetic over the premises proves them.
    std::vector<Equality> derive(const k::Proposition& current, const std::vector<Equality>& known,
                                 std::size_t& derivations) const {
        std::vector<Equality> derived;
        const auto* equality = std::get_if<k::Eq>(&current.node);
        if (equality == nullptr)
            return derived;
        std::set<std::uint32_t> mentioned;
        variables_of(equality->lhs, mentioned);
        variables_of(equality->rhs, mentioned);
        for (const std::uint32_t variable : mentioned) {
            if (variable >= binders_.size())
                continue;
            for (std::uint32_t other = 0; other < binders_.size(); ++other) {
                if (other == variable || !(type_of_variable(other) == type_of_variable(variable))) {
                    continue;
                }
                const auto lhs = k::Term::variable(k::VarIndex{variable});
                const auto rhs = k::Term::variable(k::VarIndex{other});
                const bool already = std::ranges::any_of(known, [&](const Equality& e) {
                    return (e.lhs == lhs && e.rhs == rhs) || (e.lhs == rhs && e.rhs == lhs);
                });
                if (already || ++derivations > kMaxDerivations)
                    continue;
                const k::Type type = type_of_variable(variable);
                const auto proposition = k::Proposition::equality(type, lhs, rhs);
                if (auto proof = by_arithmetic(proposition)) {
                    derived.push_back(Equality{type, lhs, rhs, std::move(*proof)});
                }
            }
        }
        return derived;
    }

    const k::Context& context_;
    bool rewriting_;
    std::vector<k::Type> binders_;
    std::vector<Premise> premises_;
    std::size_t splits_ = 0;
    std::size_t selection_splits_ = 0;
    std::size_t implied_premises_ = 0;
    std::vector<k::Proposition> implying_;
};

} // namespace

std::optional<kernel::ProofTerm> arithmetic_evidence(const kernel::Context& context, const kernel::Proposition& goal,
                                                     bool rewriting) {
    Prover prover(context, rewriting);
    return prover.prove(goal);
}

} // namespace cppl::automation
