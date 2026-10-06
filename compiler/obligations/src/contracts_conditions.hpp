#pragma once

// The walk of a verified body that collects what each path owes and may
// assume, and what the files that state contracts share with it.

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/obligations/contracts.hpp"
#include "cppl/obligations/obligation.hpp"
#include "cppl/source/location.hpp"
#include "cppl/vir/expr.hpp"
#include "cppl/vir/module.hpp"
#include "cppl/vir/types.hpp"
#include "lowering.hpp"
#include "walks.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace cppl::obligations::detail::contracts {

// Defined in contracts_build.cpp.
void report(diagnostics::Engine& engine, const vir::Function& function, const Failure& failure,
            const std::function<std::string(const Failure&)>& explain);

kernel::Proposition quantify(const std::vector<kernel::Type>& parameters, kernel::Proposition goal);

kernel::Proposition specialize(kernel::Proposition proposition, const std::vector<kernel::Type>& parameters,
                               const std::vector<kernel::Term>& arguments);

// What a value must satisfy to stand as a value of `type` (SPEC.md 17.2).
//
// A refinement of a refinement states both predicates, because both apply to the
// value (SPEC.md 17.5), and an indexed refinement states its predicate at the
// values its indices were applied at (SPEC.md 18). An unrefined type requires
// nothing, which is what makes ordinary C++ unaffected.
//
// Membership of a structural value is membership of each of its components, at
// the component's own declared type and over the projection that names it
// (SPEC.md 17.6). A record is valid exactly when its subobjects are, so a
// refined member owes its predicate wherever the whole object crosses into its
// type and supplies it wherever the whole object is known to be valid. This is
// one recursive rule rather than a member-specific one: the same call states a
// scalar's refinement, a member's, and a member of a member's.
// The refinement an obligation for membership in `type` is named after: the
// type's own, or, for a record whose refinement is on a member, the first
// component's that has one; the type itself when neither does.
inline std::string refinement_label(const vir::Type& type, unsigned depth = 0) {
    if (!type.refinements.empty()) {
        return type.refinements.front().name;
    }
    if (type.is_value() && depth < 32) {
        for (const vir::Type& component : std::get<vir::ValueType>(type.node).projections) {
            if (std::string label = refinement_label(component, depth + 1); !label.empty()) {
                return label;
            }
        }
    }
    return depth == 0 ? vir::describe(type) : std::string{};
}

// Defined in contracts_build.cpp.
std::expected<std::optional<kernel::Proposition>, Failure> membership(const Program& program, const vir::Type& type,
                                                                      const kernel::Term& value);

kernel::Proposition owed_at_return(const ContractVerification& function, std::vector<kernel::Term> arguments,
                                   kernel::Term value);

kernel::Proposition close(const ContractVerification& function, const ReturnPath& path, std::size_t prefix,
                          std::size_t conditions, bool abstract, kernel::Proposition goal);

// One expression a path evaluates before it returns: a guard, whose outcome
// then holds on this path, or the value a local's version was given. Both are
// taken where the body evaluates them, so a call inside either is proven there
// and not where its value is eventually read.
struct Step {
    const vir::Expr* value;
    const vir::PlaceVersion* binding; // null for a guard
    bool positive;                    // the guard's outcome on this path
};

struct Route {
    std::vector<Step> steps;
    const vir::Expr* returned;
};

bool routes(const vir::Expr& expression, std::vector<Step> steps, std::vector<Route>& result);

std::expected<kernel::IntType, Failure> measure_domain(const vir::Expr& measure, const std::string& what);

// Defined in contracts_build.cpp.
std::expected<void, Failure> state_contract(const vir::Function& function, const DefinitionMap& pure_definitions,
                                            const Program& program, ContractVerification& plan);

std::expected<ContractVerification, Failure> build(const vir::Function& function, const Contracts& contracts,
                                                   const DefinitionMap& pure_definitions, DefinitionMap& definitions,
                                                   const std::map<std::string, std::size_t>& established,
                                                   Program& program);

// Bounds the nodes one body's conditions are generated from. The bridge
// already bounds paths and statements; this keeps malformed VIR finite too.
inline constexpr std::size_t kMaxConditionSteps = std::size_t{1} << 15;

// Defined in contracts_termination.cpp.
kernel::Proposition lexicographically_below(const std::vector<kernel::Term>& next,
                                            const std::vector<kernel::Term>& here,
                                            const std::vector<kernel::IntType>& types);

