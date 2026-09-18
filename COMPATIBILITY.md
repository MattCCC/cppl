# C++L Compatibility

C++L is intended to be a source-compatible superset of C++.

This document defines the compatibility goals between C++L, C++, Clang, native ABIs, and existing libraries.

---

# Core principle

```text
C++ ⊂ C++L
```

A supported ordinary C++ translation unit should remain valid C++L source unless it uses an identifier reserved by C++L or depends on unsupported implementation-specific behavior.

C++L adds formal semantics without replacing the C++ runtime ecosystem.

---

# C++ standard modes

C++L-specific proof semantics should be as independent as possible from the selected underlying C++ standard.

Conceptually:

```bash
cppl -std=c++17
cppl -std=c++20
cppl -std=c++23
```

The meaning of:

```cpp
law ...
proof ...
pure ...
```

should not change merely because the runtime C++ standard changes.

The set of ordinary C++ syntax available naturally follows the selected C++ standard.

---

# Initial baseline

The reference implementation may initially target modern C++ only.

A practical initial baseline is:

```text
C++17+
```

Earlier C++ compatibility may be added independently if there is meaningful demand.

The proof language itself should not depend unnecessarily on features introduced in a particular C++ standard.

---

# Clang

C++L should use Clang as the authority for ordinary C++ parsing and semantic analysis wherever practical.

This includes:

- overload resolution
- template instantiation
- standard conversions
- declarations
- name lookup
- constexpr
- object types
- inheritance
- C++ diagnostics

C++L should not maintain a second independent interpretation of ordinary C++ when Clang can provide the resolved semantics.

---

# ABI

Proof-only constructs should normally have no ABI representation.

Example:

```text
C++L function
+
proof arguments
+
ghost arguments

    ↓ erase

ordinary C++ ABI
```

Where a dependent value is needed at runtime, its runtime representation remains part of the ABI normally.

C++L should avoid creating a new mandatory native ABI.

---

# Libraries

C++L should interoperate with existing C++ libraries.

A library may be:

```text
ordinary/unverified
verified by specification
fully verified
trusted
unsafe
```

Link compatibility does not imply proof compatibility.

---

# Templates

C++ templates remain C++ templates.

C++L may add proof relationships involving template parameters.

Example:

```cpp
template<class T, size_t N>
law array_size(...)
    proves ...;
```

C++L should reuse Clang's template instantiation semantics.

It should not invent a second incompatible C++ template system.

---

# constexpr and consteval

C++L may reuse results of C++ compile-time evaluation where doing so is sound.

However:

```text
C++ constexpr evaluation
```

and:

```text
C++L proof normalization
```

are conceptually different mechanisms.

The verifier must not assume they are interchangeable without defined semantics.

---

# Exceptions

Exception behavior must be represented explicitly in verified function semantics.

A function proven only for normal return must not silently be treated as proving behavior for exceptional exits.

C++L may initially restrict exceptions inside fully verified regions.

Any restriction must be explicit.

---

# RTTI

RTTI may remain available to ordinary C++.

Verified reasoning about:

- `dynamic_cast`
- `typeid`

requires defined proof semantics.

Unsupported RTTI reasoning should cross an unverified or trusted boundary rather than receiving invented semantics.

---

# Inline assembly

Inline assembly remains possible only through an explicit unsafe boundary unless independently specified and verified.

---

# Compiler extensions

Compiler-specific C++ extensions must be classified explicitly.

Possible classifications:

```text
supported + verified
supported + unverified
unsupported
```

No extension should silently receive formal semantics it does not actually have.

---

# C

C++L should interoperate with C through normal C/C++ ABI mechanisms.

C functions require explicit contracts before verified C++L may rely on their behavior.

---

# Objective-C++

C++L should preserve Objective-C++ interoperability where Clang supports it.

Formal guarantees across Objective-C runtime boundaries require explicit contracts or runtime validation.

---

# JNI

JNI calls are FFI boundaries.

JNI compatibility should use normal native calling conventions.

Formal assumptions about Java/Kotlin-side behavior must be declared explicitly.

---

# N-API

N-API calls are FFI boundaries.

JavaScript values entering verified C++L require validation before refined assumptions can be used.

---

# WASM

C++L should be able to target WebAssembly wherever the erased C++ can already be compiled to WebAssembly through the selected toolchain.

Proof semantics are compile-time and do not require a theorem runtime in WASM.

---

# Compatibility rule

C++L must distinguish:

```text
can compile with
```

from:

```text
has formally verified semantics for
```

A library or feature may be perfectly executable without yet belonging to the verified subset.

That distinction is fundamental to preserving C++ compatibility without weakening formal guarantees.
