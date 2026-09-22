# RFC 0016: Indexed observation

## Status

Draft

## Summary

The formal core can observe a component of an abstract value only at a constant
position: `Projection` carries a `std::uint32_t`. This RFC adds one term former,
`Element(subject, index)`, which observes a component of a homogeneous indexed
value at a position that is itself a formal term. It is the term-indexed member
of the abstract-observation family `FOUNDATIONS.md` §44 already defines, not an
array feature: the core learns to select an observation by term, and says
nothing about C++ arrays.

The bound remains a separate obligation. `Element(a, i)` does not prove
`i < n`, and no rule derives one from the other.

## Motivation

`SPEC.md` E.7 requires that an array element be observable at an index proved
within the extent. Today a subscript verifies along two different routes:

```text
a[i] where a is tracked storage   ->  Place + SymbolicElement  ->  works
a[i] where a is an argument value ->  no representable term    ->  refused
```

The second route has no representation, so this program is refused although
`SPEC.md` admits it and the precondition supplies exactly the needed bound:

```cpp
template <unsigned N>
verified int get(const int (&a)[N], unsigned i)
    expects (i < N)
    ensures (result == result)
{
    return a[i];     // refused: "this subscript's array is not tracked storage"
}
```

`a[0]` in the same body verifies. Only the symbolic index is missing, and it is
missing because `Projection::index` is a `std::uint32_t` — a representation
limit, not a semantic decision. `docs/STATUS.md` records this as the blocker
that keeps refinement types at `PROTOTYPE`.

The two workarounds are both excluded by the language definition. Enumerating
`a[0] … a[N-1]` does not exist for dependent `N` and is combinatorial where it
does. Converting the parameter to `@Seq<T>` requires an explicit abstraction
relation (`FOUNDATIONS.md` §40) that no C++ array has.

## Goals

- One generic term former for observation at a term-valued index.
- The bound stays an independent obligation discharged by the kernel.
- One projection system: the existing storage route is unchanged.
- Identity of the index term shared with the bound obligation.

## Non-goals

- No array extensionality, and no `@Seq` correspondence.
- No new surface syntax: `a[i]` is already the C++ syntax for this.
- No injectivity, no `Element(a,i) == Element(a,j) => i == j`.
- No pointer indexing: `p[i]` through a capability stays on the storage route.
- No multidimensional or nested-extent modeling beyond what composition gives.

## Proposed syntax

None. This is a core term former, reached through the C++ subscript operator
that `SPEC.md` E.7 already governs.

## Static semantics

A new type former describes a homogeneous finite indexed domain:

```text
IndexedType(element, extent)
```

`extent` is a term-level natural number fixed at type formation. It is part of
type identity, so an array of 4 and an array of 8 are different formal types
and no value of one is a value of the other.

The typing rule:

```text
Gamma |- a : IndexedType(T, n)
Gamma |- i : I            I an integer type
-------------------------------------------
Gamma |- Element(a, i) : T
```

Bounds are deliberately **not** a premise of term formation. `FOUNDATIONS.md`
§45 already fixes this architecture: the formal model represents an observation
as total internally for typing, while the C++ fact obtained from it is usable
only under the premise that the runtime observer is defined. Making `i < n` a
typing premise would make the core term partial and put a proof inside type
formation, which the kernel's `type_of` cannot express and which contradicts
§45. The obligation layer supplies `i < n` at the C++ subscript, exactly where
it supplies it for the storage route today.

So the core rule is total, and the correspondence rule is conditional:

```text
the C++ expression a[i] denotes Element(a, i)
    only where   i < extent(a)   is proved
```

Equality interaction is congruence and nothing more:

```text
a == b, i == j   |-   Element(a, i) == Element(b, j)
```

The converse does not hold. Neither of these is admitted:

```text
Element(a,i) == Element(a,j)  =>  i == j            (no injectivity)
(forall i. Element(a,i) == Element(b,i))  =>  a == b (no extensionality)
```

Substitution is structural: substituting into `Element` substitutes into the
subject and the index, with the usual binder shifting. `Element` binds nothing.

Termination is unaffected: the former is not a recursor and admits no reduction
rule. `Element(a, 3)` does **not** reduce to a component of a literal
aggregate, because the core has no aggregate literal. Normalization treats
`Element` as an uninterpreted congruent function symbol, precisely as §44
specifies for `pi_k`.

Refinement interaction follows `FOUNDATIONS.md` §85: `Valid(C,o)` implies
validity of refinement-bearing elements, applied elementwise for arrays. An
element observation of a refined element type therefore carries that element's
predicate, and no new rule is needed.

## Runtime semantics

Nothing. `Element` is a proof term. The C++ subscript that corresponds to it
compiles exactly as C++ compiles it, and erasure removes the contract that
mentions it.

## C++ interoperability

