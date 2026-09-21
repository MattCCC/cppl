# RFC 0014: Storage, memory validity and effects

> Surface syntax and formatting in this historical RFC are superseded by
> [RFC 0015](0015-canonical-language-surface.md) and the
> [normative grammar](../GRAMMAR.md). Semantic rationale remains applicable.

## Status

Accepted, implementation in progress. Supersedes this RFC's first revision,
which proposed `readable(p)` / `writable(p)` as bare predicates over a pointer
value and deferred storage identity, regions, extent, projections and effects to
"unresolved questions". Those questions are the design, and they are settled
here.

The normative boundary is `SPEC.md` 12.10 (storage and access), with `SPEC.md`
12.9 retained for reference storage and post-state.

This RFC defines a **generic storage and effect model**. Refinement types
consume it. They do not define it, and nothing in this document is
refinement-specific.

## Summary

C++L models logical *values* well and logical *storage* not at all. The VIR's
entire node set is values plus local versions; there is no representation of an
lvalue, a member, an element, a pointee, a capture or a temporary. Every feature
that needs one — refined members, `*p`, `a[i]`, `p->m`, lambdas with reference
capture, returned references — is therefore blocked on the same absence, and
each has so far been refused separately.

This RFC introduces one model:

```text
Place       where a value lives          (identity, projection, aliasing)
Region      the object a place sits in   (lifetime, extent, provenance)
Capability  what may be done there       (readable, writable, initialized)
Version     which value is there now     (mutation, havoc, effects)
```

Access — read, write, dereference, subscript, member — is a single operation
over a `Place`, guarded by the `Capability` its `Region` supplies, producing or
consuming a `Version`. Dereference is not a special case; it is the projection
that crosses from a pointer *value* to a pointee *place*.

## Motivation

### The immediate blocker

`*p` is refused today. The tempting admission rule is a non-null precondition:

```cpp
verified int read(int* p)
    expects (p != nullptr)
{
    return *p;   // unsound to admit on this basis alone
}
```

This is wrong as a *sufficient* condition. C++ does not guarantee a non-null
pointer is dereferenceable. Validity also requires liveness, initialization (for
reads), sufficient extent, provenance and access permission. Admitting
dereference on non-null alone installs

```text
non-null  ⇒  valid dereference
```

as a global assumption the kernel never checks — exactly the hidden soundness
gap `AGENTS.md` 8 and 12 exist to prevent.

The fix cannot live in the pointer provider. `AGENTS.md` 38 requires a pointer
provider to state `null` and `non_null` and **never** lifetime, provenance,
dereferenceability, bounds, initialization, ownership or uniqueness. That rule
is correct and this RFC does not weaken it.

### Why a predicate over `p` is not enough either

The first revision proposed `readable(p)` as a predicate over the pointer value.
That is still too weak, for a reason worth stating precisely:

**Validity is not a property of a pointer value.** It is a property of the
*region* the pointer designates, in the *current state*. Two facts follow:

1. Two pointers with equal values designate the same region, so validity cannot
   be attached to the syntactic pointer expression without immediately needing
   the region anyway.
2. Validity changes without the pointer value changing. `free(p)` leaves `p`
   equal to itself and destroys its validity. A predicate over `p`'s *value*
   cannot express that, because nothing about the value changed.

So the model needs region identity and state-dependence from the start.
Retrofitting them onto value-predicates later would invalidate every proof
written against the earlier form.

### Why this is one model and not five

Refined members, subscripts, dereference, reference capture and returned
references are five surface features with one underlying requirement: *name a
storage location, know what may be done to it, know which value is in it, and
know when that knowledge goes stale.* Building them separately guarantees five
incompatible aliasing stories. `AGENTS.md` 27 forbids that, and the refinement
storage invariants already require one shared crossing path.

## Goals

- One representation of storage usable by every access form.
- Access obligations that are *checkable evidence*, not assumptions.
- Conservative, sound aliasing with no precision claims that are not earned.
- Keep the pointer provider at exactly `null` / `non_null`.
- Keep runtime behavior, layout and ABI untouched.
- Make every residual gap an explicit, reported trust event.
- Zero axioms. Zero representation-specific kernel rules.

## Non-goals

- Separation logic, ownership types or borrow checking.
- Alias *precision*. Where distinctness is unproved, invalidate.
- Inferring validity from any syntactic property of a pointer.
- Pointer arithmetic beyond subscript on a single region.
- Concurrency, data races or atomics.

---

