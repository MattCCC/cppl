#pragma once

#include "aggregate_values.hpp"
#include "cppl/clang/ast.hpp"
#include "cppl/clang/bridge.hpp"
#include "cppl/source/location.hpp"
#include "cppl/source/representation.hpp"
#include "places.hpp"
#include "sequences.hpp"
#include "signature.hpp"
#include "statements.hpp"

#include <clang-c/Index.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// The lowering of a verified body (SPEC.md 12.8): what remains to be executed
// after the statement being lowered, and the state the lowering threads through
// the body. Each member function is documented where it is defined.
namespace cppl::clangbridge::detail {

// What remains to be executed after the statement being lowered: the rest of
// its block, and whatever follows the blocks enclosing it. A branch lowers this
// continuation once per arm, under the versions that arm established, which is
// what makes a local's value path-sensitive without any merge rule.
struct LoopFrame;

struct LoopHeader;

struct SwitchHeader;

struct SwitchFrame;

struct IfHeader;

// A range-based `for` being lowered: what the loop machinery advances in place
// of a written condition and increment (SPEC.md LOOP-004, STMT-005).
//
// C++ iterates the range from its beginning to its end, both taken once before
// the first iteration ([stmt.ranged]). Over a range this implementation models
// -- a vector, a string or a span this body names, or an array local -- that is
// one element per position from 0 to the length, in order, so the iteration is
// lowered as a position this body names nowhere: 0 before the loop, below the
// length at every head that runs an iteration, one more at each iteration's end.
struct RangeIteration {
    CXCursor statement = clang_getNullCursor();
    CXCursor variable = clang_getNullCursor(); // the loop variable's declaration
    std::string range;                         // how the range is written, for diagnostics
    std::size_t position = 0;                  // the hidden position's entry in the locals
    Type position_type;
    // A vector, a string or a span, whose elements a region names; otherwise an
    // array local, tracked as one place per element.
    bool sequence = false;
    ElementRegion region;
    CXCursor array = clang_getNullCursor();
    std::int64_t extent = 0;
    Type element;       // with the refinements the element type names
    Type variable_type; // the loop variable's, or its referent's, with its refinements
    bool reference = false;
    bool writable = false; // a reference to non-const
    // The sequence roots whose storage generation the iteration stands on: the
    // container the elements belong to, and a span ranged over.
    std::vector<std::size_t> watched;
};

struct Continuation {
    const Continuation* outer = nullptr;
    const std::vector<CXCursor>* statements = nullptr;
    std::size_t index = 0;

    // In place of statements: the end of one iteration of a loop, before or
    // after its increment, or a `for` loop whose initialization is done.
    const LoopFrame* iteration = nullptr;
    bool after_increment = false;
    const LoopHeader* header = nullptr;

    // In place of statements: a `switch` whose condition variable is declared
    // and which now dispatches, or the end of a switch's body, which goes on
    // with what follows the switch, outside it.
    const SwitchHeader* dispatch = nullptr;
    const SwitchFrame* left = nullptr;

    // In place of statements: an `if` whose init-statement and condition
    // variable have run, which now decides between its branches.
    const IfHeader* branch = nullptr;

