// Termination: the measures loops and recursive calls descend in, the
// recursion groups calls form, and totality settled across them.

#include "contracts_conditions.hpp"
#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/substitution.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/obligations/contracts.hpp"
#include "cppl/obligations/obligation.hpp"
#include "cppl/source/location.hpp"
#include "cppl/vir/expr.hpp"
#include "cppl/vir/module.hpp"
#include "cppl/vir/place.hpp"
#include "cppl/vir/types.hpp"
#include "definedness.hpp"
#include "lowering.hpp"
#include "walks.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace cppl::obligations::detail {

using contracts::Conditions;
using contracts::defined_and;
using contracts::lexicographically_below;
using contracts::measure_domain;
using contracts::specialize;

namespace contracts {

// The domain one measure component ranges over. An unsigned machine type,
// ordered by its natural non-wrapping `<`, is well-founded; a signed one has no
// least element a descent could stop at, and is refused rather than given an
// assumed bound (SPEC.md 22.5).
std::expected<kernel::IntType, Failure> measure_domain(const vir::Expr& measure, const std::string& what) {
    const std::optional<kernel::Type> type = core_type(measure.type);
    if (!type.has_value() || !type->is_integer()) {
        return std::unexpected(
            Failure{what + " must be an integer the formal core represents", measure.provenance.range.begin, {}});
    }
    if (type->integer_type().signedness != kernel::Signedness::Unsigned) {
        return std::unexpected(Failure{what + " must range over a well-founded domain, so its type must be unsigned",
                                       measure.provenance.range.begin,
                                       {}});
    }
    return type->integer_type();
}

// `next` strictly below `here` in the lexicographic order of their components
// (SPEC.md TERMINATION-005): the first falls, or it stays and the rest fall.
// Each comparison is the machine's own at its component's unsigned type, and a
// lexicographic product of well-founded orders is well-founded.
kernel::Proposition lexicographically_below(const std::vector<kernel::Term>& next,
                                            const std::vector<kernel::Term>& here,
                                            const std::vector<kernel::IntType>& types) {
    const auto compare = [&](kernel::PrimOp op, std::size_t index) {
        return kernel::predicate(kernel::Term::primitive(op, types[index], {next[index], here[index]}), true);
    };
    std::size_t index = next.size() - 1;
    kernel::Proposition below = compare(kernel::PrimOp::Less, index);
    while (index > 0) {
        --index;
        below = kernel::Proposition::disjunction(
            compare(kernel::PrimOp::Less, index),
            kernel::Proposition::conjunction(compare(kernel::PrimOp::Equal, index), std::move(below)));
    }
    return below;
}

// `goal`, preceded by each definedness condition a specification states of
// the terms it compares (SPEC.md ARITH-010). With none it is `goal` itself.
kernel::Proposition defined_and(std::vector<kernel::Proposition> defined, kernel::Proposition goal) {
    std::optional<kernel::Proposition> conditions;
    for (kernel::Proposition& condition : defined) {
        conditions = conditions ? kernel::Proposition::conjunction(std::move(*conditions), std::move(condition))
                                : std::move(condition);
    }
    return conditions ? kernel::Proposition::conjunction(std::move(*conditions), std::move(goal)) : goal;
}

// The strongly connected components of a call graph of `count` functions, each
// component's members in ascending order and the components ordered by their
// first member, so the result does not depend on how the search walked.
// Iterative, so a long call chain cannot exhaust the stack.
std::vector<std::vector<std::size_t>> recursion_groups(
    std::size_t count, const std::function<const std::vector<std::size_t>&(std::size_t)>& callees) {
    constexpr std::size_t kUnvisited = static_cast<std::size_t>(-1);
    std::vector<std::size_t> order(count, kUnvisited);
    std::vector<std::size_t> low(count, 0);
    std::vector<bool> on_stack(count, false);
    std::vector<std::size_t> stack;
    std::vector<std::vector<std::size_t>> groups;
    std::size_t next = 0;
    struct Frame {
        std::size_t node;
        std::size_t edge;
    };
    for (std::size_t root = 0; root < count; ++root) {
        if (order[root] != kUnvisited) {
            continue;
        }
        std::vector<Frame> frames{{root, 0}};
        order[root] = low[root] = next++;
        stack.push_back(root);
        on_stack[root] = true;
        while (!frames.empty()) {
            Frame& frame = frames.back();
            const std::vector<std::size_t>& edges = callees(frame.node);
            if (frame.edge < edges.size()) {
                const std::size_t callee = edges[frame.edge++];
                if (order[callee] == kUnvisited) {
                    order[callee] = low[callee] = next++;
                    stack.push_back(callee);
                    on_stack[callee] = true;
                    frames.push_back({callee, 0});
                } else if (on_stack[callee]) {
                    low[frame.node] = std::min(low[frame.node], order[callee]);
                }
                continue;
            }
            const std::size_t node = frame.node;
            frames.pop_back();
            if (!frames.empty()) {
                low[frames.back().node] = std::min(low[frames.back().node], low[node]);
            }
            if (low[node] == order[node]) {
                std::vector<std::size_t>& group = groups.emplace_back();
                std::size_t member = kUnvisited;
                while (member != node) {
                    member = stack.back();
                    stack.pop_back();
                    on_stack[member] = false;
                    group.push_back(member);
                }
                std::ranges::sort(group);
            }
        }
    }
    // Disjoint and each sorted, so their lexicographic order is that of their
    // first members.
    std::ranges::sort(groups);
    return groups;
}

// A function whose termination was asked for, or is required, and is not
// established (SPEC.md TERMINATION-006): a verification failure, never an
// omission.
void report_termination(diagnostics::Engine& engine, const vir::Function& function, const std::string& reason,
                        const source::SourceLocation& location, std::string note) {
    diagnostics::Diagnostic diagnostic;
    diagnostic.severity = diagnostics::Severity::Error;
    diagnostic.category = diagnostics::Category::ProofFailure;
    diagnostic.location = location.is_valid() ? location : function.range.begin;
    diagnostic.message =
        "the termination of verified function '" + function.qualified_name + "' is not established: " + reason;
    diagnostic.notes.push_back({std::move(note), diagnostic.location});
    engine.report(std::move(diagnostic));
}

} // namespace contracts

