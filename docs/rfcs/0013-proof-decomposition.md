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

## Boundaries

`std::variant`, `std::optional`, `std::expected`, pointers and product
representations have **no provider**, and this is a limit of the formal core
rather than of the case engine. The core's terms range over machine integers
only. Stating that a variant holds alternative 1, that a pointer is null, or
that a struct has a given field needs values and observers the core cannot
express, so no sound provider for them can be written today. `AGENTS.md` 11 and
12 forbid inventing one by treating a pointer or a class as an integer.

Supporting them is foundational work on the value model - an RFC against
FOUNDATIONS.md and SPEC.md - after which each becomes one provider. Until then
they are refused at the provider boundary, by name, and their states are never
guessed. ROADMAP.md records the sequence.

Induction stays separate from finite case decomposition. A provider may later
expose states useful to induction, but supporting `cases` does not make a
representation inductive.

## Validation

Generic tests exercise arm matching, binder scope, nesting, goal propagation,
dependency checking, duplicate and missing arms, source mapping, erasure and
kernel-evidence corruption once, through one provider. Provider tests cover only
representation-specific state modeling. Source tests cover C++17/20/23, aliases,
negative enumerators, empty enums, nested arms, direct and Law proofs, forward
dependencies, residual binders under quantifiers, erased runtime equivalence,
and a newly added enumerator. Rejection tests cover false goals, missing
residual and named arms, wrong types, labels and evidence, wildcards, binder
escape and capture, self and mutual dependencies, malformed arms, written
failure without fallback, and representations with no provider. Unit tests
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