    // With `statements`, the body of a switch: the positions a `case` or
    // `default` label leads into, which a jump reaches whatever stands before
    // them.
    const std::vector<std::size_t>* labels = nullptr;
};

// A loop about to be entered.
struct LoopHeader {
    CXCursor statement = clang_getNullCursor();
    CXCursor condition = clang_getNullCursor(); // null for a `for` without one
    CXCursor body = clang_getNullCursor();
    std::optional<CXCursor> increment;
    const Continuation* exit = nullptr; // what follows the loop
    // A `do` loop: the body runs first, and the condition decides at the end
    // of each iteration whether another begins (SPEC.md LOOP-003).
    bool condition_last = false;
    // A range-based `for`, iterated in place of a condition and an increment.
    const RangeIteration* range = nullptr;
};

// A loop whose body is being lowered.
struct LoopFrame {
    std::uint32_t id = 0;
    CXCursor statement = clang_getNullCursor();
    Locals head;                      // the locals at the head, each carried one at its head version
    std::vector<std::size_t> carried; // positions in `head` that the loop writes
    std::optional<CXCursor> increment;
    const Continuation* exit = nullptr;
    std::size_t frames_outside = 0; // the enclosing loops, for a `break` into what follows
    CXCursor condition = clang_getNullCursor();
    bool condition_last = false;
    const RangeIteration* range = nullptr;
};

// A `switch` about to dispatch on its condition (C++ [stmt.switch]).
struct SwitchHeader {
    CXCursor statement = clang_getNullCursor();
    CXCursor condition = clang_getNullCursor();
    CXCursor body = clang_getNullCursor();
    const Continuation* exit = nullptr; // what follows the switch
};

// An `if` about to decide between its branches (C++ [stmt.if]).
struct IfHeader {
    CXCursor statement = clang_getNullCursor();
    std::vector<CXCursor> parts; // the condition, the branch it selects, and the other branch if written
    const Continuation* exit = nullptr;
    bool constant = false; // `if constexpr`
};

// A switch whose body is being lowered: where a `break` belonging to it goes.
// A `break` belongs to the innermost loop or switch enclosing it, and a switch
// is innermost when no loop was entered after it.
struct SwitchFrame {
    const Continuation* exit = nullptr;
    std::size_t loops_outside = 0;    // the loops enclosing the switch
    std::size_t switches_outside = 0; // the switches enclosing it
};

// A memory capability the contract of the body being lowered states, resolved
// to the parameter whose pointee it describes (SPEC.md 12.10).
//
// This is what makes a dereference legal inside the body. It is not evidence
// the body produces: the caller owes it at the call, and here it is a
// hypothesis with a stated origin.
struct StatedCapability {
    std::uint32_t parameter = 0;
    Capability::Kind kind = Capability::Kind::Readable;

    // The element count of the sized form, `readable(p, n)`, as the term the
    // contract stated. Empty for the one-object abbreviation `readable(p)`.
    //
    // The term is kept rather than a flag: a subscript through this capability
    // owes `index < n`, and `n` is a value no literal is available for
    // (SPEC.md 12.10, VERIFIED-038).
    std::vector<Expr> extent;
};

// Lowers a resolved function body into the value it returns.
//
// Statements are taken in program order, threading the logical version of each
// local. A declaration or an assignment binds the next version and the rest of
// the body is lowered under it; a read of a local denotes the version current
// where the read stands. Nothing here rewrites the program: the versions are a
// model of the body Clang resolved (SPEC.md 12.8).
struct BodyLowering {
    // The body's signature, and the parameters Clang resolved for it, which are
    // its written parameters. A member function's implicit object is not among
    // them: its leaves are tracked as storage rooted in its class (SPEC.md
    // CLASS-008).
    const Signature& signature;
    const std::vector<CXCursor>& parameters;
    Type result_type;
    // The projector's generated prefix, which every declaration it puts in a
    // body carries: loop clauses, contradiction blocks, instantiation markers.
    std::string invariant_prefix;
    const std::vector<Selection::Refinement>* refinements = nullptr;
    // Which callees may write through a `const` access path they are handed,
    // through an unsafe block (TRUST.md TCB-UNSAFE-004).
    UnsafeEffects* unsafe_effects = nullptr;
    std::uint32_t next_version = 0;
    std::uint32_t next_loop = 0;
    std::vector<const LoopFrame*> frames;
    std::vector<const SwitchFrame*> switch_frames;
    std::vector<std::string> consumed_invariants;
    std::vector<std::string> consumed_contradictions;
    std::vector<Function::SplitSubject> consumed_splits;
    std::vector<std::string> consumed_unsafe;

    // Where the path being lowered passed through an unsafe block, if it has.
    // From there on the path holds none of the capabilities its contract stated:
    // the block may have ended a lifetime, released storage or moved a pointer's
    // target, and nothing checked that it did not (SPEC.md UNSAFE-003,
    // ARCHITECTURE.md ARCH-UNSAFE-002). A loop that holds an unsafe block is
    // such a point for its every iteration and for what follows it.
    std::optional<source::SourceLocation> revoked_by;
    std::string rejection;
    bool executable_state = true;
    source::SourceLocation completion_location = {};

    // Locals whose address is taken somewhere in this body, by Clang's
    // resolution of `&x`. A local not in this set cannot be the pointee of any
    // pointer, so a write through a pointer cannot reach it. Escape is
    // permanent and computed for the whole body, never per program point: a
    // pointer formed on one path may be written through on another.
    std::unordered_set<unsigned> escaped;

