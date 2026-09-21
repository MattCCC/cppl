# RFC 0014: Memory-validity obligations

## Status

Draft. Gates pointer dereference, which remains refused until this RFC is
accepted and implemented. The normative boundary will be `SPEC.md` 12.9.

## Summary

Dereferencing a pointer requires facts C++L does not have. This RFC introduces
`readable(p)` and `writable(p)` as ordinary proof obligations, discharged by the
existing proof system, so that `*p` can eventually be modeled without either
inventing memory facts inside the pointer representation provider or assuming
dereference validity globally.

## Motivation

`*p` is refused today. The obvious way to admit it is to require a non-null
precondition:

```cpp
verified int read(int* p)
    expects(p != nullptr)
{
    return *p;   // unsound to admit on this basis alone
}
```

This is wrong as a *sufficient* condition. C++ does not guarantee that a non-null
pointer may be dereferenced. Validity additionally requires:

```text
liveness
initialization      (for reads)
sufficient bounds
provenance
access permission
```

Admitting dereference on non-null alone silently installs

```text
non-null  ⇒  valid dereference
```

as a global assumption the kernel never checks. That is precisely the class of
hidden soundness gap `AGENTS.md` 8 and 12 exist to prevent.

The fix cannot live in the pointer provider either. `AGENTS.md` 38 requires that
a pointer provider state `null` and `non_null` and never lifetime, provenance,
dereferenceability, bounds, initialization, ownership, uniqueness or dynamic
type. That rule is correct and this RFC does not weaken it.

Dereference validity is therefore neither a decomposition fact nor a global
trust assumption. It is a separate obligation.

## Goals

- Give dereference a checkable precondition instead of an implicit assumption.
- Keep the pointer provider's state model at exactly `null` / `non_null`.
- Reuse the existing obligation, evidence and kernel machinery.
- Make any residual gap an explicit, reported trust event rather than silence.

## Non-goals

- A full separation-logic or ownership system.
- Pointer arithmetic, subscripting, `p->m`, `T**`, pointer locals, returned
  pointers. Those remain refused independently of this RFC.
- Aliasing precision. Alias reasoning stays type-based and conservative.
- Inferring validity from any syntactic property of a pointer.

## Architecture

```text
pointer decomposition
    -> proves p == nullptr or p != nullptr

memory-validity model
    -> proves readable(p) / writable(p)

dereference
    -> requires the appropriate validity fact
```

The three layers stay separate. Decomposition never states validity; validity
never states which state a pointer is in; dereference consumes both.

## Proposed syntax

Validity appears as an ordinary contract clause:

```cpp
verified int read(int* p)
    expects(p != nullptr)
    expects(readable(p))
{
    return *p;
}

verified void store(int* p, int x)
    expects(writable(p))
    ensures(*p == x)
{
    *p = x;
}
```

`readable` and `writable` are contextual specification predicates, following the
existing contextual-identifier rule (`SPEC.md` 2): an ordinary C++ program that
uses those spellings keeps its meaning.

`writable(p)` entails `readable(p)`. Neither entails `p != nullptr`, and
`p != nullptr` entails neither: they are independent facts, deliberately.

## Static semantics

A modeled dereference generates an obligation at the crossing:

```text
read  *p    requires  readable(p)
write *p    requires  writable(p)
```

The obligation is discharged by the ordinary proof system from the contract's
entry facts. No new kernel rule is introduced: these are ordinary predicates over
a modeled value, and implication between them is ordinary implication.

A write through `*p` participates unchanged in the existing storage-version and
alias-invalidation model (`SPEC.md` 12.9): the pointee is external storage, and a
write to it invalidates every other possible alias of the same modeled type.
Validity is a precondition of the access, not a fact about the value written, and
never substitutes for a refinement obligation.

Validity facts are not preserved across operations that may invalidate them.
A call that may free, reallocate, resize or end the lifetime of the referent must
invalidate validity facts exactly as it invalidates value facts. Where the effect
is unknown, invalidate.

## Runtime semantics

Nothing. `readable` and `writable` are proof-only and erase completely, like
every other contract clause. No runtime check, no tag, no metadata, no layout or
ABI change. `sizeof`, `alignof` and object representation are untouched.

