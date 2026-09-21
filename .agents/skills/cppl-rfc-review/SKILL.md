---
name: cppl-rfc-review
description: Review a proposed C++L RFC before acceptance. Use for language, proof-system, core-calculus, memory-model, trust-model, ABI, erasure, concurrency, or other substantial design proposals under docs/rfcs/.
---

# C++L RFC Review

Review the design before optimizing the implementation.

## Read first

- `AGENTS.md`
- the RFC
- relevant `docs/SPEC.md`
- `docs/DESIGN.md`
- `docs/TRUST.md`
- `docs/COMPATIBILITY.md`
- `docs/ARCHITECTURE.md`

## Require the RFC to answer

```text
What problem is being solved?
What semantics change?
Why is the feature necessary?
What alternatives were considered?
How does ordinary C++ behave?
Does C++ source compatibility change?
Does the formal core change?
Does the TCB change?
Does erasure change?
What is runtime behavior?
What is unsupported?
How is the feature tested negatively?
How can the design fail?
```

## Review principle

Reject designs that depend on:

- hidden axioms;
- duplicated semantic authorities;
- unspecified fallback;
- semantic ambiguity;
- temporary architecture presented as final;
- proof claims stronger than the modeled runtime semantics.

Acceptance of an RFC does not mean implementation is complete.
