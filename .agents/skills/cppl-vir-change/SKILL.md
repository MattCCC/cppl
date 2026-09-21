---
name: cppl-vir-change
description: Modify the C++L Verification IR. Use when changing formal lowering from source/Clang semantics into proof-relevant typed representation, VIR nodes, provenance, canonicalization, serialization, hashing, or proof-obligation generation.
---

# C++L VIR Change

VIR represents formal meaning, not arbitrary frontend structure.

## Read first

- `AGENTS.md`
- `docs/ARCHITECTURE.md`
- relevant `docs/SPEC.md`
- VIR definitions and lowering tests

## Preserve

VIR should remain:

- typed
- explicit
- deterministic
- provenance-preserving
- canonical where practical
- serializable where required
- hash-stable where required
- independent of irrelevant source syntax

## Workflow

1. State the semantic fact the new representation carries.
2. Show why existing VIR cannot represent it correctly.
3. Define invariants for the new node or field.
4. Update lowering.
5. Update proof-obligation generation.
6. Update serialization/hash logic if applicable.
7. Add equivalent-source and malformed-input tests.
8. Check cache invalidation.
9. Update `docs/ARCHITECTURE.md` for structural VIR changes.

## Do not

- use raw Clang AST identity as logical identity;
- encode semantic variants as magic strings;
- introduce two authoritative representations of the same fact;
- discard provenance required for diagnostics or trust reporting.
