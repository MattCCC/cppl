---
name: cppl-new-proof-feature
description: Implement a new C++L proof-system capability after its semantics are specified. Use for new proof constructors, tactics with kernel evidence, induction support, quantified reasoning, rewriting, refinement discharge, theorem composition, or new proof syntax.
---

# C++L New Proof Feature

Implement proof ergonomics without weakening the kernel.

## Read first

- `AGENTS.md`
- relevant `docs/SPEC.md`
- `docs/FOUNDATIONS.md`
- `docs/TRUST.md`
- accepted RFC if required

## Workflow

1. Identify the proposition/type being proved.
2. Identify the kernel rules that justify the feature.
3. Decide whether the feature belongs in:
   - syntax;
   - elaboration;
   - tactic/automation;
   - kernel.
4. Keep derived conveniences outside the kernel where possible.
5. Generate explicit evidence that the kernel can check.
6. Add successful proofs.
7. Add proofs that must fail.
8. Add malformed evidence tests.
9. Check termination and equality interactions.
10. Update `docs/STATUS.md`.

A new tactic is not automatically a new trusted rule.

Prefer derived proof machinery over expanding the kernel.
