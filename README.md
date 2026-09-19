<p align="center">
  <img src="docs/cppl.png" alt="C++L" width="800">
</p>

# C++L - C++ with Laws

<b>C++L</b> is <b>C++ with Laws</b>: an ambiguity-free, proof-carrying superset of C++ in which humans or Agentic AIs can specify intent as machine-checkable Laws, implementations are accepted only when those Laws are proven, and all proof machinery erases to ordinary optimized C++ compiled by Clang/LLVM.

C++L makes formal intent and proof <b>first-class language constructs</b> while remaining a source-compatible C++ superset, then erases that formal layer into normal native C++.

It means that the C++L keeps <b>standard C++ as the runtime language</b> and adds a formal compile-time layer for intent, proof, and correctness. What we do here is a true C++ source-compatible superset with first-class Laws, propositions/proofs, dependent/refinement types, termination checking, proof erasure, and ordinary Clang/LLVM runtime output.

<p align="center">
C++L makes semantic laws first-class program declarations and requires evidence that they actually hold, making it provable.<br>
  <b>C++ ⊂ C++L</b><br>
  <b>C++L = C++ + Laws</b><br>
  <b>Laws → Obligations → Evidence</b>
</p>

## Motivation

The project started from a simple chain of thought:

> If a human or AI state precise intent, can the compiler mechanically prove the implementation satisfies it, and can the proof layer erase to fast native C++ without any runtime addition?
> Humans and AIs need an ambiguity-free language for specifying what software must do, a mechanically checkable way to prove that an implementation satisfies that intent, and a path to high-performance native execution. Can we do that without creating entirely new language and work with existing tooling?
> What if these requirements could be part of the C++ language itself rather than remaining in tests, comments, fixtures, and engineering conventions?

C++L is an attempt to answer that question while preserving the C++ runtime, ABI, ecosystem, and Clang/LLVM toolchain. The project was initiated by Mateusz Czapliński equipped with AI, steming from this practical need. The goal is to make a Provable C++.

## Core idea

```text
C++L source
=
C++ runtime code
+
formal Laws
+
proofs
+
dependent/refinement types
+
compile-time correctness information

        ↓

C++L compiler/checker

        ↓

prove:
- Laws
- invariants
- dependent relationships
- refinements
- equality
- termination
- safety obligations

        ↓

erase:
- proofs
- ghost state
- theorem-only values
- compile-time-only type information

        ↓

ordinary C++

        ↓

Clang / LLVM

        ↓

normal native binary
```

C++L requires no dedicated proof VM, theorem runtime, garbage collector, or alternate execution model.

Proofs and Laws are checked at compile time and erased before native code generation.

Runtime checks occur only when explicitly required to validate values that cannot be known statically.

C++L adds new syntax and semantics such as:

```cpp
law
proves
proof
pure
verified
ghost
unsafe
trusted
where
expects
ensures
decreases
data
match
```

while ordinary supported C++ remains valid C++L.

## Mission

C++L exists to enable this workflow:

```text
Human / AI expresses intent
        ↓
formal Law

AI writes implementation
        ↓
C++L

C++L proves implementation satisfies Law
        ↓

proof succeeds
        ↓

proof information erased
        ↓

ordinary optimized C++
        ↓

native binary
```

The implementation is accepted because the compiler can prove the specification, not because a test suite happened to pass.

## Usage

Compile ordinary C++ with `cppl`:

```bash
cppl -std=c++17 main.cpp -o main
```

C++L is designed for incremental adoption: existing supported C++ can continue to compile unchanged, while formal verification is added where needed.

Example:

```cpp cppl-example
pure int identity(int x) {
    return x;
}

law identity_returns_input(int x)
    ensures(identity(x) == x);
```

A Law states a proposition. A proof declaration supplies the evidence for one, and the kernel decides whether that evidence holds:

```cpp cppl-example
pure int identity(int x) {
    return x;
}

law identity_returns_input(int x)
    ensures(identity(x) == x);

proof identity_returns_input_holds(int x)
    proves(identity_returns_input(x))
{
    refl;
}
```

A Law holds for every value of its parameters, so its proof can be used at any one of them:

```cpp cppl-example
pure unsigned identity(unsigned x) {
    return x;
}

law identity_returns_input(unsigned x)
    ensures(identity(x) == x);

proof identity_general(unsigned x)
    proves(identity_returns_input(x))
{
    refl;
}

law identity_of_41()
    ensures(identity(41u) == 41u);

proof identity_at_41()
    proves(identity_of_41())
{
    exact identity_general(41u);
}
```

