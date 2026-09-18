# C++L Trust Model

This document defines the **Trusted Computing Base (TCB)** and trust boundaries of C++L.

It does **not** define C++L language semantics.

Language syntax and semantics are defined in [SPEC.md](SPEC.md).

The purpose of this document is to answer:

> What must be trusted for a C++L claim marked `PROVEN` to mean what it claims to mean?

The objective is not to eliminate all trust.

The objective is to make trust:

- minimal
- explicit
- auditable
- reproducible
- machine-visible
- difficult to expand accidentally

---

# 1. Scope

`SPEC.md` defines what C++L constructs mean.

This document defines which components must behave correctly for those meanings and proofs to remain valid.

In particular, this document covers:

- the Trusted Computing Base
- proof-kernel trust
- compiler/frontend trust
- Clang/LLVM trust
- solver trust
- AI trust
- FFI trust
- trusted assumptions
- trust propagation
- erasure trust
- proof-cache trust
- reproducibility
- verification status reporting

This document intentionally does **not** redefine:

- Laws
- dependent types
- refinement types
- equality
- induction
- termination semantics
- contracts
- contextual keywords
- language grammar

Those belong in `SPEC.md`.

---

# 2. Core trust principle

A successful C++L build answers two different questions:

```text
Did the program compile?

Did the claimed Laws follow from the stated assumptions?
```

These questions MUST remain distinct.

Compilation success does not imply proof success.

Proof success does not imply absence of external trusted assumptions.

---

# 3. Trusted Computing Base

The **Trusted Computing Base (TCB)** is the set of components whose incorrect behavior could cause C++L to accept a false proposition as proven.

The TCB SHOULD be minimized aggressively.

Target architecture:

```text
┌───────────────────────────────────────┐
│ Untrusted / independently checked     │
│                                       │
│ parser                                │
│ elaborator                            │
│ tactics                               │
│ proof search                          │
│ AI-generated code                     │
│ AI-generated proofs                   │
│ SMT automation                        │
│ optimizers                            │
└──────────────────┬────────────────────┘
                   │
                   │ proof evidence
                   ▼
┌───────────────────────────────────────┐
│ Trusted proof boundary                │
│                                       │
│ small proof kernel                    │
│ primitive formal semantics            │
│ explicitly declared axioms            │
└──────────────────┬────────────────────┘
                   │
                   ▼
                PROVEN
```

The project SHOULD prefer:

```text
large untrusted producer
+
small trusted checker
```

over:

```text
large trusted verifier
```

where practical.

---

# 4. Proof kernel

The proof kernel is the final authority for logical proof validity.

It SHOULD be:

- small
- deterministic
- dependency-light
- auditable
- fuzzable
- heavily negatively tested
- formally specified
- isolated from unrelated compiler functionality

The kernel MUST reject invalid or malformed proof evidence.

The kernel SHOULD contain only the minimum logic required by the formal core defined in `SPEC.md`.

---

# 5. Kernel trust boundary

The kernel MUST NOT depend on the correctness of:

- AI reasoning
- tactic heuristics
- solver heuristics
- IDE features
- source formatting
- diagnostics
- optimization strategies
- proof search order

Those systems may generate candidate evidence.

The kernel decides whether that evidence is valid.

Conceptually:

```text
complex producer
    ↓
candidate proof
    ↓
kernel
    ├── valid   → accept
    └── invalid → reject
```

---

# 6. Frontend and elaborator

The C++L frontend and elaborator are complex components and SHOULD NOT automatically belong to the logical TCB.

Preferred architecture:

```text
source syntax
    ↓
frontend
    ↓
explicit core representation
    ↓
kernel validation
```

The frontend may be wrong.

The elaborator may be wrong.

Their output MUST still satisfy the kernel.

Where frontend correctness cannot yet be independently checked, that dependency MUST be documented explicitly.

---

# 7. Separation of logical trust and runtime trust

C++L has two distinct trust layers.

## Logical trust

Logical trust determines:

```text
Is this proposition actually established?
```

This is primarily the responsibility of the C++L proof system and kernel.

## Runtime trust

Runtime trust determines:

```text
Does the executable preserve the behavior that was reasoned about?
```

This includes lowering, erasure, C++ compilation, ABI behavior, and execution.

These two forms of trust MUST NOT be conflated.

---

# 8. Clang and LLVM

C++L relies on Clang and LLVM for ordinary C++ compilation and native code generation.

They are not intended to define theorem validity.

