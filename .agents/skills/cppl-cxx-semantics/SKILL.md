---
name: cppl-cxx-semantics
description: Model C++ runtime semantics for verification. Use for integers, conversions, pointers, references, ownership, object lifetime, aliasing, moves, destruction, exceptions, undefined behavior, atomics, concurrency, target behavior, or other proof-relevant C++ execution semantics.
---

# C++L C++ Semantics

Proof semantics must correspond to the C++ that actually executes.

## Read first

- `AGENTS.md`
- `SPEC.md`
- `COMPATIBILITY.md`
- `ARCHITECTURE.md`
- relevant C++ standard/Clang behavior

## For every modeled feature specify

```text
supported behavior:
unsupported behavior:
undefined-behavior conditions:
target dependencies:
formal representation:
runtime correspondence:
trusted assumptions:
```

## Rules

Do not silently equate:

```text
machine integer = mathematical integer
pointer = integer
reference = non-null pointer
move = copy
object storage = object lifetime
floating point = real arithmetic
```

Unsupported semantics must fail closed or cross an explicit unsafe/trusted boundary.

Add tests for valid behavior, invalid behavior, UB boundaries, and target-sensitive cases.
