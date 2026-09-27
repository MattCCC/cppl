# RFC 0021: Runtime-checked refinement construction

## Status

Accepted. Implemented as described in `docs/STATUS.md` ("Runtime checked
refinement construction", "Runtime refinement validation"). Normative text:
`docs/SPEC.md` §28.5, RUNTIMECHECK-010 to RUNTIMECHECK-017; trust impact:
`docs/TRUST.md` §26.3 and §31.1.

## Summary

A value C++L cannot know at compile time -- read from a file, a socket, the
command line or a foreign call -- enters a refined type through ordinary C++:
the program checks the predicate and moves the value into the refined type only
on the path where the check held. SPEC.md §28 already says this is how runtime
validation works and that its facts are `RUNTIME-CHECKED`, not `PROVEN`. This
RFC makes that distinction executable and visible: it defines which refinement
crossings a runtime check establishes (runtime validation sites), how the
implementation decides it without adding any proof power, how the trust report
lists each site and every claim resting on one, and how a site crosses
translation units.

It adds no syntax, no library, no runtime code and no kernel rule.

## Motivation

Before this RFC every refinement crossing was either proven or refused, and the
trust report printed `Runtime validation sites: 0` as a constant. A program such
as

```cpp
type Percentage = int where (self >= 0 && self <= 100);

verified int percentage_or_zero(int raw)
    ensures (result >= 0 && result <= 100)
{
    if (raw >= 0 && raw <= 100) {
        Percentage p = raw;
        return p;
    }
    return 0;
}
```

was reported with its contract assumption-free and nothing else, although the
fact `raw satisfies Percentage` holds only because the `if` executed
(RUNTIMECHECK-008, ORTHOCHECK-001). `TRUST.md` TCB-REPORT-004 forbids displaying
such a fact as a universal static proof, and TRUST.md 36.1 asks every claim
record to carry its runtime-check dependencies. Neither was possible.

## Goals

- Identify every crossing whose membership is established by a runtime check,
  in every form a crossing takes: locals, members, elements, content invariants
  of vector locals, refined results, refined parameters of verified callees and
  call post-states.
- Decide it soundly: never report a crossing as established statically unless
  the kernel accepted evidence for it without the check.
- List each site `RUNTIME-CHECKED` with its location, refinement, predicate and
  function, and every claim resting on it, directly or through verified calls,
  in this unit and across units.
- Keep the check ordinary runtime code, unchanged by erasure.

## Non-goals

- A `validate<T>()` construct or a runtime validation library
  (RUNTIMECHECK-001, ERASE-013).
- Any new proof power. The obligation every crossing owes is unchanged.
- Validation of unverified callers of a verified function. A refined parameter
  of a verified function is a precondition the caller owes (REFINE-026); an
  unverified caller is outside the verified region (CONTRACT-006).
- Proof automation for Boolean helper functions. RUNTIMECHECK-006 allows a
  checked helper whose postcondition relates its `bool` result to the predicate;
  whether such a postcondition is provable is the automation's concern, and
  today it is not proven for a `bool` result (see STATUS).

## Proposed syntax

None. The surface is SPEC.md §28: an ordinary C++ condition and a crossing on
the path where it held.

## Static semantics

A *refinement crossing* (RUNTIMECHECK-016) is any point where a value enters a
refinement type. A *runtime condition* (RUNTIMECHECK-017) is the outcome of a
condition C++ evaluates at run time to select the path: an `if`, a `?:`, the
operands of `&&`, `||` and `!` in such a condition, a loop's condition. A case
split (CASE-017) is not one: it evaluates nothing and its arms cover every
state.

A crossing selected by at least one runtime condition is a *checked crossing*
(RUNTIMECHECK-010). It owes exactly what every crossing owes: its membership,
closed over everything its path supposes, the runtime conditions included. That
obligation decides whether the program verifies; this RFC does not touch it.

In addition, the crossing's membership is stated a second time, closed over the
same path without its runtime conditions: over the parameters, preconditions
and refined parameters, the binders and postconditions of the calls the path
made, loop invariants, the defined behavior established, case facts, and the
values computed. Leaving out a supposition binds nothing, so every term keeps
its meaning. If the kernel accepts evidence for that proposition, the crossing
is *established statically*: the value satisfies the predicate on every
execution that reaches it, check or no check. Otherwise it is a *runtime
validation site* (RUNTIMECHECK-011): the value's membership there is
`RUNTIME-CHECKED`.

The decision (RUNTIMECHECK-012) is made by offering the unguarded proposition
the same strategies an obligation is offered and submitting the candidate to the
kernel. Failure to find evidence only ever reports a crossing weaker than it may
be; nothing is owed or supposed because of it, and no obligation depends on it.
A crossing reached on several paths -- a conditional initializer, a crossing
before a split -- is a site when any path needs the check.

A claim rests on a site (RUNTIMECHECK-014) when the site is in its own body or
in the body of a verified function whose contract it was proven through, to a
fixed point over the call graph, exactly as an unsafe block propagates
(TRUST.md TCB-REPORT-005). A claim that a path or a case of a body cannot occur
rests on its body's sites.

The claim stays `PROVEN`: every execution reaching the site passed the check, so
the contract holds of every execution. What it rests on is that the executable
performs the check as written. That is ordinary runtime behavior, preserved by
erasure (ERASE-012), not a trusted assumption (INTERACT-023), so such a claim
may be assumption-free and is listed separately as runtime-check-dependent.

## Runtime semantics

Unchanged. The check is the program's own `if`, loop or conditional operator,
and erasure keeps it byte for byte (RUNTIMECHECK-009, TCB-RUNTIMECHK-003). The
failure path is ordinary C++ and cannot construct the refined value: a crossing
on it owes the predicate like any other and is refused (RUNTIMECHECK-007).

## C++ interoperability

No C++ construct changes meaning. Templates are checked per specialization as
before; a site in a specialization is reported at its location. The standard
library is involved only through the existing sequence model: an element pushed
into a `std::vector<Refined>` local on a checked path is a site like a local.

## Safety

Rejected, as before, and now pinned by `negative/runtime_validation.sh`: a
crossing on the failure path, before the check, of another value than the one
checked, after a write, verified call or unsafe block that gave the checked
local a new version, through a disjunction's route or a failed conjunction's,
after a loop's condition stopped holding, and with an off-by-one check.

The new behavior can only report a crossing as a site that is not one (when
automation cannot prove the unguarded membership). It cannot hide a site: a
crossing is removed from the list only by kernel-accepted evidence.

## Trust impact

No kernel rule, axiom or trusted assumption. The classification is reporting
TCB (TRUST.md TCB-REPORT-006): its failure mode that matters is omitting a site,
which requires the kernel to accept evidence for a proposition that is false or
that differs from the crossing's membership. The unguarded proposition is built
by the same correspondence-layer code that builds the crossing's obligation,
leaving out the runtime conditions, so the correspondence TCB grows by that
omission (`compiler/obligations/src/contracts.cpp`, `Conditions::close`,
`Conditions::cross`, `crossing`), the attribution fixed point
(`compiler/obligations/src/trust.cpp`) and the classifier
(`compiler/automation/src/evidence.cpp` `classify_crossings`, whose own verdict
is the kernel's). TRUST.md §26.3 records the delta.

Across units, the verification interface gains a `runtime` dependency category
(format version 3) that takes part in the entry's result identity, so a record
with a site edited away no longer matches a record proven through it
(TUBOUND-009). Interface provenance remains unauthenticated (TCB-XTU-010): an
edit of a unit's own record with its checksum recomputed is not detected, as for
every other category. The declared verification-semantics version is raised.

## Erasure

Nothing new is erased and nothing new is kept. The fixture
`tests/fixtures/runtime_validation.cpp` and its hand erasure compile to
identical assembly at `-O0` and `-O2` in c++17, c++20 and c++23.

## Diagnostics

A refused crossing keeps its existing diagnostic ("this value is not shown to
satisfy refinement type 'Positive'", or the call precondition or return path
that owes it). The trust report adds:

```text
  relying on runtime checks: N               (per claim kind)
Runtime-check-dependent claims: N
  contract of f (file:line), identity ...
    rests on the runtime check of Positive (file:line:col), in its own body
Runtime validation sites:    N
  RUNTIME-CHECKED:           file:line:col, a value enters Positive, where (self > 0), in verified function f
```

## Alternatives considered

- *Classify syntactically: every crossing under a condition is a site.* Simple
  and conservative, but it reports literals and precondition-bounded values as
  runtime-checked, which makes the list useless for audit.
- *Try each condition separately to find the one the crossing needed.* More
  informative, exponential in the worst case, and not needed for soundness.
- *Report a claim resting on a site as not assumption-free.* Rejected: it would
  conflate `RUNTIME-CHECKED` with `TRUSTED`, which INTERACT-023 keeps distinct.
- *An explicit validation construct.* Rejected by RUNTIMECHECK-001 and
  ERASE-013; C++ control flow already is the validation.

## Drawbacks

Each checked crossing costs one more automation attempt. A crossing established
statically by reasoning the automation cannot reproduce without the check is
reported as a site.

## Testing strategy

- Positive and runtime: `tests/fixtures/runtime_validation.cpp`, run on valid,
  invalid and extreme input (`e2e/runtime_validation.sh`).
- Negative: 14 refused twins in `tests/fixtures/negative/runtime_check_*.cpp`
  (`negative/runtime_validation.sh`).
- Reporting: the full site list and claim list compared whole; static crossings
  under a check pinned absent.
- Erasure: identical assembly against the hand erasure.
- Cross-unit: `tests/fixtures/runtime_validation_cross_tu/`, three units, and a
  record with its site edited away refused beside one proven through it.
- Unit: `tests/unit/trust_closure_test.cpp` (propagation, verdicts, faults),
  `tests/unit/interface_test.cpp` (format, identity, refusals).
- Mutation: `scripts/test-mutations.sh` breaks the classifier, the guard
  bookkeeping of both body walkers, the propagation and the interface identity.

## Compatibility

Interfaces of format version 2 are refused and must be rebuilt. No C++L or C++
source changes meaning.

## Unresolved questions

- A machine-readable report form (owned with the rest of the report machinery).
- Naming the specific condition a site rested on, rather than the site alone.
