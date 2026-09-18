---
name: cppl-cache-artifacts
description: Modify C++L proof caching, incremental verification, proof artifact serialization, content addressing, dependency hashing, artifact versioning, or deserialization. Use whenever old verification results may be reused.
---

# C++L Proof Cache and Artifacts

A cached proof may be reused only when all proof-relevant inputs remain semantically equivalent.

## Read first

- `AGENTS.md`
- `TRUST.md`
- `ARCHITECTURE.md`
- artifact/cache format definitions

## Cache dependencies may include

- Law
- implementation
- imported Laws
- imported proofs
- types
- VIR
- trusted assumptions
- core-calculus version
- kernel version
- C++ language mode
- target semantics
- solver configuration

## Rules

Prefer:

```text
immutable
content-addressed
versioned
deterministic
```

artifacts.

Deserialization must treat artifacts as untrusted input.

Unknown versions and corrupt artifacts must fail closed.

## Required tests

- changed dependency invalidates cache
- unchanged dependency reuses cache
- corruption is rejected
- old incompatible version is rejected
- changed trusted assumption invalidates relevant evidence
- kernel/calculus changes invalidate incompatible artifacts

Stale proof acceptance is a soundness defect.