Conceptually:

```text
C++L kernel
    decides logical validity

Clang / LLVM
    compile runtime semantics
```

Clang/LLVM therefore belong primarily to the **runtime trust chain**, not the logical proof kernel.

Any guarantee about the final executable depends on the backend preserving the semantics of the generated C++.

---

# 9. C++ semantic dependency

C++L proofs about executable behavior depend on the C++ semantic model used by the verifier corresponding to actual runtime behavior.

The project MUST document which parts of C++ semantics are modeled sufficiently for verified claims.

Where runtime semantics are not modeled, the relevant code MUST remain:

```text
UNVERIFIED
```

or:

```text
UNSAFE
```

or depend on an explicit:

```text
TRUSTED
```

boundary.

The verifier MUST NOT silently claim semantics it does not model.

---

# 10. Erasure trust

Proof erasure is correctness-critical.

Conceptually:

```text
verified C++L
    ↓
proof / ghost erasure
    ↓
ordinary C++
```

The erasure implementation must preserve the runtime behavior defined by the verified program.

The erasure pass MUST NOT silently:

- remove required runtime validation
- introduce undefined behavior
- alter runtime values
- change observable control flow
- invalidate lifetime assumptions
- change promised ABI behavior
- make ghost state observable

Erasure SHOULD eventually have independent equivalence validation.

---

# 11. Erasure verification goal

The long-term target is to establish a property of the form:

```text
runtime_behavior(C++L_program)
=
runtime_behavior(erased_C++_program)
```

for the executable semantics relevant to the verified program.

The exact formal statement belongs in the formal semantics and erasure documentation, not in this trust document.

This document only establishes that **erasure correctness is part of the runtime trust chain**.

---

# 12. SMT solvers

SMT solvers are automation engines.

They SHOULD NOT automatically define mathematical truth.

Preferred model:

```text
proof obligation
    ↓
SMT solver
    ↓
proof / certificate
    ↓
independent checker
    ↓
kernel
```

If a solver result must be trusted directly because independently checkable evidence is unavailable, that fact MUST appear in the trust report.

Example:

```text
Trusted automation:
  cvc5: yes
  Z3: no
```

The long-term direction SHOULD be to reduce direct solver trust where practical.

---

# 13. Tactics

Proof tactics SHOULD be treated as untrusted proof producers.

Examples:

```text
simplifier
rewriter
arithmetic tactic
induction tactic
proof search
```

A bug in a tactic should ideally produce:

```text
invalid proof
    ↓
kernel rejects
```

rather than:

```text
invalid proof
    ↓
false theorem accepted
```

---

# 14. AI systems

AI systems are never formal authorities.

AI-generated:

- code
- proofs
- specifications
- refactorings
- proof repairs
- explanations

must be treated as candidate input.

Conceptually:

```text
AI output
    ↓
C++L checker
    ↓
kernel
    ↓
accepted / rejected
```

An AI's confidence or explanation has no bearing on proof validity.

This separation is a core design requirement of C++L.

---

# 15. User-declared trusted assumptions

C++L may permit explicit assumptions that cannot or should not be proven internally.

These assumptions extend the TCB of the program using them.

A trusted assumption MUST be distinguishable from a proven proposition.

Tooling MUST NOT display both simply as:

```text
verified
```

without exposing the distinction.

---

# 16. No hidden axioms

Hidden axioms are forbidden.

Any mechanism that introduces logical truth without kernel-derived proof MUST be machine-visible.

This includes assumptions introduced by:

- foreign bindings
- compiler intrinsics
- solver shortcuts
- runtime contracts
- plugins
- platform APIs
- external specifications

If an assumption exists, the trust system MUST be capable of reporting it.

---

# 17. Trust propagation

Trust is transitive.

If:

```text
Law A
    ↓ depends on
Law B
```

and `Law B` depends on trusted assumption `X`, then `Law A` also depends on `X`.

Example:

```text
Law:
  payment_conservation

Status:
  PROVEN

Depends on trusted:
  bank_api_atomicity
```

A theorem being proven does not remove the assumptions from which it was derived.

---

# 18. Assumption closure

Tooling SHOULD compute the transitive assumption closure of every Law.

Conceptually:

```text
Law
    ↓
proof dependencies
    ↓
proof dependencies
    ↓
trusted assumptions
```

For any Law, users should eventually be able to ask:

```bash
cppl trust-report <law>
```

and see every trust dependency reachable from it.

---

# 19. Foreign code