    // Locals some unmodeled write could reach, which is a stricter question
    // than `escaped` answers. See `unconfined_locals`.
    std::unordered_set<unsigned> unconfined;

    // The versions whose refinement validity is established (SPEC.md
    // REFINE-060, CLASS-010): the version a place entered the body with, which
    // the caller established, one a write established while being charged the
    // place's refinement, one a call left after the caller was charged it, and
    // one an aliasing write left in a place whose previous version was valid,
    // the write having been charged that place's refinement too. Any other
    // version -- left by an unsafe block, a loop head, a write this lowering
    // could not charge -- is absent, so its validity is never derived: a
    // normal return is charged the refinement of such a version, and of no
    // other (REFINE-061, REFINE-062). Absence is the default, so a version
    // established by a route not listed here fails closed.
    std::unordered_set<std::uint32_t> valid_versions;

    // Dereference places formed while lowering the statement in hand, awaiting
    // the binding that gives each one an entry value.
    //
    // A pointee is caller storage: this body did not write it, so its value is
    // opaque and inherits no fact, exactly as a havocked place does. Binding it
    // is what makes a read of it well formed, and the binding must wrap the
    // continuation, which only the statement lowering can do.
    // The entries themselves rather than indices into a `Locals`: each
    // statement form lowers over its own copy of the locals, so an index would
    // not survive back to where the binding is emitted.
    std::vector<Local> formed_derefs;
    std::vector<ChosenArm> chosen_arms; // the arm each route evaluates of a selection a statement computes

    // The value each leaf of a struct a call may have written takes afterwards,
    // by the leaf's post-call version: its member of the struct's post-state
    // value, which the callee's contract describes (TRUST.md TCB-AGGREGATE-001).
    // Such a leaf is reported changed the way any other place a call may have
    // written is, and `unknown` binds it to this value instead of leaving it
    // unknown. Versions are unique within a body, so a version names exactly
    // one such leaf.
    std::unordered_map<std::uint32_t, Expr> rebound_leaves;

    // The memory capabilities this body may rely on, by the parameter index of
    // the pointer each one names. These come from the contract's `expects`
    // clauses and from nothing else: a capability is established by a proven
    // obligation or a recorded trusted boundary, never because an access needed
    // it (AGENTS.md storage invariants, SPEC.md VERIFIED-043).
    const std::vector<StatedCapability>* capabilities = nullptr;

    // The standard-library models this body's lowering used (RFC 0020 §10).
    std::set<source::RepresentationKind> library_models;

    // The storage an argument hands a callee without passing the container:
    // the root of the tracked container a span or a data pointer is over, or
    // the span parameter it passes on (RFC 0020 §7).
    struct HandedStorage {
        std::optional<std::size_t> root;
        std::optional<CXCursor> span_parameter;
        source::RepresentationKind family = source::RepresentationKind::None;
    };

    // This lowering as the lowering of whole struct values asks it
    // (aggregate_values.hpp): each operation is this lowering's own.
    struct StructHooks final : aggregates::Lowering {
        explicit StructHooks(BodyLowering& body) : lowering(body) {}
        std::uint32_t fresh_version() override {
            return lowering.next_version++;
        }
        void establish(std::uint32_t version) override {
            lowering.valid_versions.insert(version);
        }
        void rebind(std::uint32_t version, Expr value) override {
            lowering.rebound_leaves.emplace(version, std::move(value));
        }
        std::nullopt_t reject(std::string reason) override {
            return lowering.reject(std::move(reason));
        }
        std::optional<Expr> evaluate(CXCursor cursor, Locals& state, std::vector<std::size_t>& invalidated) override {
            return lowering.evaluate(cursor, state, invalidated);
        }
        std::optional<std::size_t> written_local(CXCursor target, Locals& state) override {
            return lowering.written_local(target, state);
        }
        Expr bind(std::uint32_t version, Place place, Expr value, Expr body, CXCursor at, Type declared) override {
            return lowering.bind(version, std::move(place), std::move(value), std::move(body), at, std::move(declared));
        }
        Expr unknown(const Locals& state, std::size_t entry, Expr body, CXCursor at) override {
            return lowering.unknown(state, entry, std::move(body), at);
        }
        std::optional<Expr> write_then(std::size_t local, Expr value, CXCursor statement, const Locals& state,
                                       const aggregates::Rest& rest) override {
            return lowering.write_then(local, std::move(value), statement, state, rest);
        }
        std::optional<std::string> type_leaves(const Type& type, const std::string& name,
                                               std::vector<aggregates::TypeLeaf>& leaves) override {
            std::vector<AggregateLeaf> found;
            std::optional<std::string> refusal = lowering.collect_type_leaves(type, name, {}, found);
            for (AggregateLeaf& leaf : found) {
                leaves.push_back(
                    aggregates::TypeLeaf{std::move(leaf.path), std::move(leaf.type), std::move(leaf.spelling)});
            }
            return refusal;
        }