A Law can be stated under a precondition. `expects` does not assert that the precondition holds: it says what the Law concludes under it, and a proof may name that premise and use it:

```cpp cppl-example
pure unsigned add_one(unsigned x) {
    return x + 1u;
}

law increment_is_stable(unsigned x)
    expects(add_one(x) == x)
    ensures(add_one(x) == x);

proof increment_is_stable_holds(unsigned x)
    proves(increment_is_stable(x))
{
    assume premise : add_one(x) == x;
    exact premise;
}
```

Applying a Law that supposes a premise leaves that premise to prove:

```cpp cppl-example
pure unsigned identity(unsigned x) {
    return x;
}

pure unsigned add_one(unsigned x) {
    return x + 1u;
}

law guarded_increment(unsigned x)
    expects(identity(x) == x)
    ensures(add_one(x) == add_one(x));

proof guarded_increment_holds(unsigned x)
    proves(guarded_increment(x))
{
    refl;
}

law increment_is_itself(unsigned x)
    ensures(add_one(x) == add_one(x));

proof increment_is_itself_holds(unsigned x)
    proves(increment_is_itself(x))
{
    apply guarded_increment_holds(x);
    refl;
}
```

A premise is worth supposing because it can be *used*. `rewrite` transforms the goal with an equality that has already been established:

```cpp cppl-example
pure unsigned identity(unsigned x) {
    return x;
}

law identity_at_zero(unsigned x)
    expects(x == 0u)
    ensures(identity(x) == 0u);

proof identity_at_zero_holds(unsigned x)
    proves(identity_at_zero(x))
{
    assume h : x == 0u;
    rewrite h;
    refl;
}
```

`assume` is valid here because the Law's `expects` clause makes the goal an implication whose premise is `x == 0u`. It names that premise; it never grants one. `rewrite h;` then replaces `x` by `0u` in the goal, leaving `identity(0u) == 0u` to prove. The kernel performs the substitution itself and checks the result.

Contracts can also verify an executable function directly:

```cpp cppl-example
verified int identity(int x)
    ensures(result == x)
{
    return x;
}
```

The obligation comes from the Clang-resolved return expression. `result` names
that value only in the specification; every obligation must pass the kernel.
The current fragment supports one pure return expression and integer equalities:

```cpp cppl-example
verified unsigned inc(unsigned x)
    ensures(result == x + 1u)
{
    return x + 1u;
}

verified unsigned zero_if_zero(unsigned x)
    expects(x == 0u)
    ensures(result == 0u)
{
    return x;
}
```

Preconditions become proof hypotheses, never trusted facts or runtime checks.
Verified functions can also call one another through their contracts:

```cpp cppl-example
verified unsigned bump_zero(unsigned x)
    expects(x == 0u)
    ensures(result == 1u)
{
    return x + 1u;
}

verified unsigned bump_one(unsigned x)
    expects(x == 1u)
    ensures(result == 2u)
{
    return x + 1u;
}

verified unsigned two_from_zero(unsigned x)
    expects(x == 0u)
    ensures(result == 2u)
{
    return bump_one(bump_zero(x));
}
```

