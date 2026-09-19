---
name: cppl-solver-integration
description: Add or change C++L SAT/SMT/theorem automation. Use for Z3, cvc5, solver theories, certificates, proof reconstruction, solver timeouts, model extraction, solver caching, or direct-solver trust decisions.
---

# C++L Solver Integration

Solvers automate reasoning; they do not silently become theorem authorities.

## Read first

- `AGENTS.md`
- `TRUST.md`
- relevant `SPEC.md`
- solver integration code

## Define explicitly

```text
supported theories:
encoding:
machine arithmetic model:
certificate/evidence path:
directly trusted solver behavior:
timeout behavior:
unknown behavior:
version dependencies:
```

## Required rules

- timeout must not become success;
- `unknown` must not become success;
- solver crash must not become success;
- theory mismatch must fail closed;
- bitvectors must not silently become mathematical integers;
- solver models are counterexamples/evidence, not proof by themselves;
- certificates should be independently checked where practical.

If solver results are trusted directly, update `TRUST.md` and trust reporting.

Add adversarial encoding tests.