kernel::Proposition defined_and(std::vector<kernel::Proposition> defined, kernel::Proposition goal);

// Partial correctness (SPEC.md 23, 24).
//
// A body with a loop is not one total core term, so its contract cannot be a
// theorem about a definition. It is established from verification conditions,
// each an ordinary proposition the kernel decides: on every path, what the path
// supposes implies what must hold where the path stands. A value the path
// learns only through a proposition - a verified call's result, or a carried
// local at a loop head - is a fresh variable bound where the path meets it,
// followed by the proposition supposed of it. Which conditions a body needs is
// decided here, by the rules for calls and loops; that is a correspondence
// responsibility (TRUST.md 12.1, 13). Whether each holds is the kernel's.
class Conditions {
  public:
    // `recursion` is the recursion group the function belongs to, as contract
    // indices, when it recurses: a call to one of them supposes its contract as
    // the induction hypothesis and owes a strictly smaller measure.
    Conditions(const vir::Function& function, const ContractVerification& plan, const Contracts& contracts,
               const DefinitionMap& definitions, const std::map<std::string, std::size_t>& established,
               const Program& program, const std::vector<std::size_t>& recursion = {})
        : function_(function),
          plan_(plan),
          contracts_(contracts),
          definitions_(definitions),
          established_(established),
          program_(program),
          recursion_(recursion) {}

    std::expected<void, Failure> run();

    std::vector<Obligation> obligations;
    std::vector<VerificationCondition> conditions;
    std::vector<PathClaim> claims;
    // Every unsafe block a path of the body passes through, in the order the
    // walk meets them, each once.
    std::vector<source::SourceLocation> unsafe_regions;
    // Every loop a path of the body enters that states no measure, each once.
    // Its termination is not established, so neither is the body's.
    std::vector<source::SourceLocation> unmeasured_loops;
    // Every validation expression a path of the body evaluates, each once, in
    // the order the walk meets them (SPEC.md RUNTIMECHECK-011).
    std::vector<ValidationSite> validations;

  private:
    // After the parameters, a path binds fresh values and supposes facts, in
    // the order it meets them. A step is one or the other, never both, so the
    // alternative carries that exclusivity instead of a pair of optionals. The
    // outcome of a condition C++ evaluates at run time is a fact like any
    // other: a crossing proven from it is proven statically (SPEC.md
    // RUNTIMECHECK-010).
    using Event = std::variant<kernel::Type, kernel::Proposition>;

    struct Scope {
        std::vector<kernel::Type> binders; // the parameters, then each fresh value
        std::vector<Event> events;
        CallBindings calls;
        VersionBindings versions;
        OpaqueBindings opaque;
        std::vector<std::size_t> relied_on; // contracts whose postconditions are supposed
        // The unsafe block this path passed through, if any: from there on it
        // holds none of its contract's capabilities (SPEC.md UNSAFE-003).
        std::optional<source::SourceLocation> unsafe;
    };

    struct Active {
        const vir::Loop* loop;
        std::vector<kernel::Type> carried;
    };

    static std::unexpected<Failure> fail(std::string reason, const source::SourceLocation& location);

    // `goal` under everything the path supposes.
    [[nodiscard]] kernel::Proposition close(const Scope& scope, kernel::Proposition goal) const;

    [[nodiscard]] std::expected<kernel::Term, Failure> lower(const vir::Expr& expression, const Scope& scope) const;

    [[nodiscard]] std::string identity_of(std::size_t index) const;

    void emit(const Scope& scope, Origin origin, std::string subject, const source::SourceRange& range,
              kernel::Proposition goal, std::vector<std::string> explanation = {});

    // The memory capabilities a callee's contract states are owed at every call
    // to it, as its preconditions are (SPEC.md 12.10 VERIFIED-043, RFC 0014 §3).
    // A capability is not a proposition the kernel sees, so what is owed is that
    // the caller holds it: the argument is one of the caller's own pointer
    // parameters, and the caller's contract states a capability of the same kind
    // on it. Nothing about the argument's value supplies one, and neither kind
    // entails the other.
    //
    // A sized capability bounds a value, so where either side states an element
    // count, the callee's must not exceed the caller's. That comparison is an
    // ordinary obligation the kernel decides; an unstated count is one object.
    std::expected<void, Failure> owe_capabilities(const vir::Call& call, const ContractVerification& callee,
                                                  const std::vector<kernel::Term>& arguments, const Scope& scope,
                                                  const vir::Expr& site);

