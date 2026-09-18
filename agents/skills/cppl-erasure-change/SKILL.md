---
name: cppl-erasure-change
description: Modify C++L proof or ghost erasure. Use when changing removal of proofs, ghost state, compile-time indices, theorem-only constructs, lowering to ordinary C++, or anything that may affect runtime equivalence after verification.
---

# C++L Erasure Change

Proof information must disappear without changing required runtime behavior.

## Read first

- `AGENTS.md`
- `SPEC.md`
- `TRUST.md`
- `ARCHITECTURE.md`
- erasure tests

## Core obligation

Preserve the relevant equivalence:

```text
runtime semantics of verified source
=
runtime semantics after erasure
```

## Verify that erasure does not remove

- required runtime validation
- observable side effects
- runtime data
- ABI-relevant state
- required lifetime operations
- destruction
- required safety checks

## Verify that ghost/proof data cannot

- affect runtime branching;
- escape through FFI;
- change object layout unless specified;
- affect destruction;
- become runtime-observable.

## Required tests

- before/after runtime-equivalence tests
- ghost leakage tests
- ABI/layout tests where relevant
- runtime-validation preservation
- malformed erasure input
- native Clang compilation

Update `TRUST.md` if erasure trust changes.
