---
name: cppl-formal-core-change
description: Modify the C++L formal core calculus or meta-theoretic foundations. Use for propositions, dependent products, equality, universes, inductive types, eliminators, normalization, substitution, reduction, definitional equality, or fundamental proof-theoretic rules.
---

# C++L Formal Core Change

Changes to the formal core can alter the meaning of every proof.

## Read first

- `AGENTS.md`
- `docs/SPEC.md`
- `docs/FOUNDATIONS.md`
- `docs/TRUST.md`
- formalization/proofs if present
- relevant RFCs

## Define before implementation

```text
new syntax or judgment:
typing rule:
reduction rule:
equality interaction:
substitution behavior:
termination/normalization impact:
consistency impact:
kernel impact:
```

## Required review

Check:

- subject reduction/preservation implications;
- substitution;
- normalization;
- positivity where relevant;
- universe consistency where relevant;
- induction/elimination restrictions;
- interaction with recursive definitions;
- erasure behavior;
- whether new axioms are introduced.

Substantial changes require an RFC and `cppl-soundness-review`.

Never modify the calculus merely to make an implementation proof easier.