namespace {

std::string written_at(const source::SourceLocation& location) {
    return location.file + ":" + std::to_string(location.line);
}

} // namespace

namespace contracts {

// Which contracts are total-correctness claims (SPEC.md CORRECT-003 to
// CORRECT-006). One built as a theorem about its definition has no loop and
// calls only such contracts, so it is total. One built from conditions is total
// when every loop its paths enter states a measure, it passes through no
// unsafe block, and every contract it calls is total: the greatest fixed point
// of that rule, so a recursion group is total when all of its members are,
// their calls within it descending a measure. A function whose `decreases`
// asks that it terminate and is not total is refused, for the first reason it
// is not.
void settle_totality(Program& program, const Contracts& contracts, diagnostics::Engine& engine) {
    const std::size_t count = program.contracts.size();
    std::map<std::uint32_t, std::size_t> index_of;
    for (std::size_t index = 0; index < count; ++index) {
        index_of.emplace(program.contracts[index].function.value, index);
    }
    std::vector<std::vector<std::size_t>> callees(count);
    const auto calls = [&callees](std::size_t caller, std::size_t callee) {
        if (std::ranges::find(callees[caller], callee) == callees[caller].end()) {
            callees[caller].push_back(callee);
        }
    };
    std::vector<bool> total(count, false);
    for (std::size_t index = 0; index < count; ++index) {
        const ContractVerification& contract = program.contracts[index];
        // Another unit decided whether its contract is total, and its interface
        // says which; nothing here can make it more so (SPEC.md TUBOUND-007).
        if (contract.imported.has_value()) {
            total[index] = contract.total;
            continue;
        }
        if (!contract.partial) {
            total[index] = true;
            for (const ReturnPath& path : contract.paths) {
                for (const CallVerification& call : path.calls) {
                    if (const auto callee = index_of.find(call.callee.value); callee != index_of.end()) {
                        calls(index, callee->second);
                    }
                }
            }
            continue;
        }
        total[index] = contract.unmeasured_loops.empty() && contract.unsafe_regions.empty();
        for (const VerificationCondition& condition : contract.conditions) {
            for (const std::size_t callee : condition.callees) {
                if (callee < count) {
                    calls(index, callee);
                }
            }
        }
    }
    for (bool changed = true; changed;) {
        changed = false;
        for (std::size_t index = 0; index < count; ++index) {
            if (total[index] &&
                std::ranges::any_of(callees[index], [&total](std::size_t callee) { return !total[callee]; })) {
                total[index] = false;
                changed = true;
            }
        }
    }
    for (std::size_t index = 0; index < count; ++index) {
        program.contracts[index].total = total[index];
        // Another unit's contract is as it was recorded, and names nothing it
        // calls (SPEC.md TUBOUND-007).
        if (program.contracts[index].imported.has_value()) {
            continue;
        }
        for (const std::size_t callee : callees[index]) {
            if (callee != index && !total[callee]) {
                program.contracts[index].partial_callees.push_back(callee);
            }
        }
    }

    for (const auto& [usr, function] : contracts) {
        const auto found = index_of.find(function->id.value);
        if (found == index_of.end() || !function->contract.has_value() || function->contract->measures.empty() ||
            total[found->second] || program.contracts[found->second].imported.has_value()) {
            continue;
        }
        const ContractVerification& contract = program.contracts[found->second];
        std::string reason;
        source::SourceLocation location = function->contract->measure_range.begin;
        if (!contract.unmeasured_loops.empty()) {
            location = contract.unmeasured_loops.front();
            reason = "the loop at " + written_at(location) + " states no measure";
        } else if (!contract.unsafe_regions.empty()) {
            location = contract.unsafe_regions.front();
            reason = "it passes through the unsafe block at " + written_at(location) + ", which need not return";
        } else {
            const auto callee = std::ranges::find_if(callees[found->second],
                                                     [&total](std::size_t candidate) { return !total[candidate]; });
            reason = callee == callees[found->second].end()
                         ? "a function it calls does not terminate"
                         : "it calls '" + program.contracts[*callee].name + "', whose termination is not established";
        }
        report_termination(engine, *function, reason, location,
                           "a 'decreases' clause makes termination part of what is verified, so every loop the "
                           "function runs states a measure and every function it calls terminates (SPEC.md "
                           "TERMINATION-006, CORRECT-004)");
    }
}

} // namespace contracts

