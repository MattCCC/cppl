# RFC 0017: Cross-translation-unit verification metadata

## Status

Accepted; implemented as `PROTOTYPE` (see "Implementation" below, and
`SPEC.md` Annex L.2.1, TUBOUND-002 to TUBOUND-009).

## Summary

A verified contract is checked in the translation unit that defines the body,
and nothing of that result crosses to a unit that only sees the declaration. A
caller in another unit therefore cannot use the callee's contract, and a
verified specialization instantiated in one unit is unverified in the next.

This RFC defines the sound mechanism `SPEC.md` Annex L.2 already requires: a
per-unit **verification interface** emitted beside the object file, recording
which entities this unit proved and under exactly what assumptions, and
validated against the consuming unit's own resolved semantics before any of it
is believed. It is a cache of *checked results keyed by full semantic identity*,
never a channel for propositions.

## Motivation

`SPEC.md` TUBOUND-001 permits a caller to reason from a public contract "only
when checked evidence for the implementation corresponding to that contract is
available to the verification environment." Today no mechanism makes it
available, so the honest behaviour is the current one: fail closed.

That is sound but severely limiting. Real programs are many translation units:

```cpp
// clamp.hpp
template <unsigned N>
verified unsigned clamp_to(unsigned x) expects (x < N) ensures (result < N);

// clamp.cpp  -- defines and proves clamp_to<4>
// main.cpp   -- calls clamp_to<4>, cannot use the proof
```

Every verified function is currently useful only inside its own unit, which
means the feature does not compose across a real build. `docs/STATUS.md` records
this as one of the two remaining blockers on verified function templates.

## Goals

- Carry checked results across units without re-checking bodies.
- Fail closed on any mismatch, absence, staleness or ambiguity.
- Keep the kernel unchanged: no new term, rule, axiom or proposition.
- Make the trust delta explicit and reportable.

## Non-goals

- **No cross-unit proof transport.** An interface records *that* an obligation
  was discharged under stated assumptions, not a proof term to re-check
  elsewhere. Transporting evidence is a larger design and is left open.
- No distribution, signing or package format.
- No change to the native ABI, calling convention or erasure.
- No whole-program analysis, link-time verification or global solver.
- No new surface syntax.

## Proposed design

### The artifact

Compiling a unit with verification enabled emits a **verification interface**
beside its object file:

```text
clamp.o
clamp.o.cppli      -- verification interface for clamp.o
```

It records, for each entity this unit verified:

```text
entity        resolved identity (Clang USR)
contract      the formal proposition, as the obligation layer built it
status        proved | refused | not attempted
assumptions   trusted Laws and explicit assumptions in the trust closure
dependencies  the full key manifest below
```

Propositions are recorded in the formal form the obligation layer already
produces, never as source text, so nothing is re-parsed or re-interpreted.

### Identity

Reuse is keyed on `TRUST.md` TCB-CACHE-003's manifest, which this RFC adopts
verbatim rather than inventing a second one. At minimum: obligation identity,
resolved entity identity, preprocessed semantic hash, formal type and refinement
definitions, called contracts and effect summaries, imported Laws, trusted
assumptions, core calculus version, kernel version, semantic model version, C++
language mode, target/ABI properties, library models, and verification
configuration.

`compiler/source/digest.hpp` already provides the hasher, and obligation
identity is already computed this way for the in-unit case, so the mechanism is
an extension of existing identity rather than a parallel one.

### Consumption

When a unit calls an entity it does not define:

```text
1. locate the interface for that entity
2. validate it: well-formed, version-compatible, manifest matches
3. re-resolve the entity's identity in THIS unit
4. rebuild the contract proposition from THIS unit's declaration
5. compare it to the recorded one structurally
6. only then may the caller use the contract
```

Any failure at any step means the contract is unavailable and the caller fails
closed, exactly as today. A mismatch is a diagnostic, never a silent fallback.

Step 4 is the essential one. The consuming unit does not read the proposition
out of the artifact and trust it — it *rebuilds* the proposition from the
declaration it can see, and uses the artifact only to learn that this
proposition was discharged. A disagreement means the two units do not mean the
same thing by the declaration, which is precisely the case that must refuse.

### Templates

A specialization is keyed by its own USR, which already embeds its template
arguments, so `f<4>` and `f<8>` are separate entries with no new rule
(TEMPLATE-003). A unit that instantiates `f<4>` and proves it records `f<4>`;
a unit instantiating `f<8>` gets no entry for it and checks it itself.

This also supplies the missing piece for explicit instantiation: a unit whose
only use is `template unsigned f<4u>(unsigned);` today reports the template as
uninstantiated, because libclang's cursor API exposes no cursor for an explicit
instantiation. That remains true, and this RFC does not fix it; it is recorded
in `docs/STATUS.md` and left to its own change.

## Static semantics