      private:
        BodyLowering& lowering;
    };

    // A loop clause the projector declared at the head of the body: an
    // `invariant_` condition or a `measure_` expression (SPEC.md 24.1, 24.3).
    struct LoopMarker {
        CXCursor cursor = clang_getNullCursor();
        bool measure = false;
    };

    // A branch of the body: what the program does when the condition holds, and
    // what it does when it does not. Each is built on demand because condition
    // elaboration places it on more than one route, and every route needs its
    // own subtree rather than a shared one.
    using Branch = std::function<std::optional<Expr>(const Locals&)>;

    // One storage leaf of an aggregate's initialization: the path reaching it
    // from the object, the type it was declared with, and the initializer
    // element supplying its first value.
    struct AggregateLeaf {
        std::vector<PlaceStep> path;
        Type type;
        CXCursor initializer;
        std::string spelling;
    };

    // The lowering's own state: the capabilities it may rely on, the one write, a
    // version nothing describes, and the post-state a normal return hands back.
    [[nodiscard]] bool confined_element(const Local& entry) const;
    Expr bind_formed_derefs(Expr body, CXCursor at);
    [[nodiscard]] const StatedCapability* granted_capability(std::uint32_t parameter, Capability::Kind kind) const;
    [[nodiscard]] bool granted(std::uint32_t parameter, Capability::Kind kind) const;
    [[nodiscard]] std::string capability_refusal(const std::string& spelling, Capability::Kind required) const;
    bool has_post_state() const;
    Expr completed(Expr value, const Locals& locals, CXCursor at);
    Expr void_value(CXCursor at) const;
    Expr unknown(const Locals& state, std::size_t entry, Expr body, CXCursor at, bool confined = false);
    std::nullopt_t reject(std::string reason);
    Expr bind(std::uint32_t version, Place place, Expr value, Expr body, CXCursor at, Type declared = {});
    static Place anonymous_place(std::string spelling);

    // The places a statement forms: a dereference under its capability, an element
    // selected at a term, a container's element at its generation.
    std::optional<std::size_t> resolve_storage(CXCursor cursor, Locals& state, Capability::Kind required);
    Type declared_place_type(CXCursor declaration, const std::vector<PlaceStep>& prefix, const Locals& state) const;
    static Type walk_components(Type current, const std::vector<PlaceStep>& path);
    std::optional<std::size_t> resolve_symbolic_element(Locals& state, const ResolvedAccess& access);
    std::optional<std::size_t> symbolic_element_at(Locals& state, CXCursor declaration,
                                                   const std::vector<PlaceStep>& path, Expr selected, bool receiver);
    static std::optional<std::size_t> find_symbolic(const Locals& locals, CXCursor declaration,
                                                    const std::vector<PlaceStep>& path, const Expr& index_value);
    std::optional<std::size_t> resolve_sequence_element(CXCursor cursor, Locals& state, Capability::Kind required);
    bool element_capability(const ElementRegion& region, Capability::Kind required);
    std::optional<std::size_t> sequence_element_at(Locals& state, const ElementRegion& region,
                                                   const std::vector<PlaceStep>& path, Expr index, Expr length,
                                                   std::string spelling);
    bool materialize(CXCursor cursor, Locals& state);
    [[nodiscard]] static bool is_sequence_subscript(CXCursor cursor);
    std::optional<bool> form_pointee_receiver(CXCursor call, Locals& state);
    bool materialize_derefs(CXCursor cursor, Locals& state, unsigned depth = 0);

    // Which places a write may reach.
    [[nodiscard]] bool may_alias(const Local& target, const Local& other) const;
    CallEffect new_generation(Local& root, std::string reason);
    std::vector<std::size_t> invalidate_aliases(std::size_t storage, Locals& state);
    std::vector<std::size_t> invalidate_pointee_aliases(Locals& state, const std::vector<std::size_t>& handed,
                                                        const std::vector<std::size_t>& invalidated);
    void mark_sequence_writes(CXCursor root, const Locals& locals, std::vector<bool>& written, unsigned depth = 0);