# Part I — The model

## 1. Places

A **place** designates storage. It is a logical construct; it is never an
address and never a runtime value.

```text
Place ::= Local    local         one declared local's storage
        | Param    parameter     a by-reference parameter's referent
        | Field    place, index  a non-static data member of a place
        | Element  place, term   an element of an array-like place
        | Deref    pointer term  the pointee a pointer value designates
        | Temp     id            a materialized temporary
```

`Field` and `Element` are **projections**: they name storage *within* a place.
`Deref` is the one constructor that crosses from a value to a place, and it is
the only one requiring a capability to form.

Two design rules:

**A place is not a value.** Reading a place yields a value; the place itself is
never a term the kernel sees. This keeps the kernel's term language closed
(`Var`, `Literal`, `Call`, `Prim`, `Projection`) with no address-typed terms.

**A place is identified structurally, after Clang resolution.** `s.x` and
`s.x` are the same place; `s.x` and `s.y` are different places, because Clang
resolves them to different members of the same complete type. This is what
makes member disjointness *provable* rather than assumed — see §4.

## 2. Regions

A **region** is the object a place belongs to, and the unit that lifetime,
extent and provenance attach to.

```text
region_of(Local l)      = the local's own region
region_of(Param p)      = an opaque caller region, distinct per parameter only
                          where C++ guarantees it
region_of(Field q, i)   = region_of(q)          -- a member shares its object
region_of(Element q, t) = region_of(q)          -- an element shares its array
region_of(Deref v)      = the region v designates, which is opaque
region_of(Temp id)      = a fresh region, live for the full expression
```

A member and its object share a region: destroying the object destroys the
member. An element and its array share a region. This is what makes
`free(p); p->m` unsound *by construction* rather than by a special rule.

Regions carry:

```text
extent        how many elements, where known
live          whether the region's lifetime has begun and not ended
provenance    which allocation the region belongs to
```

Extent is a term, so `readable(p, n)` from the first revision is subsumed:
extent lives on the region, and a subscript obligation compares the index
against it. This answers the first revision's "sized or unsized" question — it
is sized, because unsized cannot express subscripting and would have to be
replaced.

## 3. Capabilities

A **capability** is what the current state permits at a place.

```text
readable(place)      the place may be read
writable(place)      the place may be written
initialized(place)   the place holds a value of its type
```

Entailments, which are definitional and not kernel rules:

```text
writable(q)     does not entail   readable(q)      -- write-only output buffers
readable(q)     entails           live(region_of(q))
initialized(q)  entails           readable(q)
p != nullptr    entails           nothing
readable(q)     entails           nothing about p != nullptr
```

The independence of nullness and validity is deliberate and load-bearing. It is
what the first revision got right and this revision preserves.

Reading requires `initialized`, not merely `readable`: reading a live but
uninitialized object is undefined behavior, and the distinction is exactly what
lets a constructor's member-initialization obligations be checked (§6).

## 4. Aliasing

Two places **may alias** unless C++L can show they do not. Disjointness is
proved, never assumed, and only from facts Clang resolves:

```text
distinct locals                  disjoint    (no two locals share storage)
distinct members of one object   disjoint    (C++ object model)
same array, indices i != j       disjoint    when i != j is proved
a local and any Deref            may alias   unless the local's address is
                                             never taken -- which Clang knows
any two Derefs                   may alias   always, absent further proof
different regions                disjoint    when provable
```

The last two lines are the conservative core. Two arbitrary dereferences may
alias, so a write through one invalidates facts about the other. This is
imprecise and deliberately so: `AGENTS.md` 22 and the refinement storage
invariants both require conservative invalidation over unproved distinction,
and `SPEC.md` 12.9 already applies exactly this rule to references.

Type-based disjointness (strict aliasing) is **not** used. It is valid C++
inference, but it depends on UB-freedom the program has not yet been shown to
have, and using it to justify a proof would make the proof circular.

## 5. Versions and havoc

Each place holds a **version**, as locals do today. `LocalVersion` generalizes
to `PlaceVersion`.

A write to place `q` with value `v`:

```text
1. prove   writable(q)
2. prove   membership(declared_type(q), v)      -- refinement crossing
3. bind    a new version of q, holding v
4. havoc   every place that may alias q
5. mark    initialized(q)
```

Step 4 is **havoc**: a may-aliasing place receives a fresh unconstrained
version. Its old facts do not carry over, and — critically — its *refinement*
does not re-establish itself. A fresh version of refined storage owes its
predicate again before any read may rely on it. This is already the rule for
references (`SPEC.md` 12.9) and is now the rule for all storage.

