---
name: cppl-soundness-review
description: Perform an adversarial C++L soundness review. Use for changes to proofs, types, equality, normalization, termination, solvers, erasure, unsafe boundaries, proof caching, or anything capable of affecting whether false propositions can be accepted.
---

# C++L Soundness Review

Treat the change as potentially adversarial.

## Read first

- `AGENTS.md`
- `SPEC.md`
- `TRUST.md`
- relevant implementation and tests

Read `FOUNDATIONS.md` when the change affects the formal calculus.

## Review

Answer concretely:

1. Can this make a false proposition `PROVEN`?
2. Can malformed proof evidence be accepted?
3. Can nontermination manufacture evidence?
4. Can definitional equality become too permissive?
5. Can substitution, rewriting, transport, or normalization become unsound?
6. Can `unsafe`, FFI, or memory corruption manufacture proof evidence?
7. Can a trusted assumption disappear from provenance?
8. Can a solver result bypass required checking?
9. Can stale cached evidence remain accepted?
10. Can proof erasure change runtime behavior?
11. Can unsupported C++ behavior accidentally become verified?
12. Can target-dependent behavior invalidate a proof?

## Test adversarially

Where applicable add rejection tests for:

- forged evidence
- false equality
- malformed terms
- divergent proof computation
- hidden assumptions
- stale artifacts
- unsafe laundering
- ghost leakage
- unsupported semantics

## Report

Conclude with:

```text
Soundness impact:
TCB impact:
New assumptions:
Runtime-model impact:
Required negative tests:
Blocking issues:
```

Unresolved soundness uncertainty is a blocking issue.