    // Each verified call the expression evaluates, where it evaluates it: its
    // precondition is a condition under what the path supposes so far, and its
    // result is a fresh value of which the callee's postcondition is supposed.
    // Then each operation it evaluates owes its definedness (SPEC.md ARITH-009).
    std::expected<void, Failure> evaluate(const vir::Expr& expression, Scope& scope);

    // Binds each struct value the expression assembles, before any call it makes,
    // as a call's result is bound (aggregates.hpp, TRUST.md TCB-AGGREGATE-001).
    std::expected<void, Failure> bind_aggregates(const vir::Expr& expression, Scope& scope) const;

    // Where a call's postcondition stands among a scope's events.
    struct Supposed {
        const vir::Expr* call;
        std::size_t event;
    };

    // Every operation the expression evaluates whose behavior C++ defines only
    // under a condition owes that condition on this path (SPEC.md ARITH-009,
    // DEFINEDBEHAVIOR-001 to DEFINEDBEHAVIOR-003). It is owed under what the
    // path supposes before the operation: the path so far, the `?:`, `&&` and
    // `||` outcomes that select it, and the postconditions of the calls C++
    // sequences before it, which are those in its operands and guards. A call
    // elsewhere in the expression may run after it, so its postcondition is
    // not supposed: a callee that need not return could otherwise make the
    // obligation vacuous while the operation runs first. Once the expression
    // is evaluated, what it owed holds on the rest of the path.
    std::expected<void, Failure> owe_definedness(const vir::Expr& expression, Scope& scope,
                                                 const std::vector<Supposed>& posts);

    std::expected<void, Failure> evaluate_calls(const vir::Expr& expression, Scope& scope,
                                                std::vector<Supposed>& posts);

    // What a validation expression's test is: the base type it takes, and the
    // postcondition its result supposes, `result -> R(value)`, stated over the
    // parameter and then the result like any contract's (SPEC.md
    // RUNTIMECHECK-011). Its refinement's predicate must be one the program can
    // evaluate on every value (RUNTIMECHECK-020).
    struct ValidationTest {
        kernel::Type base;
        kernel::Proposition postcondition;
        std::string name;
        std::string predicate;
    };
    [[nodiscard]] std::expected<ValidationTest, Failure> validation_test(const vir::Call& call,
                                                                         const vir::Expr& site) const;

    // What a call leaves on its path once its entry obligations are owed: a
    // fresh post-state value for each argument it writes, a fresh result, and
    // the postcondition supposed of them (SPEC.md VERIFIED-014). The values are
    // fresh, and their facts come only from the stated postcondition, never
    // from the erased parameter type. `postcondition` is stated over the
    // parameters and then the result.
    std::expected<void, Failure> suppose_call(const vir::Call& call, const vir::Expr& site,
                                              const std::vector<kernel::Type>& parameters, const kernel::Type& result,
                                              std::vector<kernel::Term> arguments,
                                              const std::optional<kernel::Proposition>& postcondition, Scope& scope);

    std::expected<void, Failure> walk(const vir::Expr& expression, Scope scope, std::vector<Active>& loops);

    // The first unsafe block a subtree passes through, if any.
    static std::optional<source::SourceLocation> first_unsafe_region(const vir::Expr& expression);

    // A case split on this path (SPEC.md CASE-017). It has no runtime effect,
    // so nothing is evaluated here: the subject and the discriminators are
    // terms over the versions current where the split was written, and a later
    // write gives the place a new version they say nothing about.
    //
    // Each arm continues the path supposing exactly what the proof-side split
    // gives the same case (`prove_cases`): every earlier discriminator false and
    // its own true, the residual every one false, and, without a residual, the
    // last named case every other one false. Those suppositions cover every
    // state by excluded middle on each discriminator in turn, whatever the
    // provider said, so no path of the body is dropped. That holds only if every
    // state has exactly one arm, which is checked here as well as where the
    // split was built.
    std::expected<void, Failure> split_path(const vir::CaseSplit& split, const vir::Expr& expression, Scope scope,
                                            std::vector<Active>& loops);

