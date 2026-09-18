# C++L Trust Model

This document defines what must be trusted for a C++L proof to mean what it claims to mean.

Formal verification is only useful when the boundary of trust is explicit.

The objective is not to pretend that nothing is trusted.

The objective is to make trust **small, visible, auditable, and difficult to expand accidentally**.

---

## 1. Principle

A successful build should answer two separate questions:

```text
Did the program compile?

Did the claimed Laws actually follow from the stated assumptions?
```

C++L must never confuse these questions.

---

## 2. Trusted Computing Base

The Trusted Computing Base, or TCB, is the collection of components whose incorrect behavior could cause C++L to accept a false theorem as proven.

The TCB SHOULD be minimized aggressively.

Conceptually:

```text
untrusted / complex
────────────────────────────────

parser
elaborator
tactics
AI
SMT automation
optimizers
proof search

           ↓ proof evidence

trusted boundary
────────────────────────────────

small proof kernel
formal primitive semantics
explicit trusted axioms

           ↓

PROVEN
```

## 2. Contextual keywords and C++ compatibility

C++L MUST NOT introduce globally reserved identifier keywords unless a future language revision explicitly justifies doing so.

All C++L-specific textual keywords SHOULD be contextual keywords and MUST be recognized only in grammar positions where C++L syntax is expected.

Examples include:

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

---

## 3. Proof kernel

The proof kernel is the final authority for theorem validity.

It SHOULD be:

- small
- deterministic
- auditable
- dependency-light
- fuzzable
- extensively property-tested
- formally specified
- eventually mechanized where practical

The kernel MUST reject malformed or invalid evidence.

---

## 4. Kernel responsibilities

The kernel or equivalent trusted formal core must validate the rules necessary for:

- types
- propositions
- proof terms
- equality
- substitution
- dependent functions
- universal quantification
- existential quantification
- induction
- refinements
- normalization
- proof composition
- impossible-state elimination

The kernel MUST NOT contain unrelated compiler functionality.

---

## 5. Frontend

The parser and elaborator are complex and therefore SHOULD NOT automatically be considered trusted proof authorities.

Preferred design:

```text
source syntax
    ↓
frontend
    ↓
explicit core representation
    ↓
kernel validation
```

If frontend elaboration changes the logical meaning of a term incorrectly, the resulting core term should still fail kernel checking unless the kernel itself contains a defect.

---

## 6. Clang

C++L relies on Clang for ordinary C++ semantics and native compilation.

That creates two different trust questions.

### Proof trust

Clang SHOULD NOT decide whether a theorem is mathematically valid.

### Runtime trust

Clang and LLVM are trusted to preserve the executable semantics of the lowered program sufficiently for C++L's runtime guarantees.

This distinction must remain explicit.

---

## 7. Erasure trust

Proof erasure is security-critical.

Given:

```text
verified C++L
    ↓
erase proof-only state
    ↓
runtime C++
```

the erasure pass MUST preserve runtime semantics.

Ghost state MUST NOT influence runtime behavior.

The erasure pass MUST NOT:

- introduce new undefined behavior;
- remove executable conditions required for dynamic validation;
- alter runtime values;
- reorder observable behavior incorrectly;
- invalidate verified lifetime assumptions.

Erasure SHOULD have dedicated equivalence tests.

---

## 8. SMT solvers

SMT solvers are automation engines, not definitions of truth.

Preferred model:

```text
proof obligation
    ↓
SMT
    ↓
certificate / proof evidence
    ↓
independent checker
```

When certificate checking is unavailable and a solver result must be trusted directly, that solver MUST appear in the trust report.

Example:

```text
Trusted automation:
  cvc5: yes
  Z3: no
```

The long-term objective should be to reduce solver trust where practical.

---

## 9. AI systems

AI-generated code or proofs are never inherently trusted.

```text
AI output
    ↓
parser
    ↓
proof checker
    ↓
accepted / rejected
```

An AI's confidence, chain of reasoning, or textual explanation has no formal authority.

Only accepted formal evidence matters.

---

## 10. Unsafe code

Unsafe C++ MAY exist.

Unsafe code MUST NOT automatically create trusted theorem evidence.

Example:

```cpp
unsafe {
    auto x = platform_call();
}
```

The value `x` is not automatically verified merely because the surrounding program compiled.

Before entering a verified domain it requires:

- proof;
- validation;
- or an explicit trusted contract.

---

## 11. Trusted assumptions

C++L MAY allow declarations whose correctness cannot be verified internally.

Such declarations must be explicit.

Conceptually:

```cpp
trusted law platform_guarantee(...)
    proves ...;
```

A trusted proposition is not the same as a proven proposition.

Tooling MUST preserve this distinction.

---

## 12. Trust transitivity

If:

```text
Law A
depends on
Law B
```

and `Law B` is trusted rather than proven, `Law A` depends transitively on that trusted assumption.

The trust report SHOULD show this dependency.

Example:

```text
law payment_conservation
  status: proven
  depends on trusted:
    bank_api_atomicity
```

A proof does not erase the assumptions from which it was derived.

---

## 13. FFI

Foreign code is not automatically verified.

Examples:

- C libraries
- operating-system APIs
- JNI
- Objective-C++
- N-API
- inline assembly
- device drivers
- GPU APIs

