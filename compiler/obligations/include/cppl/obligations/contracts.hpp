#pragma once

#include "cppl/artifact/interface.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/source/digest.hpp"
#include "cppl/source/location.hpp"
#include "cppl/source/representation.hpp"
#include "cppl/vir/ids.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace cppl::obligations {

// A contract another translation unit proved, as this unit knows it: which
// verification interface recorded it, the identity of that record, and what
// the other unit's proof rests on (SPEC.md TUBOUND-003, TUBOUND-006). The proposition
// is never taken from here: it is the one this unit stated from its own
// declaration, which is used only because the record states the same one.
struct ImportedContract {
    std::string origin; // the interface file
    source::Digest entry;
    std::vector<artifact::Premise> premises;
    std::vector<artifact::UnsafeBlock> unsafe;
    std::vector<artifact::Dependency> depends;
};

// One precondition of the callee, instantiated at a call: the obligation that
// proves it where the call is made, and the same goal stated over earlier
// call results rather than the calls themselves.
struct CallPrecondition {
    std::size_t obligation = 0;
    kernel::Proposition reasoning_goal;
};

struct CallVerification {
    vir::FunctionId callee;
    std::string callee_name;
    std::vector<kernel::Term> arguments;
    kernel::Term value;
    kernel::Type result;
    // Scope: caller parameters, earlier call results, this call's result.
    kernel::Proposition postcondition;
    std::vector<CallPrecondition> preconditions; // in the callee's source order
    std::size_t conditions = 0;
};

struct PathCondition {
    kernel::Proposition actual;
    kernel::Proposition abstract;
    std::size_t calls = 0;
};

struct ReturnPath {
    std::vector<PathCondition> conditions;
    std::vector<CallVerification> calls;
    kernel::Term returned_value;
    std::size_t obligation = 0;
    kernel::Proposition reasoning_goal;
};

// One obligation of a partial-correctness contract, with the contracts whose
// postconditions it supposes. Those must be established before it is used.
struct VerificationCondition {
    std::size_t obligation = 0;
    std::vector<std::size_t> callees; // indices into Program::contracts
};

// A trusted library summary: what one modeled standard-library operation is
// assumed to do, stated over the abstract value its model keeps (SPEC.md
// STDMODEL-013, RFC 0020 §6). It is supposed at a call exactly as a verified
// callee's postcondition is, after its preconditions are owed, and is never
// verified: nothing about libc++ or libstdc++ is checked. Every claim resting
// on a function that uses one names the model in its trust closure
// (TRUST.md 28.1).
struct LibrarySummary {
    std::string symbol; // the identity the bridge gave the call
    std::string name;   // as the call is written, for diagnostics
    source::LibraryCall library;
    std::vector<kernel::Type> parameters;
    kernel::Type result;
    // Each over the parameters, in the callee's order.
    std::vector<kernel::Proposition> preconditions;
    // Over the parameters -- post-state values in the positions the call
    // writes -- then the result. Absent when the operation states nothing.
    std::optional<kernel::Proposition> postcondition;
};

struct ContractVerification {
    vir::FunctionId function;
    std::string name;
    // The function's resolved identity, Clang's USR, which separates overloads,
    // qualifiers and template specializations (SPEC.md TUBOUND-004).
    std::string symbol;
    // The statement as the kernel describes it, for diagnostics only.
    std::string description;
    // Set when the contract is another unit's, established here by a validated
    // verification interface rather than by any obligation of this unit. Such a
    // contract is partial in construction, has no conditions, and is never a
    // claim of this unit (SPEC.md TUBOUND-003, TUBOUND-006).
    std::optional<ImportedContract> imported;
    std::vector<kernel::Type> parameters;
    // Each is supposed in turn, P1 -> ... -> Pn -> Q, which is their
    // conjunction without a conjunction connective.
    std::vector<kernel::Proposition> preconditions;
    // Scope: function parameters followed by its specification-only result.
    kernel::Proposition postcondition;
    kernel::Type result;
    kernel::Term returned_value;
    kernel::Term named_value;
    kernel::Proposition theorem;
    std::vector<ReturnPath> paths;
    std::size_t obligation = 0;

    // The conditions a partial body (see `partial`) is proven by.
    std::vector<VerificationCondition> conditions;

    // Where the body passes through an unsafe block, in source order. The
    // contract is proven with each block's effects modeled as unknown writes,
    // so it holds only if the block's own code is sound: it rests on it, and so
    // does every contract that calls this one (SPEC.md 26, TRUST.md
    // TCB-REPORT-005). A body with an unsafe block is always partial.
    std::vector<source::SourceLocation> unsafe_regions;

    // Loops a path of the body enters that state no measure, each once. Where
    // one stands, the body's termination is not established.
    std::vector<source::SourceLocation> unmeasured_loops;

    // The recursion group the function belongs to, this contract included, as
    // indices into `Program::contracts`: functions that call each other,
    // directly or through one another. Empty when the function does not
    // recurse. A condition may suppose a member's contract before it is
    // established, as the induction hypothesis the group's measure descents
    // justify, so the members are established together or not at all (SPEC.md
    // TERMINATION-007).
    std::vector<std::size_t> recursion;

    // The standard-library models the function's contract and body rest on,
    // each once (RFC 0020 §10). What they state is trusted, so the claim is
    // reported as resting on them, and so is every claim calling this one.
    std::vector<source::RepresentationKind> library_models;

    // The fields below are byte-aligned, and stand together so the record
    // carries little padding.

    // A body that is not one total core term: it contains a loop, or calls a
    // function whose contract is itself partial. Its contract states partial
    // correctness and holds when every condition in `conditions` is proven and
    // every contract they suppose is established (SPEC.md 23, 24). Such a
    // function is never admitted as a core definition, so no theorem about its
    // value exists, and the fields that describe one (`returned_value`,
    // `theorem`) are unused.
    bool partial = false;

    // Whether the contract is a total-correctness claim (SPEC.md CORRECT-003):
    // every loop its paths enter has a measure whose descent is an obligation,
    // it passes through no unsafe block, and every contract it calls is total,
    // its recursion group's own included, whose calls descend a measure. When
    // false, it states partial correctness: what holds if the function returns.
    bool total = false;

    source::Digest identity; // content identity of the conditions, for callers

    // The canonical identity of what the contract states, the same in every
    // unit that states it: parameter and result types, passing modes,
    // preconditions, postcondition, memory capabilities, measures and every
    // pure definition they reach, by content rather than by name or by the
    // number this unit gave a definition (SPEC.md TUBOUND-004). Absent when it could
    // not be computed; such a contract is neither recorded in an interface nor
    // matched against one.
    std::optional<source::Digest> statement;
};

} // namespace cppl::obligations