    // A call: the storage it hands its callee, and the versions it leaves.
    [[nodiscard]] std::optional<HandedStorage> handed_storage(CXCursor argument, const Locals& state) const;
    std::optional<std::string> view_arguments(CXCursor call, const std::vector<CXCursor>& formals, Locals& state,
                                              std::vector<std::size_t>& invalidated,
                                              std::vector<std::size_t>& written_roots, bool unsafe_callee);
    bool form_places(CXCursor cursor, Locals& state);
    // What `lower` returns, with the places it formed bound around it: a bound or
    // a capability such a place owes is owed on the routes reaching it and nowhere
    // else.
    template <typename Lower> std::optional<Expr> forming(CXCursor at, Lower&& lower) {
        std::vector<Local> enclosing;
        enclosing.swap(formed_derefs);
        std::optional<Expr> result = std::forward<Lower>(lower)();
        if (result) {
            result = bind_formed_derefs(std::move(*result), at);
        }
        formed_derefs = std::move(enclosing);
        return result;
    }
    std::optional<Expr> evaluate(CXCursor cursor, Locals& state, std::vector<std::size_t>& invalidated);
    std::optional<Expr> lower_call(CXCursor statement, const Continuation& next, const Locals& locals, unsigned depth);

    // A modeled sequence's statements: a mutator, and a local's construction.
    std::optional<Expr> lower_sequence_statement(const SequenceCall& call, CXCursor statement, const Continuation& next,
                                                 const Locals& locals, unsigned depth);
    [[nodiscard]] static std::optional<CXCursor> moved_operand(CXCursor cursor);
    [[nodiscard]] static std::optional<std::string> refinement_gap(const Local& target, const Local& source);
    [[nodiscard]] Expr read_root(const Locals& state, std::size_t root, CXCursor at) const;
    std::optional<Expr> lower_sequence_declaration(CXCursor declaration, const std::string& name, const Type& type,
                                                   const std::vector<CXCursor>& declared, std::size_t index,
                                                   const Continuation& next, const Locals& locals, unsigned depth);

    // Statements, conditions and `if`.
    std::optional<Expr> lower_statements(const Continuation& from, const Locals& locals, unsigned depth);
    std::optional<Expr> lower_statement(CXCursor statement, const Continuation& next, const Locals& locals,
                                        unsigned depth);
    std::optional<Expr> lower_statement_form(CXCursor statement, const Continuation& next, const Locals& locals,
                                             unsigned depth);
    std::optional<Expr> lower_returned(CXCursor value, CXCursor statement, const Locals& locals, unsigned depth);
    [[nodiscard]] std::optional<SelectedValue> selection_to_split(CXCursor statement) const;
    std::optional<Expr> lower_selected_statement(CXCursor statement, const SelectedValue& selected,
                                                 const Continuation& next, const Locals& locals, unsigned depth);
    std::optional<Expr> lower_for(CXCursor statement, const Continuation& next, const Locals& locals, unsigned depth);
    std::optional<Expr> lower_condition(CXCursor condition, const Branch& when_true, const Branch& when_false,
                                        const Locals& locals, unsigned depth);
    std::optional<Expr> lower_if(CXCursor statement, const Continuation& next, const Locals& locals, unsigned depth);
    std::optional<Expr> lower_branch(const IfHeader& header, const Locals& locals, unsigned depth);
    std::optional<Expr> lower_branch(CXCursor statement, const std::vector<CXCursor>& parts, const Continuation& next,
                                     const Locals& locals, unsigned depth);

    // A range-based `for`.
    std::optional<Expr> lower_range_for(CXCursor statement, const Continuation& next, const Locals& locals,
                                        unsigned depth);
    Expr range_length(const RangeIteration& range, const Locals& locals) const;
    Expr range_condition(const RangeIteration& range, const Locals& locals) const;
    Expr range_measure(const RangeIteration& range, const Locals& locals) const;
    void mark_range_writes(const RangeIteration& range, const Locals& locals, std::vector<bool>& written) const;
    std::optional<Expr> lower_range_iteration(const RangeIteration& range, const Continuation& body, const Locals& head,
                                              unsigned depth);
    std::optional<Expr> initialize_range_variable(const RangeIteration& range, const Continuation& body,
                                                  const Locals& head, unsigned depth);
    std::optional<Expr> advance_range(const LoopFrame& frame, const Locals& locals, unsigned depth);

