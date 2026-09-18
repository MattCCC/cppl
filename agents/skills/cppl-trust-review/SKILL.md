---
name: cppl-trust-review
description: Review C++L trust impact. Use when changing the proof kernel, solvers, axioms, FFI, standard-library models, compiler assumptions, erasure, caching, plugins, intrinsics, or anything that may enlarge or alter the Trusted Computing Base.
---

# C++L Trust Review

Determine exactly what must be believed after the change.

## Read first

- `AGENTS.md`
- `TRUST.md`
- relevant `SPEC.md` sections

## Questions

1. Does the TCB grow or shrink?
2. Is a new axiom or trusted assumption introduced?
3. Could the assumption instead be independently checked?
4. Does the assumption propagate to dependent Laws?
5. Is provenance preserved?
6. Will trust reporting expose it?
7. Does direct solver trust change?
8. Does backend or erasure trust change?
9. Does FFI trust change?
10. Does proof-cache correctness become trusted?

## Required output

```text
TCB before:
TCB after:
Added trusted components:
Removed trusted components:
Added assumptions:
How assumptions are surfaced:
Can trust be reduced further:
TRUST.md update required: yes/no
```

Never expand trust silently.