The inner call's proven postcondition discharges the outer call's precondition.
The caller uses those contracts, and the kernel checks their connection to the
actual body. Erased C++ retains both calls with no runtime checks. This fragment
supports equality and unsigned addition; ordering and subtraction remain future
work. Unsupported bodies and unproved call preconditions fail compilation. See
[SPEC.md](SPEC.md#126-compositional-verified-calls) for the boundary.

For installation, compiler options, project integration, Laws, proofs, verification statuses, and examples, see [DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md).

## Other Examples

This example uses inductive types and proof-aware pattern matching, which this implementation does not accept yet:

```cpp cppl-planned
data Nat {
    Zero;
    Succ(Nat predecessor);
};

pure Nat add(Nat a, Nat b) {
    return match (a) {
        Zero => b;
        Succ(n) => Succ(add(n, b));
    };
}

law add_zero(Nat x)
    ensures(add(x, Zero) == x);

proof add_zero_holds(Nat x)
    proves(add_zero(x))
{
    match (x) {
        Zero => {
            refl;
        }

        Succ(n) => {
            apply add_zero_holds(n);
        }
    }
}
```

The Law means:

```text
∀ x : Nat,
    add(x, Zero) = x
```

C++L does not enumerate every natural number.

It proves the property symbolically through the structure of `Nat`.

The proof is erased before runtime code generation.

## Laws

A Law is a formal statement of required behavior.

```cpp cppl-planned
law no_duplicate_authority(const Interpretation& x)
    ensures(financialAuthorityCount(x) <= 1);
```

A Law is not:

- a unit test
- a fuzz property
- a runtime assertion
- documentation
- a comment
- a Boolean function that happens to return `true`

A Law is a proof obligation.

If the compiler cannot prove it, verified compilation fails.

## Key capabilities

C++L is intended to provide:

- first-class Laws
- propositions and proofs
- universal and existential quantification
- dependent types
- refinement types
- proof-relevant equality
- definitional equality and normalization
- symbolic induction
- proof-aware pattern matching
- termination checking
- preconditions and postconditions
- pure / verified / ghost / unsafe / trusted boundaries
- proof erasure
- explicit handling of unsafe C++ and undefined behavior
- machine-accurate arithmetic reasoning
- a small trusted proof kernel
- proof automation and counterexamples
- ordinary C++ ABI and ecosystem interoperability

## Verified and ordinary C++

C++L is a syntactic superset of C++.

That does not mean arbitrary C++ automatically becomes formally verified.

```text
C++L
├── ordinary C++
│   └── executable but not automatically proven
│
└── verified C++L
    └── formal guarantees apply
```

The boundary must be explicit.

## Runtime model

C++L does not replace the C++ execution model.

```text
C++L
    ↓ proof/type erasure
C++
    ↓
Clang / LLVM
    ↓
native executable
```

C++L should remain compatible with:

- normal C++ ABI
- libc++
- C libraries
- JNI
- Objective-C++
- N-API
- WASM
- platform APIs
- existing native libraries

## Non-negotiable guarantees

1. Laws represent formal intent.
2. Laws are universally proven rather than tested.
3. False Laws are rejected.
4. Proof evidence cannot be forged by ordinary executable code.
5. Proof-producing computation cannot exploit nontermination.
6. Types may express value-dependent relationships.
7. Equality can be formally reasoned about.
8. Inductive structures can be proven over symbolically.
9. Proof-only information can be erased.
10. Erasure preserves executable semantics.
11. Unsafe operations cannot silently contaminate verified proofs.
12. C++ undefined behavior cannot invalidate verified guarantees.
13. Trusted assumptions are explicit.
14. Verification is deterministic.
15. Automation does not silently become the source of truth.
16. Runtime code remains ordinary optimized C++.
17. Existing C++ interoperability is preserved.
18. No mandatory theorem runtime is introduced.

## Non-goals

The following alone do **not** constitute C++L:

```text
C++ + unit tests
C++ + property tests
C++ + fuzzing
C++ + assertions
C++ + static_assert
C++ + concepts
C++ + contracts
C++ + annotations
C++ + Z3
C++ + external linting
```

All of those may be useful tools.

None alone provide the intended proof-aware language model.

## Documentation

- [DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md) - practical developer guide with examples
- [SPEC.md](SPEC.md) - normative C++L language semantics
- [ARCHITECTURE.md](ARCHITECTURE.md) - compiler structure, component boundaries, and data flow
- [DESIGN.md](DESIGN.md) - design rationale and major language/compiler decisions
- [FOUNDATIONS.md](FOUNDATIONS.md) - mathematical foundations and intellectual lineage
- [TRUST.md](TRUST.md) - Trusted Computing Base, assumptions, and trust boundaries
- [COMPATIBILITY.md](COMPATIBILITY.md) - C++ source, ABI, toolchain, and standard compatibility
- [STATUS.md](STATUS.md) - current implementation status and verification maturity
- [ROADMAP.md](ROADMAP.md) - planned implementation sequence and milestones
- [SECURITY.md](SECURITY.md) - soundness and security policy
- [ACKNOWLEDGEMENTS.md](ACKNOWLEDGEMENTS.md) - intellectual and project credits
- [CONTRIBUTING.md](CONTRIBUTING.md) - contribution and development process
- [AGENTS.md](AGENTS.md) - mandatory repository rules for AI agents and automated contributors

## Definition of done

C++L succeeds when a human or AI can:

1. express software intent as formal Laws;
2. implement the software in a C++-compatible language;
3. mechanically prove that the implementation satisfies those Laws;
4. inspect all assumptions and unsafe boundaries;
5. erase all proof-only information;
6. compile the remaining code with normal Clang/LLVM;
7. obtain a normal high-performance native binary.

```text
FORMAL INTENT
+
MACHINE-CHECKED PROOFS
+
DEPENDENT / REFINEMENT TYPES
+
C++ SYSTEMS PROGRAMMING
+
C++ ABI AND ECOSYSTEM
+
ZERO-COST PROOF ERASURE
+
CLANG / LLVM
=
C++L
```

**C++L is C++ with Laws.**