    // An unsafe block on the path.
    [[nodiscard]] std::vector<std::size_t> unsafe_reach(const Locals& locals) const;
    std::optional<Expr> lower_unsafe(CXCursor block, CXCursor marker, const Continuation& next, const Locals& locals,
                                     unsigned depth);

    // A claim that a path cannot occur, and a case split on it.
    [[nodiscard]] std::optional<std::string> contradiction_marker(CXCursor statement) const;
    std::optional<Expr> lower_contradiction(const std::string& marker, const std::vector<CXCursor>& statements,
                                            std::size_t index, const Locals& locals);
    [[nodiscard]] static std::optional<CXCursor> declared_as(CXCursor statement, std::string_view name);
    [[nodiscard]] std::optional<std::string> split_marker(const std::vector<CXCursor>& statements,
                                                          std::size_t index) const;
    std::optional<Expr> lower_split(const std::string& marker, const Continuation& from, const Locals& locals,
                                    unsigned depth);

    // Loops.
    [[nodiscard]] bool is_instantiation_marker(CXCursor statement) const;
    [[nodiscard]] std::optional<LoopMarker> invariant_marker(CXCursor statement) const;
    std::optional<Expr> lower_loop(const LoopHeader& header, const Locals& locals, unsigned depth);
    std::optional<Expr> end_iteration(const LoopFrame& frame, bool after_increment, const Locals& locals,
                                      unsigned depth);
    std::optional<Expr> lower_break(const Locals& locals, unsigned depth);
    void leave_switches_inside(const LoopFrame& frame);

    // `switch`.
    std::optional<Expr> lower_switch(CXCursor statement, const Continuation& next, const Locals& locals,
                                     unsigned depth);
    std::optional<Expr> case_value(CXCursor value, const Type& type);
    std::optional<Expr> lower_switch_dispatch(const SwitchHeader& header, const Locals& locals, unsigned depth);
    std::optional<Expr> leave_switch(const SwitchFrame& frame, const Locals& locals, unsigned depth);

    // Local declarations, aggregate ones included.
    std::optional<std::string> collect_leaves(const Type& type, CXCursor initializer, const std::string& written,
                                              const std::vector<PlaceStep>& prefix, std::vector<AggregateLeaf>& leaves);
    std::optional<std::string> collect_type_leaves(const Type& type, const std::string& written,
                                                   const std::vector<PlaceStep>& prefix,
                                                   std::vector<AggregateLeaf>& leaves);
    std::optional<Expr> lower_aggregate(CXCursor declaration, const std::string& name, const Type& type,
                                        const std::vector<CXCursor>& declared, std::size_t index,
                                        const Continuation& next, const Locals& locals, unsigned depth);
    std::optional<Expr> lower_declaration(const std::vector<CXCursor>& declared, std::size_t index,
                                          const Continuation& next, const Locals& locals, unsigned depth);

    // Ghost state.
    std::optional<Expr> lower_ghost(const Continuation& from, const Locals& locals, unsigned depth);
    std::optional<Expr> lower_ghost_declaration(const std::vector<CXCursor>& declared, std::size_t index,
                                                const Continuation& next, const Locals& locals, unsigned depth);

    // Writes: the place a write targets, assignments and updates.
    std::optional<std::optional<std::size_t>> read_reference(CXCursor argument, Locals& locals, CXCursor callee,
                                                             bool unsafe_callee);
    std::optional<std::size_t> written_local(CXCursor target, Locals& locals);
    std::optional<Expr> write(std::size_t local, Expr value, CXCursor statement, const Continuation& next,
                              const Locals& locals, unsigned depth);
    std::optional<Expr> write_then(std::size_t local, Expr value, CXCursor statement, const Locals& locals,
                                   const std::function<std::optional<Expr>(const Locals&)>& rest);
    std::optional<Expr> lower_assignment(CXCursor statement, const Continuation& next, const Locals& locals,
                                         unsigned depth);
    std::optional<Expr> lower_update(CXCursor statement, const Continuation& next, const Locals& locals,
                                     unsigned depth);
};

} // namespace cppl::clangbridge::detail