No new judgment. An imported result is a *side condition on availability*, not
a new rule of the formal core:

```text
Gamma |- contract(f) discharged elsewhere, manifest matches
------------------------------------------------------------
the caller may assume the postcondition of f at the call
```

which is exactly the existing in-unit call rule with a different source for
"discharged." The proposition the caller assumes is the one it rebuilt itself.

## Runtime semantics

None. The interface is a build artifact; the object file is unchanged, and
erasure is untouched.

## C++ interoperability

Headers carry the declarations, as Annex L.3 already permits. Redeclaration
follows ordinary C++ entity rules plus contract compatibility — two declarations
of one entity with different contracts are an error, detected by step 5 above.
Modules are not addressed here and keep the current behaviour.

## Safety

These must be refused, and each gets a test:

- no interface for the callee -> unavailable, fail closed;
- interface present but the manifest differs in any field;
- the contract rebuilt here differs from the recorded one;
- the recorded status is `refused` or `not attempted`;
- a stale interface, older than the unit it describes;
- a truncated, malformed or hand-edited interface;
- a trusted assumption in the closure that this unit does not also admit;
- two interfaces claiming the same entity with different contracts;
- an interface produced by a different kernel or core version.

Adversarially: a forged interface must not be able to make a false contract
usable. It cannot make one *provable* — the kernel never sees it — but it can
make a caller *assume* a postcondition. That is the honest trust statement, and
it is why this lands in the artifact/reuse TCB and must be reported.

## Trust impact

Yes, and it is the point of the RFC to state it precisely.

This enlarges the **artifact and reuse TCB** (`TRUST.md` 2.5), which exists for
exactly this case. Added: the emitter, the validator, the manifest comparison,
and the file's integrity. Not added: anything in the logical TCB. No kernel
rule, axiom or term former changes, and no proposition becomes checkable that
was not checkable before.

`TRUST.md` gains rules in a new section for interface emission and validation,
and the trust report gains a line naming how many contracts were assumed from
imported interfaces rather than proved in this unit. A result that leans on an
imported interface is not a proof of its own unit alone, and must never be
reported indistinguishably from one: each contract it leans on is listed with
it, with the interface that recorded it (see "Trust" under "Implementation").

## Erasure

Unchanged. The interface is never compiled and never reaches the executable.

## Diagnostics

```text
error: the contract of 'clamp_to<4u>' was verified in another translation unit
       under assumptions this unit does not share
note:  imported from clamp.o.cppli
note:  this unit's 'Positive' is not the refinement that interface recorded
```

The rule is that a refusal names the field that differed. "Unavailable" is never
reported as "unverified body", because the two have different fixes.

## Alternatives considered

**Transport proof terms and re-check them.** Strongest option: it would keep
everything in the logical TCB. Rejected for this RFC only on scope — it needs a
serialization format for evidence and a story for definitional equality across
versions. This design is forward-compatible with it: the interface gains an
evidence field later, and validation gains a re-check step.

**Whole-program verification at link time.** Rejected: it defeats separate
compilation, and does not help a library shipped without sources.

**Trust the header's contract with no artifact.** Rejected outright — it is
exactly the "declaration alone proves the body" error Annex L.1 forbids.

**Put results in the object file.** Rejected: it perturbs a runtime artifact
with proof-only data and entangles verification with the linker.

## Drawbacks

The artifact/reuse TCB grows, and a build system must now track a second output
per unit and keep it in sync. A stale interface becomes a new failure mode —
mitigated by making staleness always refuse rather than mislead.

There is also a real risk of the manifest being *too* strict: an unrelated
configuration change invalidates everything and verification cost returns.
That is the correct direction for soundness, but it will need measurement.

## Testing strategy

- **Positive**: a two-unit program where the caller uses an imported contract;
  a three-unit chain; a specialization proved in one unit used in another.
- **Negative**: each bullet in Safety above, asserting the stated reason.
- **Adversarial**: a hand-edited interface claiming a false contract must not
  make the program verify silently — it must either refuse or be reported as
  assumed, never as proved.
- **Erasure**: object files identical whether or not an interface was consumed.
- **Property**: over a family of two-unit programs, a contract is usable across
  the boundary exactly when every manifest field agrees.

## Compatibility

No break. A build that emits no interfaces behaves exactly as today, because
absence means "unavailable" and the existing fail-closed path runs. Nothing
about accepted single-unit programs changes.

## Implementation

What was built, and every decision the design above left to it.

**Surface.** Placement is the build system's: `cppl --cppl-emit-interface=<file>`
writes the interface of the one unit a command compiles, and
`--cppl-import-interface=<file>`, repeatable, imports one. Nothing is written
beside an object implicitly, so a build that asks for nothing behaves exactly as
before. The interface is written only after the unit verified and its object was
produced, atomically, and a unit that fails removes an interface of an earlier
compile at that path.

