# Contributing to C++L

C++L is a programming language and proof system.

Changes to C++L therefore have a higher correctness bar than ordinary application changes.

A compiler bug may be inconvenient.

A proof-kernel bug may allow a false theorem to be accepted as true.

Contributions should reflect that distinction.

---

# Principles

Contributions should preserve these project principles:

1. C++L remains a genuine C++ superset.
2. Laws mean formal specifications, not tests.
3. Proof validity is independent of AI confidence.
4. Unsafe behavior is explicit.
5. Trusted assumptions are explicit.
6. Proof-only information is erasable.
7. Runtime semantics match the semantics used by proofs.
8. Solver automation does not silently define truth.
9. Existing C++ interoperability is preserved where sound.
10. Soundness is more important than convenience.

---

# Repository areas

Changes generally fall into one of these categories:

```text
frontend/
clang/
elaborator/
type_system/
logic/
normalization/
termination/
proof/
vir/
automation/
safety/
erase/
diagnostics/
stdl/
tools/
tests/
docs/
```

Kernel-related changes receive the highest scrutiny.

---

# Language changes

New syntax or semantic behavior must not be introduced casually.

Substantial language changes require an RFC.

Examples:

- new keywords
- new proof rules
- new equality semantics
- new type constructors
- new unsafe capabilities
- changes to erasure
- changes to memory semantics
- changes to trusted boundaries

See:

```text
docs/rfcs/README.md
```

---

# Normative documentation

Changes that alter language semantics must update `SPEC.md`.

Changes that alter trust boundaries must update `TRUST.md`.

Changes that alter mathematical foundations must update `FOUNDATIONS.md`.

Changes that alter project direction should update `ROADMAP.md`.

Documentation must not describe semantics that the implementation does not actually provide without clearly marking them as planned.

---

# Proof-kernel changes

Any change under the trusted proof kernel must include:

- rationale;
- formal rule being implemented or changed;
- positive tests;
- negative tests;
- malformed-input tests;
- regression tests;
- explanation of soundness impact.

Kernel code should remain deliberately simple.

Avoid clever abstractions that make the trusted logic harder to audit.

---

# Negative tests

Proof systems require strong negative tests.

Every proof feature should test both:

```text
things that must be accepted
```

and:

```text
things that must be rejected
```

Examples of essential negative cases:

- false equality
- invalid induction
- forged proof value
- nonterminating proof
- ghost-to-runtime leakage
- unsafe-to-proof laundering
- invalid refinement
- invalid pointer proof
- stale cached proof
- hidden trusted assumption

A feature is not complete if only successful examples are tested.

---

# Regression tests

Every soundness bug must gain a permanent regression test.

Prefer tests that capture the underlying invariant rather than only the exact syntax that exposed the bug.

---

# Trusted computing base

Changes that enlarge the TCB require explicit justification.

A pull request that changes:

```text
untrusted
→
trusted
```

must state why independent checking is impractical.

Reducing the TCB is strongly encouraged.

---

# Solvers

Solver integrations must clearly specify whether solver output is:

```text
certificate-checked
```

or:

```text
trusted
```

Never silently change a solver from an automation dependency into a trusted theorem authority.

---

# Unsafe features

A proposed unsafe capability must define:

- what guarantee is suspended;
- how the unsafe region is represented;
- what values may leave the region;
- how verified code may consume those values;
- whether trust reporting changes.

Unsafe must remain visible.

---

# Performance changes

Verification performance matters.

However, optimizations must not alter theorem meaning.

Proof caching must include all relevant semantic inputs in invalidation.

Never trade soundness for compilation speed.

---

# C++ compatibility

Changes affecting C++ compatibility should specify:

- affected C++ standards;
- affected Clang behavior;
- ABI impact;
- template impact;
- constexpr impact;
- standard-library impact;
- whether ordinary C++ source remains valid C++L.

---

# Coding style

Prefer:

- explicit invariants
- deterministic behavior
- immutable intermediate representations where practical
- narrow interfaces
- small trusted modules
- exhaustive enums
- typed state transitions
- reproducible tests

Avoid:

- hidden global state
- nondeterministic proof results
- implicit trust
- ambiguous error recovery inside the proof kernel
- silent fallback from verified to unverified behavior

---

# Diagnostics

New language features should include useful diagnostics.

A verification error should answer:

```text
What was being proven?
What was known?
What remains to prove?
Where did the obligation originate?
What assumption failed?
```

Where possible include a counterexample.

---

# Pull requests

A pull request should include:

## Problem

What problem is being solved?

## Semantics

Does this change language meaning?

## Trust impact

Does this change the trusted computing base?

## C++ compatibility

Does this affect source, ABI, or standard-version compatibility?

## Tests

What positive and negative cases were added?

## Documentation

Which normative documents changed?

---

# Commit discipline

Prefer commits that each preserve:

```text
buildable
testable
explainable
```

state.

Do not mix unrelated proof-kernel and UI/tooling changes in one commit.

---

# Security issues

Do not publicly disclose suspected soundness vulnerabilities before maintainers have had a reasonable opportunity to investigate.

Follow `SECURITY.md`.

---

# Contribution standard

The standard for a language feature is not:

> It works on the example.

The standard is:

> Its semantics are defined, valid programs are accepted, invalid programs are rejected, trust impact is understood, C++ runtime behavior agrees with the proof model, and the result is covered by adversarial tests.
