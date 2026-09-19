---
name: cppl-performance
description: Optimize C++L compiler or verifier performance. Use for parser, elaborator, VIR, normalization, proof search, solvers, caching, diagnostics, memory use, startup time, incremental builds, or native compilation performance.
---

# C++L Performance

Performance work must preserve semantics, proof status, trust, and determinism.

## Read first

- `AGENTS.md`
- `ARCHITECTURE.md`
- relevant performance tests/benchmarks

## Workflow

1. Measure the existing bottleneck.
2. Record a reproducible baseline.
3. Identify the responsible stage.
4. Optimize the authoritative implementation rather than adding semantic shortcuts.
5. Re-measure.
6. Run correctness, negative, soundness, and conformance tests.

## Preserve exactly

```text
proof result
verification status
trusted-assumption closure
runtime semantics
erasure semantics
cache correctness
```

Do not introduce:

- approximate proof checking;
- skipped obligations;
- incomplete invalidation;
- heuristic acceptance;
- hidden fallback;
- nondeterministic theorem results.

Be especially conservative when optimizing the kernel.
