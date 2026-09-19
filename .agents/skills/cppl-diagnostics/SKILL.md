---
name: cppl-diagnostics
description: Improve C++L compiler and proof diagnostics. Use for failed obligations, source mapping, proof contexts, counterexamples, trust provenance, error explanations, diagnostic formatting, or machine-readable verification errors.
---

# C++L Diagnostics

Diagnostics explain proof state; they do not change proof semantics.

## Read first

- `AGENTS.md`
- relevant `SPEC.md`
- diagnostic architecture
- affected proof/VIR representation

## Prefer diagnostics containing

- source location
- failed goal
- relevant local context
- relevant hypotheses
- provenance
- trusted dependencies
- failed proof step
- counterexample when valid
- actionable next location

## Preserve distinctions

```text
disproven
unresolved
unsupported
unsafe
trusted
solver timeout
internal error
```

Do not collapse them into `verification failed`.

## Critical rule

```text
counterexample found
→ may disprove claim

no counterexample found
→ does not prove claim
```

Do not loosen proof checking to improve diagnostics.

Machine-readable diagnostics should preserve stable semantic categories independently from human wording.
