# RFC 0001: Body-derived single-return contracts

> Surface syntax and formatting in this historical RFC are superseded by
> [RFC 0015](0015-canonical-language-surface.md) and the
> [normative grammar](../GRAMMAR.md). Semantic rationale remains applicable.

## Status

Implemented initial fragment.

## Decision

Implement the existing `verified`, `expects`, `ensures`, and `result` syntax for
namespace-scope integer functions whose bodies consist of one pure return
expression. The normative rule and supported boundary are in `SPEC.md` 12.5.

Clang resolves the executable body. C++L elaborates its return term `R`, then
generates `forall parameters. P -> Q[R/result]`, omitting the implication when
there is no precondition. The existing proof machinery produces evidence and
the existing kernel checks every obligation. Kernel rules added: 0. Logical
assumptions added: 0.

## Rationale

This connects formal claims to executable implementations without inventing a
body-summary axiom, control-flow semantics, or a second expression elaborator.
`result` exists only in analysis; erasure preserves the runtime signature and
body. Ordinary C++ callers retain their existing runtime behavior.

## Boundaries

Unsupported bodies fail closed. Precondition-bearing functions are unavailable
to verified reasoning until caller obligations are implemented. Explicit pure
definitions without preconditions retain the existing admission and termination
checks. Written `refl` does not gain automatic rewriting.

## Validation

Native positive tests cover identity, unsigned wrapping addition, conditional
contracts, multiple parameters, overloads, and pure helpers. Negative tests
cover false contracts, unsupported C++ bodies, precondition calls, and preserved
written-proof behavior. Unit tests check body-dependent obligation identities,
capture-safe substitution, malformed evidence, and erasure/source locations.

## Follow-up sequence

Conditionals and path obligations; locals and richer expressions; loops with
invariants; recursion with induction/termination; memory/reference reasoning;
automation and SMT.