Step 2 before step 3 is the ordering that makes `S{-5}` impossible to admit.

## 6. Effects

A call may change storage. Which storage it may change is derived from
Clang-resolved parameter kinds and the callee's verification status:

```text
by value                        no effect on the caller's storage
const T& / const T*             no effect through that parameter
T& / T*                         may write the referent's region
verified callee                 exactly its stated effects, after its
                                contract and the call's entry obligations
                                are proven
unverified callee               may write every region reachable through
                                its non-const reference and pointer
                                parameters, and every region whose address
                                may have escaped
```

**An unverified call is never assumed pure.** This is `SPEC.md` 12.9's existing
rule generalized from references to regions.

Escape analysis is conservative: a region whose address is taken and passed
anywhere unverified is assumed to have escaped, permanently, and every
unverified call thereafter may write it.

A verified callee's postcondition may *re-establish* facts the effect
invalidated. That is the only way a fact survives a mutating call, and it
depends on the callee's own successful verification — never on a summary
asserted without proof.

## 7. Lifetime, temporaries, moves and returns

**Temporaries.** A materialized temporary gets a fresh region, live for its
full-expression, then dead. No runtime temporary is introduced that C++ did not
already create (`SPEC.md` 27 requirement, and `AGENTS.md` 38's "a binding is
never a new object").

**Moves.** A move from a scalar or trivially-copyable region follows Clang's
actual semantics — the source keeps its value. A move from a user-defined type
havocs the source region: its facts, including refinements, do not survive. The
moved-from state of a user-defined type is a library guarantee C++L has not
verified, so assuming anything about it would be an unearned fact.

**Returned references and pointers.** A returned `T&` or `T*` denotes storage
that already existed. The alias relationship is preserved where the callee's
contract states it, and where lifetime cannot be shown to outlive the call, the
return is **refused**. C++L does not infer lifetime; a returned reference to a
local is a diagnosable error, and a returned reference whose provenance is
unknown is refused rather than trusted.

## 8. Trusted boundaries

Where validity genuinely comes from outside the verified world — an external
API, an OS guarantee, a hardware mapping — a `trusted` boundary may introduce a
capability as a **recorded trust event**:

```text
proof unavailable
        ↓
explicit trusted boundary, with a source location and a stated reason
        ↓
capability introduced as trusted evidence
        ↓
named in the trust report, per region and per location
```

Three constraints:

- **Never the normal path.** Every dereference being a trust event would
  enlarge the audit surface, not shrink it.
- **Never implicit.** A failed capability obligation is a diagnostic, never a
  silent downgrade to an assumption (`AGENTS.md` 23).
- **Local and auditable.** Trust attaches to a specific region at a specific
  location, and the trust report names the capability, the place, the location
  and the mechanism.

This RFC **depends on `trusted` being implemented**. It is currently recognized
only in order to be refused (`compiler/frontend/src/recognizer.cpp:1177`). That
work is part of this RFC's implementation, not an assumed prerequisite.

---

# Part II — Representation and kernel impact

## 9. VIR representation

New nodes, added to `vir::Expr`'s variant:

```text
PlaceRef      place                    a read of the value at a place
PlaceVersion  place, value, body       the version a write establishes
Havoc         places, body             fresh versions after possible mutation
Capability    kind, place              a capability proposition
RegionExtent  place                    the extent term of a place's region
```

`LocalVersion` and `LocalRef` become the `Local` case of `PlaceVersion` and
`PlaceRef`. They are not kept as a parallel path — that would be the
"syntax-specific refinement subsystem" the storage invariants forbid.

The existing version-numbering discipline extends unchanged: versions are
unique within a body, and a value reads only versions numbered below its own.
That is what makes transitive resolution terminate, and it must keep holding for
places, or the route/provenance work already landed would break.

## 10. Kernel impact — the honest part

This is the part where the zero-delta goal meets an actual semantic requirement,
and I am not going to hide the tension.

The kernel's proposition language is closed:

```text
Proposition ::= Eq | Forall | Implies | And | Or
Term        ::= Var | Literal | Call | Prim | Projection
```

Every proposition bottoms out in `Eq` over terms, and a `Call` requires a
`Definition` with a concrete `Term body` — the context admits **no uninterpreted
symbols**. A refinement predicate works within this because it is a decidable
condition on a modeled value.

`readable(q)` is not. It is a property of the execution state, not a computable
function of any value the kernel holds. There is no `Term` whose evaluation
yields it.

So there are three options, and only one is sound:

**Option A — encode capabilities as terms.** Introduce a boolean-valued term for
each capability. *Rejected.* The kernel would evaluate it, and there is nothing
to evaluate; it would need an uninterpreted constant, which is a new term former
and a genuine kernel extension. It also invites `readable(p) == true` to be
proved by reflexivity, which would be catastrophic.

**Option B — add capability propositions to the kernel.** Extend `Proposition`
with a `Holds(capability, place)` former. *Rejected under `AGENTS.md` 38*: this
is a representation-specific kernel rule in all but name, and it puts memory
semantics inside the trusted kernel where they cannot be checked.

**Option C — capabilities are *hypotheses*, not propositions.** Adopted.

A capability is carried as a **context hypothesis** by the obligation layer, not
as a proposition the kernel reasons about. The obligation layer:

- tracks which capabilities hold at each program point;
- checks an access by looking up the required capability in that context;
- emits the *value* obligations (refinement membership, bounds) to the kernel
  as ordinary propositions.

The kernel therefore checks arithmetic, equality and refinement obligations
exactly as today. It never sees `readable`. Capability checking is a
**correspondence-layer decision**, discharged by a decidable context lookup, not
a logical derivation.

### What this costs, stated plainly

This places capability tracking in the trusted correspondence layer
(`TRUST.md` 41.2), alongside the existing rules that map C++ operations to core
terms. That is a **real TCB delta**, and I will not report it as zero.

```text
kernel rules added          0
axioms added                0
logical assumptions added   0
core/kernel version         unchanged (0.5.0)
correspondence rules added  1 family (capability tracking and access checking)
```

The delta is honest and is the minimum that expresses the requirement. A
capability-tracking defect can cause the kernel to check the wrong statement —
the same failure class as every other correspondence rule — but it cannot make
the kernel accept an invalid derivation.

Why this is the right trade rather than a convenient one: capability tracking is
a **decidable flow analysis**, not a logical inference. It has no search, no
induction and no quantifier reasoning. Putting a decidable analysis in the
kernel would grow the trusted core without making it check more; keeping it in
the layer that already maps C++ semantics to logic puts it where the rest of the
C++-to-logic correspondence already lives. Bounds and refinement obligations —
the parts that genuinely need *proof* — still go to the kernel.

### Why extent obligations do reach the kernel

A subscript's bound check is a proposition about values:

```text
index < extent(region)
```

Both sides are terms, so this is an ordinary obligation the kernel checks with
the existing linear-arithmetic rule. Bounds safety is therefore *proved*, not
tracked. Only the capability part is contextual.

## 11. Erasure, ABI and runtime

Nothing in this model is executable. Places, regions, capabilities and versions
erase completely. No runtime check, no tag, no metadata, no wrapper type, no
layout change. `sizeof`, `alignof` and object representation are untouched, and
erasure equivalence tests apply to every construct this RFC adds.

---

# Part III — Surface and consequences

## 12. Syntax

Capabilities appear as ordinary contract clauses, using contextual identifiers
(`SPEC.md` 2), so ordinary C++ using these spellings keeps its meaning:

```cpp
verified int read(int* p)
    expects (readable(*p))
{
    return *p;
}

verified void store(int* p, int x)
    expects (writable(*p))
    ensures (*p == x)
{
    *p = x;
}

verified int at(int* a, std::size_t n, std::size_t i)
    expects ((readable(a, n)) && (i < n))
{
    return a[i];
}
```

The capability names a **place** (`*p`), not a pointer value — this is the
model's central correction to the first revision. The sized form `readable(a, n)`
states the region's extent.

## 13. Refined members

Once places exist, refined members follow without a new mechanism:

```cpp
type Positive = int where (self > 0);

struct S {
    Positive x;
};
```

`s.x` is `Field(Local s, 0)`. Every write to it is a write to a place and goes
through the §5 sequence, so the refinement obligation is generated for
aggregate initialization, constructor member-init, copy/move construction and
assignment, compound assignment, increment, and writes through references,
pointers and subscripts — because those are all the same operation on a place.

```cpp
S s{-5};    // rejected: -5 does not satisfy Positive
```

remains rejected permanently, now for a structural reason rather than a blanket
refusal.

## 14. What must still be rejected

- `*p` without the capability in scope.
- `*p = e` with only `readable`.
- Any derivation of a capability from `p != nullptr`.
- Any derivation of a capability from a pointer's decomposition state.
- A capability surviving a call whose effects are unknown.
- An unverified declaration manufacturing a capability or a refinement.
- A subscript whose index is not proved within extent.
- A returned reference whose lifetime or provenance is not established.
- A refinement fact surviving a may-aliasing write.
- `S{-5}` and every other construction path violating a member refinement.

## 15. Diagnostics

Per `AGENTS.md` 34, a diagnostic must distinguish *unsupported*, *unproved* and
*disproved*, and must never claim a predicate is false when it is merely
unproved:

```text
cannot dereference 'p': 'readable(*p)' was not established
note: 'p != nullptr' does not imply that 'p' may be dereferenced

writing through 'p' requires 'writable(*p)'; only 'readable(*p)' is available

'a[i]' requires 'i < n'; that was not proved here

the refinement 'Positive' of 's.x' is not available after the call to 'f',
which may write that storage
note: a verified postcondition could re-establish it

validity of '*p' was introduced by a trusted boundary at file.cpp:12
```

## 16. Testing strategy

Positive: read and write under the right capability; capability carried through
branches and loop invariants; refined members through every construction path;
subscript within proved bounds.

Negative: every item in §14, each pinned to the specific reason it must produce.

Adversarial, per the "added proof power must never add a fact" invariant:
attempts to retain a stale refinement through a second reference, a pointer, a
returned reference, a lambda capture, a member alias, a subscript alias, a
function call and a casted alias. Each must remain sound.

Erasure: a capability- and member-heavy fixture must produce byte-identical
runtime behavior, and layout/ABI tests must show no change.

## 17. Sequence

```text
1.  Places, regions and versions in the VIR          -- foundation
2.  Generic read and write over places               -- one path, all forms
3.  Aliasing and havoc                               -- conservative
4.  Member places, refined members, construction     -- unblocks S{-5}
5.  Capabilities and the trusted boundary            -- needs `trusted`
6.  Dereference                                      -- needs 3 and 5
7.  Extent, subscript and bounds obligations         -- needs 6
8.  Call effects and escape                          -- needs 3
9.  Temporaries, moves, returned aliases             -- needs 1 and 7
```

Each step is independently testable, and each leaves the tree green. Steps 1-4
need no capability model at all, which is why refined members can land before
dereference.

## 18. Alternatives considered

**Non-null as sufficient.** Rejected: unsound, as shown in the motivation.

**Validity in the pointer provider.** Rejected: violates `AGENTS.md` 38 and
would let one provider invent lifetime and provenance facts.

**Predicates over pointer values** (this RFC's first revision). Rejected: cannot
express that `free(p)` destroys validity without changing `p`, and cannot
express extent. Superseded rather than extended, because proofs written against
it would not survive the correction.

**Per-site `trusted` for every dereference.** Rejected as the normal path:
makes every dereference a trust event and enlarges the audit surface.

**Separation logic.** Rejected for now: a large trusted-core change for
precision this project does not yet need. The place/region model is compatible
with a later separation-logic layer, which would sit above it.

**Type-based alias analysis.** Rejected: depends on UB-freedom not yet
established, making the resulting proof circular.

## 19. Drawbacks

Authors write capability clauses that ownership-based languages infer. This RFC
chooses explicitness over inference; inference can be layered on later without
changing the obligation model, because it would only *discharge* capabilities,
never redefine them.

Conservative aliasing will reject programs that are in fact correct. That is the
intended direction of error (`AGENTS.md` 22: false rejection is preferable to a
stale unsound fact).

## 20. Compatibility

No break. `readable` and `writable` are contextual, and ordinary C++ using those
identifiers keeps working. No supported C++ version is affected. Pointer values,
their comparisons, and proof-side case analysis over `null` and `non_null` are
untouched.

## 21. Document reconciliation

```text
SPEC.md        new 12.10 storage and access; 12.9 retained and cross-referenced;
               32 updated so pointers/references point at the model
FOUNDATIONS.md storage and capabilities placed against Hoare logic and
               weakest preconditions, where this model already belongs
TRUST.md       41.2 gains the capability-tracking correspondence rule;
               41.5 notes the delta; trust report gains trusted capabilities
ARCHITECTURE.md the place/region/capability layer and where it sits
AGENTS.md      memory invariants: capabilities are never inferred from
               nullness, aliasing is conservative, no provider states validity
```
