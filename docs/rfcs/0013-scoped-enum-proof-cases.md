# Proof-only case reasoning over scoped enums

Status: implemented prototype of the semantics in SPEC.md 20 and RFC 0005.
The implementation boundary is SPEC.md 20.5.

## Scope and rationale

This vertical capability reasons about existing ordinary scoped enums. Clang owns
lookup, enum identity, constant evaluation and target-dependent underlying types.
It adds no `data`, runtime `match`, second type system, or native representation.

A scoped enum has a fixed underlying type whose entire value set is available to
it, including unnamed values ([C++ draft, dcl.enum](https://eel.is/c++draft/dcl.enum)).
Modeling only the enumerator list would prove false claims such as every `E`
being `E::a` for `enum class E { a };`. The residual arm is therefore explicit.
Aliased enumerators name one case. New distinct enumerators require new arms.

## Evidence construction

For each distinct enumerator value `c`, in declaration order, split on `s == c`.
Use the existing conditional-elimination rule with both value branches `s` and a
constant motive equal to the enclosing goal. Its true premise is `s == c`; its
false premise is `s != c`. The kernel derives and checks both premises itself.

In the residual branch, combine the false premises using conjunction introduction.
The underlying-value binder is an alias of `s`, resolved in analysis-only C++
probes and remapped before core lowering. Each arm proves the original goal;
`assume` can name a supplied fact but cannot assert a new one. Nesting preserves
value binding, hypothesis indices and source locations. Every nested reference
participates in proof dependency checking, so recursion cannot hide in an arm.

No new logical rule is needed. Logical assumptions: 0. Axioms: 0. Trusted
mechanisms: 0. The existing correspondence TCB gains the enum/type/alias mapping
and exhaustiveness checks documented in TRUST.md 41.6.

## Boundaries

This prototype requires every named case and a residual arm to be written. It
does not omit a case through guessed impossibility. Bool-backed or unscoped enums,
missing enum definitions, high unsigned enumerators beyond the existing literal
representation, compound subjects, variants, optionals, product decomposition and
pointers are refused. Supporting those representations requires their formal
runtime models; they do not become enums merely by having arm syntax. Induction
and termination are independent later capabilities.

## Validation

Source tests cover C++17/20/23, aliases, negative enumerators, empty enums, nested
arms, direct and Law proofs, forward dependencies, residual binders under
quantifiers, erased runtime equivalence, and a newly added enumerator. Rejection
tests cover false goals, missing residual/named arms, wrong types/labels/evidence,
wildcards, binder escape/capture, self/mutual dependencies, malformed arms, written
failure without fallback, and unsupported representations. Unit tests corrupt
VIR partitions and generated kernel evidence; the existing kernel remains the
final authority. No test result is treated as proof of soundness.
