---
name: cppl-stdlib-model
description: Add or change formal models for C++ standard-library facilities. Use for std types, algorithms, iterators, allocators, containers, smart pointers, strings, ranges, concurrency primitives, or other standard-library behavior used by verified C++L.
---

# C++L Standard Library Model

Keep the formal specification separate from a particular library implementation.

## Read first

- `AGENTS.md`
- `docs/SPEC.md`
- `docs/TRUST.md`
- `docs/COMPATIBILITY.md`
- relevant C++ standard requirements

## For each model identify

```text
standard-guaranteed behavior:
implementation-specific behavior:
preconditions:
postconditions:
invalidation rules:
ownership/lifetime behavior:
complexity assumptions if proof-relevant:
trusted implementation correspondence:
unsupported operations:
```

## Rules

Do not claim:

```text
formal model exists
→ libc++ / libstdc++ implementation is formally verified
```

Expose any assumption that the runtime implementation satisfies the model.

Prefer small reusable specifications over implementation-specific modeling.

Add conformance tests against supported standard-library implementations where practical.