Foreign code is not automatically verified.

Examples include:

- C libraries
- Objective-C++
- JNI
- N-API
- operating-system APIs
- device drivers
- GPU APIs
- inline assembly
- external native libraries

Foreign interfaces MUST have an explicit trust classification.

Possible classifications include:

```text
VERIFIED
TRUSTED
RUNTIME-CHECKED
UNSAFE
UNVERIFIED
```

The exact semantics of those classifications belong in `SPEC.md`.

This document requires that the classification remain visible.

---

# 20. Verified wrappers

An external implementation may be exposed through a formally specified wrapper.

Conceptually:

```text
external implementation
        ↓
formal contract
        ↓
verified wrapper
        ↓
verified C++L
```

A wrapper does not prove the external implementation correct.

Its contract defines the trust boundary unless the implementation itself has been independently verified.

---

# 21. Standard library trust

Using the C++ standard library does not imply that its implementation is formally verified.

C++L may provide formal specifications for library abstractions while relying on an external implementation.

Such a model introduces a relationship:

```text
formal specification
    ↕ assumed correspondence
runtime library implementation
```

Any required assumption MUST remain visible in the trust model.

---

# 22. Runtime validation

Runtime validation is not proof-kernel execution.

A dynamic check can establish a fact about a runtime value.

Example:

```text
external input
    ↓
runtime validation
    ↓
value admitted into verified domain
```

The correctness of that validation mechanism is part of the runtime trust chain.

Runtime validation sites SHOULD be reportable separately from purely static proofs.

---

# 23. Proof cache

Proof caching is trust-sensitive.

A stale proof must never remain accepted after a semantically relevant dependency changes.

Cache invalidation MUST account for all inputs relevant to proof validity.

Examples include:

- proposition
- implementation
- imported proofs
- imported Laws
- formal type definitions
- compiler semantics
- kernel version
- trusted assumptions
- relevant target semantics
- solver assumptions

Content-addressed proof artifacts are preferred.

---

# 24. Incremental verification

Incremental checking MAY reuse previous proof results only when semantic dependencies remain valid.

Performance optimization MUST NOT weaken proof dependency tracking.

If dependency validity cannot be established confidently, the proof MUST be recomputed.

---

# 25. Determinism

Kernel checking SHOULD be deterministic.

Given identical:

```text
formal input
kernel version
configuration
```

the kernel should produce the same accept/reject result.

Automation MAY use nondeterministic search internally, but accepted proof evidence must validate deterministically.

---

# 26. Reproducibility

Verification SHOULD be reproducible.

A verification artifact SHOULD eventually record enough information to recreate the result, including:

- C++L compiler version
- proof-kernel version
- target architecture
- selected C++ standard
- solver versions
- trusted assumptions
- verification configuration
- proof artifact hashes

Reproducibility is important for auditing old releases.

---

# 27. Build statuses

C++L tooling MUST distinguish different levels of assurance.

At minimum:

```text
PROVEN
TRUSTED
RUNTIME-CHECKED
UNSAFE
UNVERIFIED
UNRESOLVED
```

These states MUST NOT be silently collapsed into a generic:

```text
verified
```

The exact language semantics of each category belong in `SPEC.md`.

The trust layer is responsible for reporting them accurately.

---

# 28. Strict verification mode

The toolchain SHOULD eventually provide a strict mode.

Example:

```bash
cppl build --require-fully-verified
```

Such a mode may reject builds containing:

- trusted assumptions
- unsafe regions
- unverified FFI
- directly trusted solver results
- unresolved obligations

Strict policy is a tooling decision.

It does not change theorem semantics.

---

# 29. Trust report

C++L MUST eventually provide a machine-readable and human-readable trust report.

Example:

```text
C++L Trust Report

Laws:
  proven:                      241
  trusted:                       0
  unresolved:                    0

Boundaries:
  unsafe regions:                3
  runtime validation sites:      7
  unverified FFI:                0

Trusted components:
  external axioms:               0
  direct solver trust:           0

Kernel:
  version: ...

Compiler:
  version: ...

Target:
  arm64

C++ mode:
  c++23
```

---

# 30. Per-Law trust report

Users should eventually be able to inspect a single theorem.

Example:

```text
Law:
  settlement_closes

Status:
  PROVEN

Proof dependencies:
  arithmetic_conservation        PROVEN
  item_partition                 PROVEN
  payment_provider_contract      TRUSTED

Runtime checks:
  0

Unsafe dependencies:
  0

Trusted closure:
  payment_provider_contract
```

