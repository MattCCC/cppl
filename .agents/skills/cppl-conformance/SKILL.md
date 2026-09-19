---
name: cppl-conformance
description: Test or review C++L source compatibility with C++. Use for C++ standard modes, syntax collisions, templates, concepts, constexpr, modules, macros, ABI, exceptions, RTTI, compiler extensions, or regressions where valid C++ may stop being valid C++L.
---

# C++L C++ Conformance

Primary invariant:

```text
valid supported C++
    remains
valid C++L
```

## Read first

- `AGENTS.md`
- `SPEC.md`
- `COMPATIBILITY.md`
- affected Clang integration

## Cover

- ordinary declarations
- contextual-keyword collisions
- templates
- concepts and `requires`
- constexpr
- macros
- modules
- overload resolution
- exceptions
- RTTI
- ABI-sensitive constructs
- supported compiler extensions
- each supported C++ standard mode

## Distinguish

```text
source accepted as C++
```

from:

```text
semantics fully verified by C++L
```

A verification limitation should not silently become a parser incompatibility unless the specification explicitly requires rejection.

When adding C++L syntax, add ordinary-C++ collision regressions.