std::string Conditions::measure_subject(const vir::Expr& loop) const {
    return function_.qualified_name + " loop at line " + std::to_string(loop.provenance.range.begin.line) + " measure";
}

std::expected<void, Failure> Conditions::descends(const vir::Loop& loop, const vir::Expr& expression,
                                                  const Scope& scope, const Active& active,
                                                  const std::vector<kernel::Term>& values) {
    const std::size_t carried = loop.heads.size();
    OpaqueBindings holes = scope.opaque;
    for (std::size_t index = 0; index < carried; ++index) {
        holes[loop.heads[index]] = scope.binders.size() + index;
    }
    std::vector<kernel::Term> next;
    std::vector<kernel::Term> here;
    std::vector<kernel::IntType> types;
    // A measure is a specification: at both readings, the operations it
    // would evaluate are defined as well as it being smaller (SPEC.md
    // ARITH-010).
    std::vector<kernel::Proposition> defined;
    for (std::uint32_t position = 0; position < loop.measures; ++position) {
        const vir::Expr& written = loop.operands[carried + loop.invariants + position];
        auto domain = measure_domain(written, "a loop measure");
        if (!domain) {
            return std::unexpected(domain.error());
        }
        const TermLowerer after_iteration = [&](const vir::Expr& term) {
            return lower_value(term, definitions_, scope.binders.size() + carried, &scope.calls, &scope.versions,
                               &holes);
        };
        const TermLowerer at_head = [this, &scope](const vir::Expr& term) {
            return lower(term, scope);
        };
        auto after = after_iteration(written);
        if (!after) {
            return std::unexpected(after.error());
        }
        auto before = at_head(written);
        if (!before) {
            return std::unexpected(before.error());
        }
        auto defined_after = definedness_of(written, after_iteration);
        auto defined_before = definedness_of(written, at_head);
        if (!defined_after || !defined_before) {
            return std::unexpected(!defined_after ? defined_after.error() : defined_before.error());
        }
        if (defined_after->has_value()) {
            defined.push_back(std::move(**defined_after));
        }
        if (defined_before->has_value()) {
            defined.push_back(kernel::shift(**defined_before, static_cast<std::uint32_t>(carried)));
        }
        next.push_back(std::move(*after));
        // `next` stays abstracted over the head values, so the kernel
        // instantiates it at what this iteration produced; `here` is moved
        // past those binders to stand beside it.
        here.push_back(kernel::shift(*before, static_cast<std::uint32_t>(carried)));
        types.push_back(*domain);
    }
    std::string measure;
    for (std::uint32_t position = 0; position < loop.measures; ++position) {
        measure += (measure.empty() ? "" : ", ") + vir::describe(loop.operands[carried + loop.invariants + position]);
    }
    std::string carries;
    if (const auto* iteration = std::get_if<vir::Iterate>(&expression.node)) {
        for (std::size_t index = 0; index < carried && index < iteration->operands.size(); ++index) {
            carries += (carries.empty() ? "" : ", ") + vir::describe(loop.places[index]) + " = " +
                       vir::describe(iteration->operands[index]);
        }
    }
    emit(
        scope, Origin::LoopDescent, measure_subject(expression),
        loop.operands[carried + loop.invariants].provenance.range,
        specialize(defined_and(std::move(defined), lexicographically_below(next, here, types)), active.carried, values),
        {"the measure is (" + measure + ") at the head of the iteration, and is read again where this path " +
             (carried == 0 ? std::string("ends it, having changed nothing it reads") : "ends it, at " + carries),
         "the second reading must be strictly smaller, component by component in order; 'x#k' names one "
         "value local 'x' takes on the path"});
    return {};
}

