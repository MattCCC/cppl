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

---

# Implemented compatibility

This section records what the current implementation actually accepts. The rest
of this document describes the compatibility goal; `STATUS.md` records maturity.

## Standard modes

`-std=c++17`, `-std=c++20` and `-std=c++23` are exercised by the conformance
suite: an ordinary program compiles and runs unchanged in each, and a program
using C++L words as ordinary identifiers does too.

The compiler is built as C++23. The program handed to code generation is the
user's own text with formal spans blanked, so erasure can only delete and can
never introduce a construct from a later standard. That property is checked
directly: the runtime program is emitted and compiled on its own under
`-std=c++17 -pedantic-errors -Werror`.

## Contextual recognition

C++L syntax is recognized after preprocessing. A macro is therefore expanded
before C++L looks at the token, and a macro named after a C++L word keeps its
ordinary preprocessor meaning.

`law` is recognized only when a parameter list is followed by a contract clause,
which ordinary C++ cannot have in that position. So these remain ordinary C++:

```cpp
int law = 1;
law make_law(int x);
law value = compute();
```

`proof` is recognized on the same principle: it introduces a proof declaration
only when a `proves` clause follows the parameter list. So these remain ordinary
C++:

```cpp
void proof();
proof make_proof(int x);
proof* holder(int x);
```

`refl`, `exact`, `apply`, `assume`, and `rewrite` are contextual `proof` keywords: they are interpreted specially only inside a proof body and remain ordinary identifiers elsewhere. Inside a proof body, `exact q(a, b);` names proof evidence `q` and supplies terms at which to instantiate it, `assume h : P;` binds a name to the premise the goal supposes, and `rewrite h;` transforms the goal with an equality that name stands for. Those arguments, and that proposition, are ordinary C++ expressions resolved and type-checked by Clang in the proof's lexical scope. C++L then elaborates only the expression forms and conversions that its VIR models; anything Clang accepts but C++L cannot faithfully lower is explicitly refused.

`expects` on a Law is recognized wherever `ensures` is, and states the Law's precondition. A Law with one states an implication, so the precondition is never an assumption C++L makes about the program. A Law with more than one `expects` clause is refused, because conjoining them is not something the formal core can yet express.

`pure` and `verified` are recognized only where the following tokens cannot
begin an ordinary declaration whose type carries that name. `pure f(int);` is
therefore left alone, because it may declare `f` returning a type named `pure`.

One known gap: if a program declares a type named `pure` or `verified` and then
declares a variable of that type qualified by `const` or `volatile`, for example
`pure const value;`, the declaration is diagnosed rather than compiled. The
construct is rejected, never silently reinterpreted.

A second: a Law is analysed as a C++ function carrying the Law's own name, so
that a proof can name it through ordinary lookup. A Law whose name already
belongs to a function in the same scope is therefore reported by Clang as a
redeclaration, pointing at the Law. The name clash is real - `GRAMMAR.md` 46
puts Law names in a declaration namespace associated with C++ scope - and it is
reported rather than resolved silently. Nothing is emitted into the runtime
program either way.

## Inputs

C++L processes `.cpp`, `.cc`, `.cxx`, `.c++`, `.C` and `.cppl` inputs. A Law
written in a `.h`, `.hpp` or `.hh` header is verified in every source file that
includes it, which is the ordinary way to use one.

Compiling a header directly is passed through to Clang unchanged unless that
header contains C++L constructs, in which case it is refused rather than
compiled unverified.

`-x` is passed through for ordinary C++, and refused for a unit containing C++L
constructs, because the implementation selects the input language itself when it
projects such a unit.

## Arguments

Include paths, defines, optimization levels, warning flags, target flags and
linker arguments are preserved in order and handed to Clang unchanged. Options
that take a separate value are understood well enough not to mistake the value
for an input file.

## Platforms

Subprocess handling uses POSIX process spawning, so the driver builds and runs
on macOS and Linux. Windows support is not implemented.
