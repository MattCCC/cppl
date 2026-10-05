# RFC 0021: Runtime-checked refinement construction

## Status

Accepted. Implemented as described in `docs/STATUS.md` ("Runtime checked
refinement construction", "Runtime refinement validation"). Normative text:
`docs/SPEC.md` §28, RUNTIMECHECK-001 to RUNTIMECHECK-021, and WORD-013; syntax:
`docs/GRAMMAR.md` 16.1; trust impact: `docs/TRUST.md` §26.3 and §31.1.

Revised before release. The first design classified refinement crossings: a
crossing on a path an ordinary condition selected was re-proven without that
condition, and reported a runtime validation site when the kernel did not
accept the result. That made a static proof from path facts, such as
`if (x > 0) { Positive p = x; }`, look like a runtime check, and made what a
report listed as runtime-checked depend on what the automation happened to
find. It was withdrawn in favor of the explicit validation below, and no
release shipped it.

## Summary

A value C++L cannot know at compile time -- read from a file, a socket, the
command line or a foreign call -- comes to satisfy a refinement in one of two
ways, and this RFC keeps them apart:

```text
ordinary C++ condition     the verifier proves membership
selects the path           from the path's facts             PROVEN

validate<R>(e)             the program tests the value
                           against R's predicate at run
                           time, and its success
                           establishes membership            RUNTIME-CHECKED

anything else              refused
```

A path fact is a premise of static proof: a crossing proven from it is proven,
and C++L adds no runtime code for it. A runtime validation site exists only
where the program writes a validation expression, and the code it runs is the
program's own request. The trust report lists each site and every claim
resting on one, and a site crosses translation units in the verification
interface.

It adds one expression form, no library, no runtime code the program does not
request, and no kernel rule.

## Motivation

Reading a value from outside the program and moving it into a refined type is
the ordinary way a refinement meets the world. When the program already tests
the predicate with an `if`, the verifier can prove membership on the branch the
`if` selects, and that is a proof like any other: every execution reaching the
crossing satisfies the predicate. Nothing about it is runtime-checked in the
sense of `TRUST.md` TCB-REPORT-004, because the verifier established the fact;
it did not suppose it.

What was missing is the other case: a program that wants to *ask* whether a
value satisfies a refinement, without restating its predicate, and to be told
honestly that what follows rests on that test. Restating the predicate by hand
duplicates it, and drifts from it when the declaration changes. A helper
function returning `bool` needs a contract relating its result to the
predicate, which the program then has to prove. A validation expression is the
refinement's own test, whose meaning is fixed by the declaration; what a proof
takes from it is reported as `RUNTIME-CHECKED`, not as proven.

## Goals

- Keep a static proof from path facts `PROVEN`, with no site and no runtime
  code (RUNTIMECHECK-010).
- Give programs an explicit validation, `validate<R>(e)`, whose success makes
  membership available and whose failure makes nothing available
  (RUNTIMECHECK-011).
- Never turn an unproven crossing into a site: failure to prove is refused
  (RUNTIMECHECK-013).
- List each site `RUNTIME-CHECKED` with its location, refinement, predicate and
  function, and every claim resting on it, directly or through verified calls,
  in this unit and across units (RUNTIMECHECK-014, RUNTIMECHECK-015).
- Keep the validation ordinary runtime code, kept by erasure (RUNTIMECHECK-021).

## Non-goals

- A runtime validation library, or runtime support code the program did not
  request (RUNTIMECHECK-001, ERASE-013).
- Validating indexed refinements, refinements whose base type is itself a
  refinement, or refinements whose predicate is a formal proposition or
  evaluates an operation C++ defines only under a condition on its operands
  (RUNTIMECHECK-020).
- Validation of unverified callers of a verified function. A refined parameter
  of a verified function is a precondition the caller owes (REFINE-026); an
  unverified caller is outside the verified region (CONTRACT-006).

## Proposed syntax

```ebnf
validation-expression ::= "validate" "<" identifier ">" "(" expression ")"
```

```cpp
type Positive = int where (self > 0);

verified int positive_or_one(int raw)
    ensures (result > 0)
{
    if (validate<Positive>(raw)) {
        Positive p = raw;
        return p;
    }
    return 1;
}
```

`validate` is a contextual word: where the translation unit declares or uses
`validate` for anything else, the spelling is ordinary C++ and a warning says so
(WORD-013). A validation stands only in the body of a verified function,
outside every unsafe block: never in a contract clause, loop clause, Law, proof,
refinement predicate, declaration or unverified function (RUNTIMECHECK-019).

## Static semantics

The identifier names one refinement type without indices that the unit
declares; the argument has its base type; the validation is a `bool`
(RUNTIMECHECK-018). The body walker models it as a call of a function whose
postcondition is `result -> P(argument)`, with `P` the refinement's predicate,
supposed of a fresh result exactly as a verified callee's postcondition is. On
the path where the result is `true`, membership of the value tested follows by
ordinary kernel reasoning, for the logical version the value had where it was
tested; a later write, a write through a possible alias, a call effect, a loop
head or an unsafe block gives the storage a new version that no earlier fact
describes (RUNTIMECHECK-012). On the path where it is `false`, nothing follows.

Every refinement crossing owes its membership under its complete path context
(RUNTIMECHECK-017): preconditions, refined parameters, every condition outcome
selecting it, case facts, call postconditions, loop invariants, defined
behavior, the values computed, and the facts of the validations it passed. The
kernel's acceptance of that obligation decides whether the program verifies,
and nothing else does. No crossing is classified and no condition is set aside.

A claim rests on a site (RUNTIMECHECK-014) when the site is in its own body or
in the body of a verified function whose contract it was proven through, to a
fixed point over the call graph, exactly as an unsafe block propagates
(TRUST.md TCB-REPORT-005). A claim that a path or a case of a body cannot occur
rests on its body's sites.

The claim stays `PROVEN`: it is proven statically, relative to the specified
semantics of each validation it rests on. What it rests on is that the
executable performs each validation as its lowering states. That is runtime
behavior preserved by erasure (ERASE-012), not a trusted assumption
(INTERACT-023), so such a claim may be assumption-free and is listed separately
as runtime-check-dependent.

## Runtime semantics

`e` is evaluated once, and the validation yields `true` exactly when `R`'s
predicate holds of its value. The refinement declaration lowers, beside its
alias, to a function whose parameter is `self` of the base type and whose body
returns the predicate as written; the validation lowers to a call of it. A
refinement no validation of the unit names lowers exactly as before (REFINE-016).

```cpp
using Positive = int; [[maybe_unused]] static inline bool __cppl_validate_1(int self) { return static_cast<bool>(self > 0); }

int positive_or_one(int raw) {
    if (__cppl_validate_1(raw)) {
        Positive p = raw;
        return p;
    }
    return 1;
}
```

Because the predicate must have defined behavior for every value of the base
type (RUNTIMECHECK-020), the test itself cannot have undefined behavior. The
failure path is ordinary C++ and cannot construct the refined value: a crossing
on it owes the predicate like any other and is refused (RUNTIMECHECK-007).

## C++ interoperability

No C++ construct changes meaning: `validate` keeps any meaning the unit gives
it. Templates are checked per specialization as before; a site in a
specialization is reported at its location. The standard library is involved
only through the existing sequence model: an element pushed into a
`std::vector<Refined>` local after a validation is a crossing like a local.

## Safety

Refused, and pinned by `negative/runtime_validation.sh`: a crossing on a
validation's failure path, after a write gave the validated local a new
version, of another value than the one validated, into a stronger refinement
than the one validated, or on a disjunction's route where the validation
failed; a validation outside a verified body, in a contract or loop clause, in
an unsafe block, naming no refinement, a qualified name or an indexed
refinement, or of a refinement with a formal or partially defined predicate or a
refined base type. The ordinary-condition twins -- a crossing on the failure path, before the
check, of another value, under too weak a check, after a write, verified call
or unsafe block, through a disjunction's route or a failed conjunction's, after
a loop's condition stopped holding -- are refused as before.

## Trust impact

No kernel rule, axiom or trusted assumption. Correspondence TCB: the
validator's lowering and the site's call of it, recomputed by the erasure
checker (`compiler/frontend/src/projection.cpp` `canonical_lowering`,
`lowered_validation`; `compiler/erasure/src/erase.cpp`), and the validation's
postcondition, which states over the value tested the predicate the validator
evaluates (`compiler/obligations/src/contracts.cpp` `validation_test`).
Reporting TCB: the recognizer's record of every site
(`compiler/frontend/src/recognizer.cpp`) and the attribution fixed point
(`compiler/obligations/src/trust.cpp`). TRUST.md §26.3 records the delta
(TCB-RUNTIMECHK-005, TCB-RUNTIMECHK-006).

Across units, the verification interface carries a `runtime` dependency
category (format version 3) that takes part in the entry's result identity, so
a record with a site edited away no longer matches a record proven through it
(TUBOUND-009). Interface provenance remains unauthenticated (TCB-XTU-010). The
declared verification-semantics version is `cppl-verification-3`, so an
interface written under the classifying design is refused.

## Erasure

The validation is kept, lowered to a call of the validator; nothing else new is
kept or erased. The fixture `tests/fixtures/runtime_validation.cpp` and its hand
erasure compile to identical assembly at `-O0` and `-O2` in c++17, c++20 and
c++23.

## Diagnostics

A refused crossing keeps its existing diagnostic ("this value is not shown to
satisfy refinement type 'Positive'", or the call precondition or return path
that owes it). A misplaced or malformed validation is refused by name ("a
validation expression is checked only in the body of a verified function", "a
loop clause states a proposition, and a validation expression is runtime
code", "'Count' does not name a refinement type this translation unit
declares"). The trust report adds:

```text
  relying on runtime checks: N               (per claim kind)
Runtime-check-dependent claims: N
  contract of f (file:line), identity ...
    rests on the validation of Positive (file:line:col), in its own body
Runtime validation sites:    N
  RUNTIME-CHECKED:           file:line:col, validates a value against Positive, where (self > 0), in verified function f
```

## Alternatives considered

- *Classify crossings: a crossing under a condition is a site unless it can be
  re-proven without the condition.* The first design. Rejected: a proof from
  path facts is static proof, and calling it runtime-checked misstates what was
  established; the list depended on the automation; and the reporting TCB grew
  by the classifier and the condition bookkeeping of both body walkers.
- *Classify syntactically: every crossing under a condition is a site.*
  Rejected for the same reason, and it reports literals and precondition-bounded
  values as runtime-checked.
- *Turn an unproven crossing into an inserted check.* Rejected by
  RUNTIMECHECK-013: failure to prove is refused, never repaired at run time.
- *A validation library.* Rejected by RUNTIMECHECK-001 and ERASE-013: the
  validator is lowered from the declaration, per unit, and only when named.

## Drawbacks

`validate` is a new contextual word; a unit using it for anything else loses
validations (with a warning), not its meaning. Each refinement a unit validates
gains one inline function in the runtime text.

## Testing strategy

- Positive and runtime: `tests/fixtures/runtime_validation.cpp`, path-fact
  crossings pinned as no sites, validations in conditions, `&&`, `!`, a `bool`
  local, a loop condition and after a revalidation, run on valid, invalid and
  extreme input (`e2e/runtime_validation.sh`).
- Negative: the refused twins in `tests/fixtures/negative/runtime_check_*.cpp`
  and `validation_*.cpp` (`negative/runtime_validation.sh`).
- Reporting: the full site list and claim list compared whole.
- Erasure: the validator and every call of it pinned in the runtime text, and
  identical assembly against the hand erasure.
- Cross-unit: `tests/fixtures/runtime_validation_cross_tu/`, three units, and a
  record with its site edited away refused beside one proven through it.
- Unit: `tests/unit/trust_closure_test.cpp` (propagation, faults),
  `tests/unit/interface_test.cpp` (format, identity, refusals).
- Mutation: `scripts/test-mutations.sh` makes the validation fact
  unconditional, flips its polarity, drops a site, lets a partially defined
  predicate be validated, lets a validation stand in a loop clause, and breaks
  the propagation and the interface identity.

## Compatibility

Interfaces of format version 2, and of version 3 under verification semantics
`cppl-verification-2`, are refused and must be rebuilt. A unit declaring
`validate` keeps its C++ meaning.

## Unresolved questions

- Validating indexed refinements, whose validator would take the indices as
  parameters, and layered refinements, whose validator would test the base's
  predicate too.
