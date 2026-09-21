# Generic proof decomposition

Status: implemented. Supersedes the scoped-enum-only framing of this RFC's first
revision. The normative boundary is SPEC.md 20.5.

Scoped enumerations were the **first vertical implementation** of proof-side
`cases`. They are no longer the architecture. The production architecture is
generic proof decomposition, and a scoped enum is its first provider.

## Separation

```text
ordinary C++ representation semantics
            from
generic proof decomposition and case reasoning
```

Clang stays authoritative for parsing, lookup, types, templates, constant
evaluation, layout, value categories, aliases, standard-library types and every
other ordinary C++ semantic. C++L owns only verification-level decomposition,
proof-state splitting, binders, hypotheses, exhaustiveness, evidence
construction, dependency checking and kernel lowering.

```text
C++ value/type
    ↓
Clang-resolved semantic information
    ↓
C++L representation provider
    ↓
ProofDecomposition
    ├── SumDecomposition
    └── ProductDecomposition
    ↓
generic proof reasoning
    ↓
existing VIR / core / kernel
```

```text
representation provider
    = knows the sound logical state model of one C++ representation family

generic case engine
    = performs proof splitting, binding, exhaustiveness, evidence and lowering
```

Adding a C++ representation to proof-side case reasoning means implementing one
sound provider and its semantic tests. It does not mean another case language,
another proof engine, another lowering path, or another editor subsystem.

## What the engine owns

Subject analysis, arm parsing, arm labels, binder declaration and scope,
`assume`, hypothesis introduction, goal propagation, nested `cases`, nested
binder capture, source locations, proof dependency tracking, self and mutual
recursion rejection, duplicate arm detection, missing arm detection,
exhaustiveness checking, arm result checking, evidence construction, VIR
lowering, kernel lowering, erasure and diagnostics. No provider reimplements any
of it.

## What a provider supplies

An ordered list of cases, each with a **discriminator** - a modeled Boolean
condition on the subject that holds in exactly that case - and its bindings; an
exhaustiveness model; and, when the model requires one, a residual label. A
provider never supplies the residual discriminator: the engine derives it as the
conjunction of the negated discriminators, so a provider cannot widen the
residual state.

Provider selection is by Clang-resolved semantic identity. Aliases, qualified
names and template specializations that resolve to one declaration select one
provider; a matching unqualified spelling never does.

## Evidence

Splitting on a discriminator is the existing conditional-elimination rule: its
true premise is the discriminator, its false premise the negation, and the
kernel derives and checks both itself. Splitting on each in turn leaves one
branch in which all are false, which conjunction introduction combines into the
tail case's fact.

No new logical rule is needed, for this or for any future provider whose states
are distinguished by decidable conditions on modeled values.

```text
representation-specific frontend knowledge
        ↓
ordinary logical propositions / evidence
        ↓
small generic trusted kernel
```

Logical assumptions: 0. Axioms: 0. Trusted mechanisms: 0. The existing
correspondence TCB gains the per-provider state mapping documented in TRUST.md
41.6.

## Scoped enumerations, as a provider

A scoped enum's state space is its fixed underlying type's entire value set, not
its enumerator list ([dcl.enum]). Modeling only the enumerators would prove false
claims such as every `E` being `E::a` for `enum class E { a };`, so the residual
case `unnamed` is explicit. Aliased enumerators name one case. Distinct values
produce distinct cases. New distinct enumerators require new arms.

## Tagged sums, as providers

`std::variant`, `std::optional` and `std::expected` share one shape: a
discriminating observation and one payload observation per state. They are
therefore one mechanism, not three. A variant's states are its alternative
**indices** plus `valueless`, so repeated and aliased alternative types stay
distinct and `valueless` is never omitted. An optional's are `some` and `none`,
with the payload bound only in `some`. An expected's are `value` and `error`,
each with its own payload. Only the public semantics of these types are modeled;
no standard library's layout is read, and no `std::get`, `std::visit`,
`.index()`, `has_value()` or dereference is emitted or relied on.

## Pointers, as a provider

A pointer decomposes into `null` and `non_null` and into nothing else. The
provider states no lifetime, provenance, dereferenceability, bounds,
initialization, ownership, uniqueness or dynamic type. `non_null` binds nothing:
a non-null pointer is not an assertion that a live, initialized object exists.

## Products, as a provider

Records, `std::pair`, `std::tuple`, `std::array` and built-in arrays all project
onto their components in order. A product has one state, so `decompose` is not a
case split and generates no discriminator; it is spelled with its own keyword for
that reason. Each binding is a logical projection onto the existing subobject, so
no structured binding, copy, move or temporary is created. Component order,
types, access and array extents come from Clang. A component Clang reports as
inaccessible is refused by name rather than projected.

## Composition

Nesting is generic. An arm binder is an ordinary proof expression, so
decomposing it selects a provider the same way the outer subject did. No pairwise
handler exists for `variant<optional<T>, E>` or `optional<pair<A, B>>`, and none
is needed.

Induction stays separate from finite case decomposition. A provider may later
expose states useful to induction, but supporting `cases` does not make a
representation inductive.

## Remaining boundaries

A representation with no provider is refused at the provider boundary, by name,
and its states are never guessed. Arm syntax does not make a class a sum.

`cases` and `decompose` are proof statements and appear only in proof bodies,
which contain no assignment, call, construction or destruction. No case fact can
go stale, and none can escape: a subject another object can write has reference
type, and a reference type has no formal meaning, so no law or contract states a
proposition about it. Mutation and aliasing are excluded structurally rather
than by analysis. Admitting `cases` over values that can change is future work
and is governed by SPEC.md 20.5.2.

## Validation

Generic tests exercise arm matching, binder scope, nesting, goal propagation,
dependency checking, duplicate and missing arms, source mapping, erasure and
kernel-evidence corruption once, through one provider. Provider tests cover only
representation-specific state modeling. Source tests cover C++17/20/23, aliases,
negative enumerators, empty enums, nested arms, direct and Law proofs, forward
dependencies, residual binders under quantifiers, erased runtime equivalence,
and a newly added enumerator. Every provider is exercised end to end, including
repeated and aliased variant alternatives, cv-qualified and reference subjects,
template-dependent payloads, all five product forms, and cross-provider nesting
in both directions; `std::expected` is gated on the C++23 library. Rejection
tests cover false goals, missing residual and named arms, an omitted
`valueless`, an out-of-range and a duplicated alternative, a payload bound in a
stateless arm, a pointee bound through `non_null`, an inaccessible member, a
product written as a sum and a sum written as a product, a type spelled like a
standard one, wrong types, labels and evidence, wildcards, binder escape and
capture, self and mutual dependencies, malformed arms, written failure without
fallback, and representations with no provider. Adding an alternative or a
product field invalidates a previously exhaustive proof. Unit tests
corrupt VIR partitions and generated kernel evidence; the kernel remains the
final authority. No test result is treated as proof of soundness.

## Abstract observation signature

The generalized value model admits `V(identity; T0, ..., Tn)` and checked
`project<i>(v) : Ti`, as specified in SPEC.md 20.5. It needs no new inference
rule: equality, substitution and conditional elimination already apply to the
resulting typed propositions. A signature is part of identity, so changing a
component invalidates old evidence. Observation normalization never unfolds C++
code or assumes a constructor or payload value. The core/kernel version is
0.6.0; the typing and normalization TCB grows, without axioms or logical
assumptions. This removes the scalar-only core restriction; provider and source
integration status is stated separately from availability of this core model.
