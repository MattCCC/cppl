---
name: cppl-kernel-change
description: Modify or review the C++L trusted proof kernel. Use whenever kernel inference rules, proof checking, conversion, primitive propositions, trusted constructors, or other kernel-authoritative behavior changes.
---

# C++L Kernel Change

Kernel changes receive the highest review standard.

## Read first

- `AGENTS.md`
- `SPEC.md`
- `FOUNDATIONS.md`
- `TRUST.md`
- relevant formalization
- relevant kernel tests

## Before coding

Identify:

```text
formal rule:
current kernel behavior:
required kernel behavior:
why the change belongs in the kernel:
```

If the behavior can safely remain outside the TCB, keep it outside.

## Implementation rules

Prefer:

- explicit rule application
- exhaustive variants
- immutable data
- deterministic behavior
- narrow interfaces
- minimal dependencies
- direct, auditable code

Avoid:

- heuristics
- recovery
- hidden state
- solver dependencies
- frontend-specific assumptions
- implicit proof construction
- clever metaprogramming
- unrelated refactoring

## Required tests

Every semantic kernel change needs:

1. valid evidence that must be accepted;
2. invalid evidence that must be rejected;
3. malformed evidence;
4. relevant boundary cases;
5. a regression test if fixing a defect.

Run `cppl-soundness-review` mentally or as a composed skill.

Update `TRUST.md` if the TCB or trusted rule set changes.
