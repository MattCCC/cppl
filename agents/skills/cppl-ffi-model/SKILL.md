---
name: cppl-ffi-model
description: Model or review a C++L foreign-function boundary. Use for C, Objective-C++, JNI, N-API, assembly, operating-system APIs, native libraries, device APIs, callbacks, or other external code interacting with verified C++L.
---

# C++L FFI Model

Foreign code is not automatically verified.

## Read first

- `AGENTS.md`
- `SPEC.md`
- `TRUST.md`
- `COMPATIBILITY.md`

## Define the boundary

For every FFI operation determine:

- inputs and outputs
- ownership
- lifetimes
- aliasing
- mutation
- exceptional behavior
- thread behavior
- possible UB
- runtime validation
- trusted assumptions

Classify the boundary using the statuses defined by the project.

## Rules

A wrapper does not prove its foreign implementation.

Foreign code must not be able to manufacture logical proof evidence.

Values entering a verified domain must satisfy their claimed invariants through:

- proof;
- runtime validation;
- explicit trust;
- or explicit unsafe handling.

Update `TRUST.md` when new foreign assumptions are introduced.

Add tests at both sides of the boundary.