## C++ interoperability

Clang stays authoritative for pointer types, conversions, qualification, lookup
and overload resolution. C++L attaches validity obligations around the
Clang-resolved access. Templates and constexpr are unaffected because the
predicates are ordinary specification clauses. FFI and unsafe code cannot forge
validity: an unverified declaration cannot establish `readable(p)` any more than
it can establish a refinement (`SPEC.md` 17).

## Safety

These must be rejected:

- `*p` with no `readable(p)` in scope.
- `*p = e` with only `readable(p)`, not `writable(p)`.
- Any attempt to derive validity from `p != nullptr`.
- Any attempt to derive validity from a pointer's decomposition state.
- Retaining validity across a call whose effects are unknown.
- An unverified function's declaration manufacturing validity.

## Trust impact

No new kernel rule, no new axiom, no new trusted mechanism, no value-model
expansion. The obligations are ordinary predicates checked by the existing
kernel.

`trusted`/`unsafe` is the escape hatch, not the normal path. Where validity comes
from outside the verified world — an external API contract, an OS guarantee — an
explicit trusted boundary may introduce `readable(p)` as a **recorded trust
event**:

```text
proof unavailable
        ↓
explicit trusted boundary
        ↓
readable(p) introduced as trusted evidence
        ↓
visible in the trust report
```

rather than every dereference being implicitly trusted. The trust report must
name the predicate, the pointer, the location and the mechanism.

## Erasure

Removed: the clauses and their obligations. Remaining: the ordinary C++ access.
Runtime behavior is preserved because nothing about validity was ever executable.

## Diagnostics

```text
cannot dereference 'p': 'readable(p)' was not established
note: 'p != nullptr' does not imply that 'p' may be dereferenced

writing through 'p' requires 'writable(p)'; only 'readable(p)' is available

validity of 'p' is no longer available after the call to 'f'
```

A diagnostic must not claim a pointer is invalid when validity is merely
unproved (`AGENTS.md` 34).

## Alternatives considered

**Non-null as sufficient.** Rejected: unsound, as shown above.

**Validity in the pointer provider.** Rejected: violates `AGENTS.md` 38 and
would let one provider invent lifetime and provenance facts.

**Per-site `trusted` for every dereference.** Rejected as the *normal* path: it
makes every dereference a trust event, which enlarges the audit surface rather
than shrinking it. Retained only as the escape hatch above.

**Sized predicates now** (`readable(p, sizeof(T))`, storage regions,
capabilities). Deferred, not rejected — see unresolved questions.

## Drawbacks

Authors must write validity clauses that other verified languages infer from an
ownership system. This RFC deliberately chooses explicitness over inference;
inference can be layered on later without changing the obligation model.

## Testing strategy

Positive: read and write under the right clause; validity carried through
branches and loop invariants.

Negative: each item under Safety above.

Adversarial: validity must not survive a possibly-invalidating call; a stale
validity fact must not be usable after mutation; an unverified declaration must
not manufacture validity; kernel corruption tests must still reject forged
evidence.

Erasure: a validity-heavy fixture must produce byte-identical runtime behavior.

## Compatibility

No break. `readable` and `writable` are contextual, and ordinary C++ using those
identifiers keeps working. No supported C++ version is affected. Pointer
*values* and existing proof reasoning (`cases p { null => … non_null => … }`,
`p != nullptr`) are untouched.

## Unresolved questions

- Should validity be sized (`readable(p, n)`) from the start, or unsized first?
- How does validity compose with arrays and subscripting once those are modeled?
- What exactly invalidates validity, beyond "unknown effect invalidates"?
- Should `writable` imply exclusive access, or is that a separate predicate?
- Is a storage-region or capability model the eventual target, and does that
  subsume these predicates or sit beneath them?

## Sequence

```text
1. Pointer provider states null/non-null only        (already true)
2. Introduce readable/writable obligations           (this RFC)
3. Dereference requires those obligations
4. trusted/unsafe can explicitly discharge gaps
5. Until 2 exists, dereference remains refused
```
