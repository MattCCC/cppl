---
name: cppl-runtime-validation
description: Design or modify C++L runtime validation for dynamic external values entering verified domains. Use for refined values from network, files, user input, FFI, environment, deserialization, sensors, or other data unavailable at compile time.
---

# C++L Runtime Validation

Runtime validation establishes properties of concrete runtime values.

It is not runtime theorem proving.

## Read first

- `AGENTS.md`
- `docs/SPEC.md`
- `docs/TRUST.md`
- relevant boundary model

## Required flow

```text
untrusted dynamic value
        ↓
validation
        ↓
validated value carrying established invariant
        ↓
verified domain
```

## Check

- every refinement condition is validated;
- failure cannot construct the refined value;
- validation cannot be optimized away incorrectly;
- provenance remains `RUNTIME-CHECKED` where required;
- FFI/deserialization cannot bypass construction;
- integer and target semantics match verification semantics.

Do not mark runtime validation itself as compile-time `PROVEN`.

Erasure must preserve checks required for dynamic values.
