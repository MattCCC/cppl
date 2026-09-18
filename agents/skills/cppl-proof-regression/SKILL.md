---
name: cppl-proof-regression
description: Add regression coverage for a C++L verification or soundness defect. Use when a false theorem was accepted, a valid proof was incorrectly rejected, trust provenance was lost, erasure changed behavior, caching reused invalid evidence, or another proof invariant failed.
---

# C++L Proof Regression

Every soundness defect becomes permanent adversarial coverage.

## Workflow

1. Record the violated invariant.
2. Reduce the original failure to the smallest useful reproducer.
3. Separate the semantic bug from unrelated parser or diagnostic behavior.
4. Add a regression at the lowest authoritative layer that exposed the defect.
5. Add an integration regression when the defect crossed component boundaries.
6. Test the opposite case when meaningful.

Prefer:

```text
small invariant-level test
```

over only preserving:

```text
large accidental reproducer
```

## Required form

For an unsound acceptance:

```text
before: invalid proof was accepted
after:  invalid proof is rejected
```

For an incorrect rejection:

```text
before: valid proof was rejected
after:  valid proof is accepted
```

## Never

- delete the regression because internals change;
- rewrite the Law merely to eliminate the failing case;
- assert only a diagnostic string when proof status is the real invariant;
- treat a formerly unsound behavior as compatibility behavior.