    // `contradiction evidence;` on this path (SPEC.md VERIFIED-023). The path
    // ends here, so nothing after it owes anything; what it owes instead is the
    // claim itself, that the facts established on the way here cannot all hold.
    // That is a condition like any other: every fresh value and supposed fact of
    // the path, closed over `False`. Its identity carries its origin, so it is
    // never mistaken for an omitted case stating the same proposition
    // (CASE-012, CASE-016).
    //
    // The evidence's arguments are specification terms and are never evaluated,
    // so a call in one is not a call this body makes: it is lowered as a term
    // like any other, and a call the core cannot state is refused rather than
    // proven where it stands.
    std::expected<void, Failure> impossible(const vir::PathContradiction& claim, const vir::Expr& expression,
                                            const Scope& path);

    [[nodiscard]] std::string invariant_subject(const vir::Expr& loop, std::uint32_t position) const;

    // Entering a loop: each invariant must hold of the carried locals' values
    // here. From the head on, each carried local is a fresh value of which only
    // the invariants are supposed.
    std::expected<void, Failure> enter(const vir::Loop& loop, const vir::Expr& expression, Scope scope,
                                       std::vector<Active>& loops);

    // The end of an iteration: each invariant must hold again of the values
    // the carried locals take into the next one. The invariant is stated with
    // the head values abstracted, then instantiated at those values by the
    // kernel's own substitution, so the head value an iteration started from
    // and the value it ends with are never confused.
    std::expected<void, Failure> iterate(const vir::Iterate& next, const vir::Expr& expression, const Scope& scope,
                                         const std::vector<Active>& loops);

    [[nodiscard]] std::string measure_subject(const vir::Expr& loop) const;

    // The descent an iteration owes its measure: the tuple it carries into the
    // next iteration is strictly below the tuple the head started from, in the
    // lexicographic order of its components (SPEC.md 24.3, LOOP-006,
    // TERMINATION-005).
    //
    // Each component is read twice from one expression. At the head it is
    // stated over the carried values as they are bound there; at the end of the
    // iteration it is stated over the values the iteration produces, by the
    // same abstraction and instantiation the invariants use, so the two points
    // are never confused. Each comparison is the machine's own order at the
    // component's unsigned type, which is well-founded, so a strict descent
    // cannot continue forever.
    std::expected<void, Failure> descends(const vir::Loop& loop, const vir::Expr& expression, const Scope& scope,
                                          const Active& active, const std::vector<kernel::Term>& values);

    // The descent a call within the recursion group owes: the callee's measure
    // at the call's arguments is strictly below the caller's at the values it
    // was entered with, lexicographically (SPEC.md TERMINATION-005,
    // TERMINATION-007). Both are read from the contracts, so a measure is a
    // function of the parameters alone, which a body cannot write.
    //
    // The callee's measure is stated over its own parameters and instantiated
    // at the arguments by the kernel's substitution; the caller's is moved past
    // those binders to stand beside it, as a loop's head measure is.
    std::expected<void, Failure> descends_at_call(const vir::Call& call, const ContractVerification& callee,
                                                  const std::vector<kernel::Term>& arguments, const Scope& scope,
                                                  const vir::Expr& site);

    std::expected<void, Failure> returned(const vir::Expr& expression, Scope scope);

    const vir::Function& function_;
    const ContractVerification& plan_;
    const Contracts& contracts_;
    const DefinitionMap& definitions_;
    const std::map<std::string, std::size_t>& established_;
    const Program& program_;
    const std::vector<std::size_t>& recursion_;
    std::size_t steps_ = 0;
    std::size_t paths_ = 0;
    std::size_t impossibilities_ = 0;
};

// Defined in contracts_build.cpp.
std::expected<ContractVerification, Failure> build_partial(const vir::Function& function, const Contracts& contracts,
                                                           const DefinitionMap& pure_definitions,
                                                           const std::map<std::string, std::size_t>& established,
                                                           Program& program);

std::expected<void, std::pair<const vir::Function*, Failure>> build_group(
    const std::vector<const vir::Function*>& members, const Contracts& contracts, const DefinitionMap& pure_definitions,
    std::map<std::string, std::size_t>& established, Program& program);

// Defined in contracts_termination.cpp.
std::vector<std::vector<std::size_t>> recursion_groups(
    std::size_t count, const std::function<const std::vector<std::size_t>&(std::size_t)>& callees);

void report_termination(diagnostics::Engine& engine, const vir::Function& function, const std::string& reason,
                        const source::SourceLocation& location, std::string note);

void settle_totality(Program& program, const Contracts& contracts, diagnostics::Engine& engine);

} // namespace cppl::obligations::detail::contracts
