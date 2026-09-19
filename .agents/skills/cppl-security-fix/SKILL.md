---
name: cppl-security-fix
description: Handle a C++L soundness or security-sensitive defect. Use for false theorem acceptance, proof forgery, trust laundering, unsafe-to-proven escalation, malformed proof acceptance, cache poisoning, erasure unsoundness, or verifier vulnerabilities.
---

# C++L Security and Soundness Fix

Soundness vulnerabilities have priority over feature work.

## Read first

- `AGENTS.md`
- `SECURITY.md`
- `TRUST.md`
- relevant `SPEC.md`
- affected implementation

## Workflow

1. Preserve a minimal reproducer.
2. Identify the violated invariant.
3. Determine whether false proof acceptance is possible.
4. Determine affected versions/components.
5. Determine whether the TCB is involved.
6. Fix the authoritative cause, not merely the reproducer.
7. Add permanent regression coverage.
8. Run `cppl-soundness-review`.
9. Update trust/specification documentation if required.
10. Avoid publishing exploit details prematurely when disclosure policy requires coordination.

## Never

- weaken the theorem;
- hide the failure behind diagnostics;
- convert the path to `TRUSTED`;
- special-case only the reported input;
- delete the regression after refactoring.