This is more useful than:

```text
✓ verified
```

---

# 31. Kernel versioning

Proof validity may depend on kernel semantics.

Proof artifacts SHOULD therefore identify the kernel version under which they were accepted.

A kernel change that alters proof semantics MUST invalidate incompatible cached proof artifacts.

---

# 32. Core calculus versioning

If the formal core calculus changes, proof artifacts MUST record the relevant calculus version.

A proof accepted under one calculus MUST NOT automatically be assumed valid under a semantically different one.

---

# 33. Mechanized meta-theory

The long-term goal SHOULD include mechanized reasoning about the C++L formal core.

This may establish properties such as:

- consistency
- substitution
- preservation
- normalization of proof-relevant fragments
- termination
- soundness of erasure

The specific mathematics belongs in `FOUNDATIONS.md`, `SPEC.md`, or dedicated formal-semantics documents.

Its relevance here is:

> Mechanization can reduce the amount of core semantics that must be trusted informally.

---

# 34. Compiler verification

A fully verified compiler is not required for C++L to provide value.

However, compiler trust SHOULD be reduced progressively.

Possible progression:

```text
Stage 1
small trusted kernel

Stage 2
mechanized kernel semantics

Stage 3
verified erasure

Stage 4
verified critical lowering passes

Stage 5
stronger end-to-end compiler refinement
```

Each stage reduces the gap between:

```text
source theorem
```

and:

```text
runtime behavior
```

---

# 35. Security-sensitive trust failures

A bug is security-sensitive if it causes C++L to:

- accept invalid proof evidence
- hide a trusted assumption
- incorrectly classify unsafe code as proven
- reuse an invalid cached proof
- accept malformed proof certificates
- remove required runtime checks during erasure
- misreport theorem trust status
- allow backend behavior to invalidate a claimed guarantee without disclosure

Such bugs belong under the policy in `SECURITY.md`.

---

# 36. TCB growth policy

Any change that enlarges the Trusted Computing Base SHOULD require explicit review.

A change from:

```text
independently checked
```

to:

```text
trusted
```

must be justified.

The preferred direction is always:

```text
trusted
    ↓
independently checked
```

not the reverse.

---

# 37. Target mature architecture

The intended mature trust architecture is:

```text
Human / AI source
        ↓
┌─────────────────────────────┐
│ C++L frontend               │
│ elaborator                  │
│ tactics                     │
│ SMT                         │
│ proof search                │
│ AI proof generation         │
└─────────────┬───────────────┘
              │
              │ proof evidence
              ▼
┌─────────────────────────────┐
│ Small trusted proof kernel  │
└─────────────┬───────────────┘
              │
              ▼
            PROVEN
              │
              ▼
┌─────────────────────────────┐
│ Verified / trusted erasure  │
└─────────────┬───────────────┘
              │
              ▼
        ordinary C++
              │
              ▼
        Clang / LLVM
              │
              ▼
      native executable
```

The upper trust chain answers:

```text
Is the proposition logically established?
```

The lower trust chain answers:

```text
Does the native executable preserve the semantics that were established?
```

Both are necessary.

---

# 38. Relationship to other documents

The documentation responsibilities are intentionally separated.

```text
SPEC.md
    what the language means

TRUST.md
    what must be trusted

FOUNDATIONS.md
    mathematical basis

DESIGN.md
    language/compiler design decisions

ARCHITECTURE.md
    implementation components and data flow

COMPATIBILITY.md
    C++ / ABI / toolchain compatibility

SECURITY.md
    vulnerability and disclosure policy

STATUS.md
    what actually exists today
```

This separation is intentional.

`TRUST.md` SHOULD reference these documents instead of duplicating their contents.

---

# 39. Fundamental question

For every Law reported as proven, C++L should eventually be able to answer:

> Why should I believe this?

The answer should reduce to:

```text
formal proposition
+
explicit assumptions
+
kernel-checked proof evidence
+
known trust boundary
+
sound connection to runtime execution
```

and never merely:

```text
because the compiler said so
```

---

# 40. Trust philosophy

C++L does not pursue zero trust.

C++L pursues **explicit, minimal trust**.

Over time:

```text
trusted core
    should shrink

visible assumptions
    should become more precise

automation
    should become more independently checked

runtime semantics
    should become more formally connected

verified C++ coverage
    should grow
```

But the meaning of:

```text
PROVEN
```

must never be weakened to make implementation easier.