Foreign interfaces must be classified as:

```text
verified
specified-but-trusted
runtime-validated
unsafe
```

The classification must be machine-visible.

---

## 14. Standard library

Using the C++ standard library does not automatically make every implementation detail formally verified.

C++L SHOULD provide verified specifications for frequently used library abstractions.

Examples:

- `std::span`
- `std::array`
- `std::vector`
- `std::optional`
- `std::variant`
- smart pointers
- strings
- algorithms

A verified specification may model externally implemented code without reproducing every implementation detail.

---

## 15. Undefined behavior

Undefined behavior is a direct threat to proof validity.

If the verifier proves:

```text
x == 10
```

but runtime execution contains UB before `x` is observed, the logical guarantee may become meaningless.

Verified regions must therefore either:

1. prove absence of relevant UB;
2. reject the construct;
3. or explicitly cross an unsafe/trusted boundary.

---

## 16. Memory safety

The trust model must account for at least:

- lifetime
- aliasing
- pointer provenance
- nullability
- bounds
- mutation
- move semantics
- destruction
- concurrency

The implementation MUST NOT claim memory-related Laws while ignoring C++ memory semantics required to justify them.

---

## 17. Concurrency

Data races and weak-memory behavior can invalidate naive sequential reasoning.

Verified concurrent code therefore requires an explicit concurrency model.

Until such a model is implemented, concurrency constructs that affect proof validity SHOULD be:

- rejected in verified regions;
- isolated behind verified abstractions;
- or marked unsafe/trusted.

---

## 18. Runtime validation

Runtime validation is not theorem proving.

Example:

```cpp
Percentage p = checked<Percentage>(input);
```

At runtime:

```text
input = 140
```

may be rejected.

After successful validation:

```text
0 <= p <= 100
```

is established for the verified region according to the semantics of `checked`.

The runtime check establishes a fact about a runtime value.

It does not require a runtime theorem engine.

---

## 19. Build modes

The reference implementation SHOULD support distinct modes.

Example:

```text
cppl check
cppl build
cppl trust-report
```

A strict mode MAY reject any build containing:

- unresolved obligations
- trusted assumptions
- unsafe boundaries
- unverified FFI

Example:

```bash
cppl build --require-fully-verified
```

---

## 20. Trust report

The trust report should expose at least:

```text
C++L Trust Report

Laws proven:                 241
Laws trusted:                  0
Unresolved obligations:        0

Unsafe regions:                3
Runtime validation sites:      7
Unverified FFI boundaries:     0

Trusted solvers:               0
Trusted external axioms:       0

Kernel version:
  ...

Compiler version:
  ...

Underlying C++ mode:
  c++23
```

For each proven Law the tool SHOULD be able to display its assumption closure.

---

## 21. Hidden axioms

Hidden axioms are forbidden.

A feature that introduces an axiom into the logical system MUST make that fact visible to tooling.

No compiler optimization, plugin, tactic, solver shortcut, or foreign binding may silently introduce logical assumptions.

---

## 22. Proof caching

Cached proof results MUST be content-addressed or otherwise invalidated whenever relevant inputs change.

Relevant inputs include:

- theorem statement
- implementation
- imported Laws
- type definitions
- compiler semantics
- kernel version
- solver assumptions
- target machine semantics where relevant

Stale proof caches MUST NOT cause an invalid theorem to remain accepted.

---

## 23. Determinism

Kernel checking SHOULD be deterministic.

Given identical:

- formal input
- kernel version
- configuration

the accept/reject result MUST be identical.

Automation may explore different strategies, but accepted evidence must still validate deterministically.

---

## 24. Reproducibility

Verification results SHOULD be reproducible.

Long-term releases SHOULD record:

- C++L compiler version
- kernel version
- target
- C++ standard mode
- solver versions
- relevant configuration
- trusted assumptions

This allows a proof result to be audited later.

---

## 25. Formalization

The long-term objective SHOULD be to provide a mechanized model of the C++L core calculus.

Potential hosts include established theorem provers.

The mechanized model should eventually cover:

- type formation
- proof typing
- equality
- substitution
- normalization assumptions
- termination
- soundness properties

The implementation and formal model must not be allowed to drift silently.

---

## 26. Proof of compiler correctness

A fully verified compiler is not required for initial C++L usefulness.

However, the architecture SHOULD permit progressive reduction of compiler trust.

A reasonable progression is:

```text
Stage 1
small proof kernel

Stage 2
mechanized kernel semantics

Stage 3
verified erasure properties

Stage 4
verified critical lowering passes

Stage 5
stronger end-to-end refinement guarantees
```

These stages improve trust without changing what a C++L Law means.

---

## 27. Security classification

A bug should be treated as a security issue when it can cause C++L to:

- accept a false proof;
- forge proof evidence;
- omit an assumption from the trust report;
- miscompile verified semantics;
- erase a required runtime validation;
- incorrectly mark unsafe code as verified;
- permit UB to invalidate a claimed guarantee.

See `SECURITY.md`.

---

## 28. Fundamental rule

The project must always be able to answer:

> Why should I believe this Law is true?

The answer should reduce to:

```text
formal statement
+
explicit assumptions
+
kernel-checked evidence
+
sound runtime semantics
```

and never:

```text
because the compiler said so
```