**Format.** A versioned, line-oriented text (`compiler/artifact`): magic and
format version first, then configuration, the unit, its source files, one entry
per contract, and a SHA-256 checksum of every preceding byte. Every field is one
token with a single canonical escape; repeated items are in canonical order; the
reader accepts only exactly the text the writer produces for what it read, with
bounds on size, line length and counts, and reports why anything else is
refused. It is fuzzed (`tests/fuzz/interface.cpp`) with that round trip as its
oracle.

**Entry.** Clang's USR for the function; a statement identity; the contract as
the kernel prints it, for diagnostics only; `status proven`, the only status
written or read; `total` or `partial`; and what the proof rests on: trusted laws
by identity, name and location, models of code outside the program (the
standard-library models of RFC 0020) by identity and name, unsafe blocks by
location, runtime validation sites (RFC 0021) by location, the refinement they
enter and, shown only, their predicate, and imported contracts by symbol and
entry identity, transitively. Models were added in format version 2 and runtime
validation sites in version 3, and an interface of an earlier version is
refused, since it could not say whether a contract rested on one; the format
knows no model by name, so adding one changes no format, and a consumer refuses
a record naming a model it does not have, or a model under another name, which
no compiler of the same semantics writes. A function with internal
linkage is never recorded or matched: its USR can be spelled the same in two
units that mean two functions.

**Identity (step 4 and 5).** Rather than a manifest compared field by field,
the consumer rebuilds the contract statement from its own declaration and
compares a canonical statement identity with the recorded one. That identity
hashes parameter and result types and passing modes, every precondition
(refined parameters' predicates included), the postcondition (a refined result
and reference post-states included), memory capabilities and whether the
function is asked to terminate, as kernel terms, with every pure definition
they reach, and every one those reach, transitively, encoded by content in the
order first reached, so it does not depend on how either unit numbered its
definitions. Parameter names do not enter it. The measure that proves
termination does not either: `decreases` on a declaration asks for termination
(`SPEC.md` TERMINATION-006), and which measure establishes it matters only to
recursive calls, which never cross units (TUBOUND-008), so a producer may prove
the same total contract under another measure than a consumer's declaration
spells. The first implementation compared the measure too; the owner's review
separated it. Within one unit a measure is what recursion descends by, so
`TU-003` compares two `verified` declarations of one function with their
measures, through the same encoding.

A record is named by other records through an identity of the verification
result (`artifact::identify`): the callable, the statement identity, the
totality and, by identity, every trusted law, model, unsafe block and further
record it rests on. Its name, its description and the names and locations of
its assumptions are provenance and do not enter, so a dependent record survives
a rewording and never survives a different result (TUBOUND-009).

**Configuration and staleness (the manifest).** Compared exactly, and refused
naming the first difference: the compiler's release version; the verification
semantics, identified twice, by a version declared by hand
(`obligations::kVerificationSemanticsVersion`) and by the verifier semantics
digest the build computes from every source of the components that implement
verification (`cmake/VerifierSemanticsSources.cmake`, with every source under the
code roots classified as semantic or, with a reason, not); then kernel, formal
core, the Clang that resolves C++ semantics, language mode and target triple.
The SHA-256 of the compiler's executable is recorded for audit (`TRUST.md`
TCB-VERSION-004) and not compared: an interface is bound to a release and to what
a result means, not to the binary that wrote it, so two builds of one release
from the same semantic sources use each other's interfaces. The declared version
is the statement of compatibility, and keeping it honest is a reuse-TCB
obligation; the digest guards it mechanically, so a semantic change nobody
declared still invalidates the interfaces written before it (`TRUST.md`
TCB-XTU-008). The first implementation compared the executable's digest, which
refused every interface after any rebuild; a later revision stopped comparing
the release as well, which the reconciliation with the verification-semantics
design restored (`SPEC.md` TUBOUND-005). The
meaning-changing command-line options are recorded for audit and not compared:
what a contract means to the consumer is rebuilt from the consumer's own
preprocessing, and what the body means is fixed by the producer's own compile,
which those options drove. Staleness is by content: every file the producing
unit was preprocessed from is recorded with its digest and rehashed on import;
a timestamp is never consulted, and neither is the path the interface is read
from.

**Closure across units.** A record is usable only while every record it rests on
is imported with the identity it had when the dependent proof was made, so a
change anywhere in a chain invalidates what rests on it; two interfaces that
record different contracts for one function are an error naming both, and the
function's contract is unavailable. The trust report lists every imported
contract, and every claim resting on one with the interface and record, and
every trusted law, library model, unsafe block and runtime validation site the
other unit's proof rested on, each in its own category. An imported trusted law is not re-affirmed in the consuming unit: it is
carried and reported, which is the resolution of the open question below, and a
claim resting on it is never assumption-free.

