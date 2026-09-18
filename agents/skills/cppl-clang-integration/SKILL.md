---
name: cppl-clang-integration
description: Integrate C++L with Clang. Use for parsing ordinary C++, AST access, name lookup, overload resolution, templates, concepts, constexpr, conversions, declarations, source locations, layout, ABI information, or other C++ semantic integration.
---

# C++L Clang Integration

The default is to reuse Clang rather than implement C++ again.

## Read first

- `AGENTS.md`
- `ARCHITECTURE.md`
- `COMPATIBILITY.md`
- relevant Clang API documentation

## Workflow

1. Identify the exact C++ semantic information C++L requires.
2. Determine whether Clang already computes it.
3. Consume semantic results rather than reconstructing them from source text.
4. Preserve source provenance into C++L IR.
5. Translate only proof-relevant information into VIR.
6. Keep Clang AST details outside the proof kernel.
7. Add ordinary-C++ conformance tests.

## Prefer Clang for

- parsing
- lookup
- overload resolution
- template instantiation
- concepts
- conversions
- declarations
- constexpr
- source locations
- layout and ABI facts

## Avoid

- regex-based C++ interpretation;
- duplicate overload resolution;
- duplicate template semantics;
- treating raw syntax as resolved semantics;
- forking Clang without architectural approval.

If Clang cannot provide required semantics, document the gap before introducing an independent model.