- **Ordinary C++**: `a[i]` on an array of modeled element type.
- **Templates**: the point of the feature. `const T (&a)[N]` in a verified
  template gets `N` from the specialization, as a term. Per `TEMPLATE-001`,
  each specialization is checked separately, so `N` is a literal by the time
  the obligation is built — but the elaborator never depends on that, and a
  non-constant extent lowers the same way.
- **Overload resolution / ABI / constexpr / exceptions / FFI**: unaffected.
  No signature, mangling or evaluation changes.
- **Standard library**: none. `std::array`/`std::vector` are class types and
  need their own abstraction relations, which this RFC does not supply.

## Safety

These must be rejected, and are covered by tests:

- a subscript whose index has no proved bound;
- an index bounded by a wider extent than the array's;
- an index of a type whose conversion to the extent's type is not modeled;
- a write through an element observation (this RFC is read-only; writes stay
  on the storage route, which owns versioning);
- any attempt to derive `i < n` from the existence of `Element(a, i)`;
- any attempt to prove two arrays equal from equal observations.

## Trust impact

Yes — this enlarges the logical TCB, and `TRUST.md` is updated to say so.

Added to the logical TCB: typing, substitution, congruence and structural
identity of `Element`, plus the `IndexedType` well-formedness check. This is
the smallest increment that expresses the rule: one variant, one typing case,
one substitution case, no reduction rule, no decision procedure.

Added to the correspondence TCB: that the C++ expression `a[i]` denotes
`Element(formal_a, formal_i)` and that the extent used in the bound obligation
is the extent of that array type. The correspondence claim is the part that
could be wrong without the kernel noticing, so it is stated explicitly rather
than folded into the logical claim.

Not added: any axiom. `Element` is uninterpreted, so it admits no proposition
that congruence does not already justify. `FOUNDATIONS.md` Annex F.6
(abstract-observation type safety) extends to it unchanged.

## Erasure

Removed: the contract clauses and obligations mentioning `Element`. Remaining:
the C++ subscript. Runtime behavior is preserved because no runtime construct
is introduced — the erased program is the C++ program, byte-identical, which
the existing erasure tests already assert for templates.

## Diagnostics

The unbounded case keeps the wording the storage route established, so one
message covers both routes:

```text
error: this subscript's 'element index' is not proven to lie within the extent
```

The mismatched-type case reuses the existing extent-conversion refusal. The
refusal being removed is `"this subscript's array is not tracked storage of
this body"`, which becomes reachable only for genuinely unmodeled subjects.

## Alternatives considered

**Widen `Projection::index` to a term.** Rejected. `Projection(v,k)` selects
from a heterogeneous signature where the result *type* depends on `k`; a term
index would make its typing rule depend on a proof of constancy. Keeping the
two formers distinct keeps each typing rule total and decidable, which is why
the user-facing distinction (structural component vs. dynamically selected
observation) is also the right internal one.

**Model arrays as `@Seq<T>`.** Rejected: requires an explicit abstraction
relation (§40) that a C++ array does not have, and would make bounds a property
of the sequence rather than of the C++ type.

**Enumerate elements.** Rejected: impossible for dependent `N`, combinatorial
otherwise, and explicitly excluded by the language definition.

**Keep it outside the kernel.** Rejected: the obligation layer cannot express a
term whose type the kernel cannot check, and inventing evidence outside the
kernel is exactly what `TCB-CORE-002` forbids.

## Drawbacks

The TCB grows by one term former; every future core change must handle one more
variant. Array reasoning stays deliberately weak — without extensionality, two
arrays with equal elements are not provably equal, so some natural-looking
programs will still be refused. That is the conservative direction, and
extensionality can be added later under its own RFC if a real need appears.

## Testing strategy

- **Positive**: the motivating template; a non-template array reference; an
  element of refined type; an index derived arithmetically from a bounded one.
- **Negative**: unbounded index; index bounded by a wider extent; element write
  attempted through the value route; unmodeled element type.
- **Adversarial**: no injectivity, no extensionality, and `Element` supplying no
  bound — each written as a program that would verify if the rule leaked.
- **Soundness regressions**: the kernel suite gains malformed-term cases
  (wrong arity, non-indexed domain, index of non-integer type).
- **Property**: the existing extent x bound matrix in
  `tests/negative/template_identity.sh` is extended to the value route, so the
  invariant is checked across argument families rather than at one fixture.

## Compatibility

No break. This admits programs that were previously refused and changes no
accepted program's meaning. It affects no C++ version differently and requires
no ABI or erasure change.

## Unresolved questions

- Whether element *writes* through the value route should ever be admitted, or
  whether the storage route remains the only writer. This RFC takes the latter.
- Whether array extensionality is worth adding later, and under what evidence.
- Whether multidimensional arrays should compose `IndexedType` directly or get
  a nested-extent rule of their own.