**Distinct dimensions, not one.** An imported proven contract is an external
verified dependency, not a trusted law or a library model (`SPEC.md`
TUBOUND-006). A claim inherits the trusted laws, models, unsafe blocks and
runtime validation sites in the recorded closure of every contract it was proven
through, each in its own category, and is listed under those it inherits. It
also rests on the record itself, which is believed on the provenance of an
interface nothing authenticates, so it is never listed under `Assumption-free
claims`, however little the record rests on (TUBOUND-014). It is listed under
`Interface-dependent claims` with each contract it rests on, the interface and
the record's identity named, and the report states that interface provenance is
unauthenticated:

```text
Assumption-free claims:      0
...
Interface-dependent claims:  1
  contract of g (client.cpp:12), identity 7ea5fd84b9bd9ea0
    rests on the contract of f [c:@F@f#i#], imported from lib.cppli, entry ad6a6890cacbc39c, called in its own body
Interface provenance:        unauthenticated; 1 imported contracts are believed on the build's word (TRUST.md TCB-XTU-010)
```

A revision of this implementation listed a claim whose records rested on
nothing trusted as assumption-free, with its records beneath it. That read an
unauthenticated artifact as no assumption at all: a deliberately edited
interface whose checksum is recomputed could make a claim resting on an unproven
contract appear free of assumptions. The reconciliation with the
verification-semantics design reverted it to the rule the first implementation
kept, now stated as TUBOUND-014, and kept what the revision added: the closure
each record carries is complete, every category a claim can rest on, trusted
laws, models, unsafe blocks, runtime validation sites and further records,
written into the record, read back and written again by the next unit, and each
category mutation-checked on its own at each step (`TRUST.md` TCB-XTU-011), with
callees alike but for a trusted law, a model or an unsafe block used directly
and through a third unit (`tests/fixtures/cross_tu/closure*.cpp`). The listing
keeps the reliance on the artifact visible, which the trust split requires: the
interface's bytes are untrusted parser input, its provenance a trusted build
input of the reuse TCB, and the contract it records an external verified
dependency (`TRUST.md` 31.1).

**The production model.** What the consumer derives and what the producer
supplies are separate, and every step refuses rather than guesses:

```text
consumer: its own declaration -> its own contract meaning -> semantic contract identity
producer: entity identity, contract identity, verification-result identity,
          dependency closure, TOTAL or PARTIAL, trusted closure, unsafe closure

identities match?                                           no -> REFUSE
every recorded dependency imported with its identity?       no -> REFUSE
no cycle through a record in the dependency graph?          no -> REFUSE
the caller may use the contract, as an external verified dependency
```

**Totality and recursion.** A record is total or partial as recorded; totality
follows it through callers, and a function asking to terminate cannot rest on a
partial one. A record describing a function whose declaration states `decreases`
as partial is refused. Recursion across units is refused: honest builds cannot
produce it, since neither unit could verify first, but a record claiming it is
refused as well. The check is on a graph: every function of the consuming unit
with an edge to each function it calls, and every record reached, with an edge to
each contract its record says the proof rests on, whether a function of this
unit or a record of a third. A strongly connected component of that graph with a
record in it and a cycle (more than one member, or a record resting on itself)
crosses units, and a contract whose proof reaches one is refused, whether the
caller is in the cycle or only relies on it; a diamond, with no cycle, is used
(`tests/unit/cross_unit_contracts_test.cpp`). The first implementation followed
only the edges from a record back into this unit, which missed a cycle through a
third unit's record whose own record was not transitive.

**Templates.** An explicit specialization declared with its own contract is a
function like any other, keyed by its USR, and crosses; `f<4>`'s record never
serves `f<5>`. A specialization of a template a unit only declares is not
instantiated there, so no statement can be compared, and it is refused.

**Trust.** Unchanged logical TCB: no kernel rule, axiom or former. The artifact
and reuse TCB grows by the emitter, reader, validator and statement identity,
and the reporting TCB by the imported closure and its completeness; `TRUST.md`
31.1 states each. A hand-edited interface with a recomputed checksum is not
detected; everything resting on it is reported as resting on that record, with
the interface it came from, and never as assumption-free
(`tests/negative/cross_tu.sh`).

**Not done here.** Evidence transport; authentication of interfaces; binding the
object that is linked to the interface that was imported (`SPEC.md` L.5); import
in `cppl-lsp`; modules.

## Unresolved questions

- Whether evidence itself should eventually be transported, and in what form.
- Modules: whether a BMI should carry this directly instead of a sidecar.
- Whether interfaces should be authenticated, and how the object a build links
  is bound to the interface a consumer imported.

Resolved by the implementation: placement is the build system's, through
explicit options; an imported trusted law is carried and reported rather than
re-affirmed; the target triple and language mode participate, the remaining
command-line options are recorded and not compared.