std::expected<void, Failure> Conditions::descends_at_call(const vir::Call& call, const ContractVerification& callee,
                                                          const std::vector<kernel::Term>& arguments,
                                                          const Scope& scope, const vir::Expr& site) {
    const source::SourceLocation& location = site.provenance.range.begin;
    const auto declared = contracts_.find(call.callee.usr);
    if (declared == contracts_.end()) {
        return fail("'" + call.callee_name + "' has no established contract", location);
    }
    const std::optional<vir::Contract>& own = function_.contract;
    const std::optional<vir::Contract>& stated = declared->second->contract;
    if (!own.has_value() || !stated.has_value()) {
        return fail("'" + call.callee_name + "' has no established contract", location);
    }
    const std::vector<vir::Expr>& mine = own->measures;
    const std::vector<vir::Expr>& theirs = stated->measures;
    if (mine.empty() || mine.size() != theirs.size()) {
        return fail("'" + function_.qualified_name + "' and '" + call.callee_name +
                        "' call each other, so each states a measure with as many components as the other",
                    location);
    }
    std::vector<kernel::Term> next;
    std::vector<kernel::Term> here;
    std::vector<kernel::IntType> types;
    std::vector<kernel::Proposition> defined;
    for (std::size_t position = 0; position < mine.size(); ++position) {
        auto caller = measure_domain(mine[position], "a function measure");
        if (!caller) {
            return std::unexpected(caller.error());
        }
        auto called = measure_domain(theirs[position], "a function measure");
        if (!called) {
            return std::unexpected(called.error());
        }
        if (!(*caller == *called)) {
            return fail("component " + std::to_string(position + 1) + " of the measure of '" +
                            function_.qualified_name + "' has type '" + vir::describe(mine[position].type) +
                            "', and of '" + call.callee_name + "' type '" + vir::describe(theirs[position].type) +
                            "': the two cannot be compared",
                        location);
        }
        const TermLowerer at_call = [this, &callee](const vir::Expr& term) {
            return lower_value(term, definitions_, callee.parameters.size());
        };
        const TermLowerer at_entry = [this, &scope](const vir::Expr& term) {
            return lower(term, scope);
        };
        auto after = at_call(theirs[position]);
        if (!after) {
            return std::unexpected(after.error());
        }
        auto before = at_entry(mine[position]);
        if (!before) {
            return std::unexpected(before.error());
        }
        // Each measure is a specification, defined where it is read
        // (SPEC.md ARITH-010).
        auto defined_after = definedness_of(theirs[position], at_call);
        auto defined_before = definedness_of(mine[position], at_entry);
        if (!defined_after || !defined_before) {
            return std::unexpected(!defined_after ? defined_after.error() : defined_before.error());
        }
        if (defined_after->has_value()) {
            defined.push_back(std::move(**defined_after));
        }
        if (defined_before->has_value()) {
            defined.push_back(kernel::shift(**defined_before, static_cast<std::uint32_t>(callee.parameters.size())));
        }
        next.push_back(std::move(*after));
        here.push_back(kernel::shift(*before, static_cast<std::uint32_t>(callee.parameters.size())));
        types.push_back(*caller);
    }
    const auto described = [](const std::vector<vir::Expr>& expressions) {
        std::string text;
        for (const vir::Expr& expression : expressions) {
            text += (text.empty() ? "" : ", ") + vir::describe(expression);
        }
        return "(" + text + ")";
    };
    emit(scope, Origin::CallDescent, function_.qualified_name + " -> " + call.callee_name, site.provenance.range,
         specialize(defined_and(std::move(defined), lexicographically_below(next, here, types)), callee.parameters,
                    arguments),
         {"'" + function_.qualified_name + "' was entered at measure " + described(mine) + "; '" + call.callee_name +
              "' is called with arguments " + described(call.arguments) + ", at its measure " + described(theirs),
          "the call's measure must be strictly smaller, component by component in order"});
    return {};
}

std::expected<void, Failure> Conditions::returned(const vir::Expr& expression, Scope scope) {
    const std::optional<kernel::Type> type = core_type(expression.type);
    if (!type.has_value() || !(*type == plan_.result)) {
        return fail("a returned value's type differs from the declared result type", expression.provenance.range.begin);
    }
    if (auto evaluated = evaluate(expression, scope); !evaluated) {
        return evaluated;
    }
    auto value = lower(expression, scope);
    if (!value) {
        return std::unexpected(value.error());
    }
    const auto fresh = static_cast<std::uint32_t>(scope.binders.size() - plan_.parameters.size());
    emit(scope, Origin::ReturnPath, function_.qualified_name + " path " + std::to_string(++paths_),
         expression.provenance.range, kernel::instantiate(kernel::shift(plan_.postcondition, fresh, 1), *value));
    return {};
}

} // namespace cppl::obligations::detail
