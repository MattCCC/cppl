# C++L Status

**Project status:** Early implementation, first vertical slice  
**Stability:** Experimental  
**Production ready:** No  
**Language specification frozen:** No  
**Proof system frozen:** No  
**ABI guarantees:** No

C++L is currently being designed as a source-compatible C++ superset with first-class Laws, machine-checked proofs, dependent/refinement types, proof erasure, and ordinary native C++ output through Clang/LLVM.

This document exists to distinguish:

```text
what C++L intends to provide
```

from:

```text
what the current implementation actually provides
```

The README, specification, design documents, examples, and roadmap describe the intended language unless this file explicitly marks a capability as implemented.

---

# Status meanings

C++L uses the following status categories.

| Status        | Meaning                                                                                                                  |
| ------------- | ------------------------------------------------------------------------------------------------------------------------ |
| `SPECIFIED`   | Semantics or architecture have been documented, but no conforming implementation exists yet.                             |
| `PROTOTYPE`   | Experimental implementation exists, but semantics or implementation may change substantially.                            |
| `PARTIAL`     | Implementation exists, but important required behavior is still missing.                                                 |
| `IMPLEMENTED` | Intended behavior is implemented and covered by relevant tests.                                                          |
| `VERIFIED`    | Implementation exists and additionally satisfies the project's required formal or equivalence checks for that component. |
| `BLOCKED`     | Work cannot safely proceed until another semantic or architectural issue is resolved.                                    |
| `NOT STARTED` | No implementation work exists yet.                                                                                       |

`IMPLEMENTED` does not automatically mean production-stable.

`VERIFIED` must not be used casually. It means the relevant verification requirement has itself been satisfied.

---

# What the current implementation does

One vertical slice exists and works end to end. Concretely:

```text
cppl -std=c++17|c++20|c++23 main.cpp -o main
```

compiles ordinary supported C++ with no source changes, and for a unit that
declares Laws it:

1. preprocesses with Clang and recognizes `law` and `pure` contextually;
2. projects the unit into an analysis text and a runtime text in one pass;
3. resolves the C++ semantics of the analysis text through libclang;
4. elaborates the resolved semantics into typed VIR;
5. lowers VIR into core definitions and a universally quantified equality goal;
6. proposes evidence and submits it to the trusted kernel;
7. reports `PROVEN` only on kernel acceptance, and fails the build otherwise;
8. checks that erasure only deleted text, and hands the runtime program to Clang.

The verified fragment is deliberately small: a Law is one equality between two
built-in integer expressions, universally quantified over its parameters, over
functions declared `pure` whose bodies are a single `return` of a modeled
expression. Everything else is reported as unsupported and produces no
obligation. See `ARCHITECTURE.md` 97 for the implemented structure and
`TRUST.md` 41 for what must be trusted today.

This slice does **not** implement induction, dependent types, refinement types,
contracts, ghost state, `unsafe`, `trusted`, written proofs, solvers, proof
caching, or any verification of the C++ memory model. Those remain `SPECIFIED`
below.

---

# Current project state

Outside that slice, C++L should be considered primarily a **language and verification-system specification**.

The following documents define the intended direction:

```text
README.md
SPEC.md
DESIGN.md
FOUNDATIONS.md
TRUST.md
COMPATIBILITY.md
ROADMAP.md
SECURITY.md
ACKNOWLEDGEMENTS.md
STATUS.md
```

Unless implementation evidence says otherwise, documented features should be treated as:

```text
SPECIFIED
```

rather than:

```text
IMPLEMENTED
```

---

# Current milestone

The first vertical slice is in place. The next priority is to widen the formal
core deliberately rather than to widen the language surface: written proofs,
preconditions as implications, and the obligations that justify signed
arithmetic are each a prerequisite for the Laws people will actually want to
state.

Original target, for reference:

```text
formal core
    ↓
trusted kernel
    ↓
verification IR
    ↓
Clang semantic bridge
    ↓
C++L syntax
    ↓
proof erasure
    ↓
ordinary C++
```

The project should not claim broad language implementation before the proof semantics and trust model are sufficiently defined.

---

# Core language status

| Capability                   | Status      |
| ---------------------------- | ----------- |
| C++L language mission        | `SPECIFIED` |
| Genuine C++ superset model   | `PROTOTYPE` |
| `law` declarations           | `PROTOTYPE` |
| `ensures` clauses on laws    | `PROTOTYPE` |
| `proves` clauses             | `SPECIFIED` |
| proof declarations           | `SPECIFIED` |
| proposition types            | `PROTOTYPE` |
| universal quantification     | `PROTOTYPE` |
| existential quantification   | `SPECIFIED` |
| dependent types              | `SPECIFIED` |
| refinement types             | `SPECIFIED` |
| algebraic data types         | `SPECIFIED` |
| proof-aware pattern matching | `SPECIFIED` |
| impossible-state elimination | `SPECIFIED` |
| definitional equality        | `PROTOTYPE` |
| propositional equality       | `PROTOTYPE` |
| normalization                | `PROTOTYPE` |
| structural induction         | `SPECIFIED` |
| well-founded recursion       | `SPECIFIED` |
| termination checking         | `SPECIFIED` |
| `expects`                    | `SPECIFIED` |
| `ensures` on functions       | `SPECIFIED` |
| `pure`                       | `PROTOTYPE` |
| `verified`                   | `SPECIFIED` |
| `ghost`                      | `SPECIFIED` |
| `unsafe`                     | `SPECIFIED` |
| `trusted`                    | `SPECIFIED` |
| `decreases`                  | `SPECIFIED` |
| proof erasure                | `PROTOTYPE` |

---

# Proof system status

| Capability                           | Status        |
| ------------------------------------ | ------------- |
| Core proof calculus                  | `SPECIFIED`   |
| Propositions-as-types model          | `SPECIFIED`   |
| Trusted proof kernel architecture    | `PROTOTYPE`   |
| Kernel implementation                | `PROTOTYPE`   |
| Proof-term representation            | `PROTOTYPE`   |
| Proof-term binary/serialized format  | `NOT STARTED` |
| Equality checking                    | `PROTOTYPE`   |
| Substitution                         | `PROTOTYPE`   |
| Dependent application                | `NOT STARTED` |
| Universal introduction               | `PROTOTYPE`   |
| Universal elimination                | `NOT STARTED` |
| Existential introduction/elimination | `NOT STARTED` |
| Induction checking                   | `NOT STARTED` |
| Refinement introduction/elimination  | `NOT STARTED` |
| Normalization engine                 | `PROTOTYPE`   |
| Termination checker                  | `NOT STARTED` |
| Proof certificate format             | `NOT STARTED` |
| Kernel fuzzing                       | `NOT STARTED` |
| Kernel property testing              | `NOT STARTED` |
| Kernel rejection tests               | `PROTOTYPE`   |

The kernel implements two rules, reflexivity and universal introduction, over
propositions built from equality and universal quantification. Its terms are
variables, machine-integer literals, applications of admitted definitions and
one primitive, wrapping addition. It admits no recursion, which is why it needs
no termination checker yet (`ARCHITECTURE.md` 97.7).
| Mechanized core calculus             | `NOT STARTED` |
| Meta-theory / soundness proofs       | `NOT STARTED` |

---

# Mathematical foundation status

| Area                            | Status        |
| ------------------------------- | ------------- |
| Curry–Howard foundation         | `SPECIFIED`   |
| Dependent type theory direction | `SPECIFIED`   |
| Inductive reasoning             | `SPECIFIED`   |
| Equality model                  | `SPECIFIED`   |
| Hoare-style contracts           | `SPECIFIED`   |
| Weakest-precondition reasoning  | `SPECIFIED`   |
| Refinement typing               | `SPECIFIED`   |
| SMT-assisted reasoning          | `SPECIFIED`   |
| Exact core calculus             | `NOT STARTED` |
| Formal typing rules             | `NOT STARTED` |
| Formal reduction rules          | `NOT STARTED` |
| Formal substitution rules       | `NOT STARTED` |
| Formal erasure theorem          | `NOT STARTED` |
| Mechanized soundness model      | `NOT STARTED` |

---

# C++ integration status

| Capability                               | Status        |
| ---------------------------------------- | ------------- |
| Clang-based C++ semantic integration     | `PROTOTYPE`   |
| Clang AST bridge                         | `PROTOTYPE`   |
| Source mapping                           | `PROTOTYPE`   |
| C++ name lookup reuse                    | `PROTOTYPE`   |
| C++ overload-resolution reuse            | `PROTOTYPE`   |
| C++ template interoperability            | `SPECIFIED`   |
| C++ `constexpr` interoperability         | `SPECIFIED`   |
| C++ exceptions model                     | `SPECIFIED`   |
| C++ RTTI model                           | `SPECIFIED`   |
| C++ ABI preservation                     | `SPECIFIED`   |
| libc++ interoperability                  | `SPECIFIED`   |
| Existing native library interoperability | `SPECIFIED`   |
| C interoperability                       | `SPECIFIED`   |
| Objective-C++ interoperability           | `SPECIFIED`   |
| JNI interoperability                     | `SPECIFIED`   |
| N-API interoperability                   | `SPECIFIED`   |
| WASM target compatibility                | `SPECIFIED`   |

---

# C++ standard compatibility

Current design target:

```text
initial implementation:
C++17+

preferred primary modes:
C++20+
C++23+
```

The proof language should remain as independent as practical from the selected underlying C++ version.

Conceptually:

```bash
cppl -std=c++17
cppl -std=c++20
cppl -std=c++23
```

Current status:

| C++ mode                        | Status        |
| ------------------------------- | ------------- |
| C++98/03                        | `NOT STARTED` |
| C++11                           | `NOT STARTED` |
| C++14                           | `NOT STARTED` |
| C++17                           | `PROTOTYPE`   |
| C++20                           | `PROTOTYPE`   |
| C++23                           | `PROTOTYPE`   |
| Newer Clang-supported standards | `SPECIFIED`   |

No C++ compatibility level should be claimed as implemented until compatibility tests exist.

The conformance suite covers each of C++17, C++20 and C++23 with an ordinary
program and with a program that uses C++L words as ordinary identifiers, and
checks that the runtime program emitted for a C++17 target compiles as C++17 on
its own. That is enough for `PROTOTYPE`, not for `IMPLEMENTED`: templates,
modules, concepts and ABI-sensitive constructs are not yet covered.

---

# Verification IR status

| Capability                     | Status        |
| ------------------------------ | ------------- |
| Verification IR architecture   | `PROTOTYPE`   |
| VIR type representation        | `PROTOTYPE`   |
| VIR expression representation  | `PROTOTYPE`   |
| VIR provenance                 | `PROTOTYPE`   |
| VIR proposition representation | `NOT STARTED` |
| VIR control-flow model         | `NOT STARTED` |
| VIR state model                | `NOT STARTED` |
| VIR contract model             | `NOT STARTED` |
| VIR proof obligations          | `PROTOTYPE`   |
| VIR unsafe/trust annotations   | `NOT STARTED` |
| VIR serialization              | `NOT STARTED` |
| VIR deterministic hashing      | `NOT STARTED` |

Obligation identities are content-derived today, but they are computed from the
core representation rather than from VIR, so VIR hashing has no consumer yet and
is not implemented.

---

# Contracts status

| Capability                              | Status        |
| --------------------------------------- | ------------- |
| Preconditions                           | `SPECIFIED`   |
| Postconditions                          | `SPECIFIED`   |
| Function invariants                     | `SPECIFIED`   |
| Loop invariants                         | `SPECIFIED`   |
| Verification-condition generation       | `NOT STARTED` |
| Weakest-precondition engine             | `NOT STARTED` |
| Contract composition                    | `NOT STARTED` |
| Contract reuse across translation units | `NOT STARTED` |

---

# Refinement status

| Capability                              | Status        |
| --------------------------------------- | ------------- |
| Predicate refinements                   | `SPECIFIED`   |
| Static refinement construction          | `SPECIFIED`   |
| Runtime checked refinement construction | `SPECIFIED`   |
| Refinement elimination                  | `SPECIFIED`   |
| Refinement subtyping                    | `NOT STARTED` |
| Arithmetic refinement solving           | `NOT STARTED` |
| Bitvector refinements                   | `NOT STARTED` |
| User-defined refinement predicates      | `SPECIFIED`   |

---

# Inductive types status

| Capability                 | Status        |
| -------------------------- | ------------- |
| Algebraic data type syntax | `SPECIFIED`   |
| Constructor typing         | `SPECIFIED`   |
| Exhaustive matching        | `SPECIFIED`   |
| Pattern-based refinement   | `SPECIFIED`   |
| Structural induction       | `SPECIFIED`   |
| Impossible branches        | `SPECIFIED`   |
| Runtime lowering           | `NOT STARTED` |
| ABI rules                  | `NOT STARTED` |

---

# Termination status

| Capability                   | Status        |
| ---------------------------- | ------------- |
| Structural recursion         | `SPECIFIED`   |
| Explicit `decreases` clauses | `SPECIFIED`   |
| Well-founded recursion       | `SPECIFIED`   |
| Termination checker          | `NOT STARTED` |
| Mutual recursion policy      | `NOT STARTED` |
| Termination diagnostics      | `NOT STARTED` |
| Nontermination isolation     | `SPECIFIED`   |

Proof-producing nontermination must never be accepted as proof evidence.

---

# C++ safety status

The C++ safety model is essential to C++L's eventual guarantees.

A theorem about C++ execution is not meaningful if undefined behavior or incorrect machine semantics can invalidate the assumptions used by the proof.

| Capability                            | Status        |
| ------------------------------------- | ------------- |
| Signed-overflow reasoning             | `SPECIFIED`   |
| Division-by-zero reasoning            | `SPECIFIED`   |
| Shift validity                        | `SPECIFIED`   |
| Bounds checking                       | `SPECIFIED`   |
| Nullability reasoning                 | `SPECIFIED`   |
| Object lifetime model                 | `SPECIFIED`   |
| Reference validity                    | `SPECIFIED`   |
| Pointer arithmetic                    | `SPECIFIED`   |
| Pointer provenance                    | `SPECIFIED`   |
| Aliasing                              | `SPECIFIED`   |
| Move semantics                        | `SPECIFIED`   |
| Destruction                           | `SPECIFIED`   |
| Uninitialized reads                   | `SPECIFIED`   |
| Cast validity                         | `SPECIFIED`   |
| Data race reasoning                   | `SPECIFIED`   |
| Implementation of C++ safety analysis | `NOT STARTED` |

Unsupported behavior must eventually be:

```text
rejected
or
unsafe
or
trusted
```

never silently verified.

---

# Memory model status

| Capability                     | Status        |
| ------------------------------ | ------------- |
| Lifetime model                 | `SPECIFIED`   |
| Ownership model                | `SPECIFIED`   |
| Borrowing/equivalent reasoning | `SPECIFIED`   |
| Aliasing rules                 | `SPECIFIED`   |
| Pointer provenance             | `SPECIFIED`   |
| Mutation model                 | `SPECIFIED`   |
| Move semantics                 | `SPECIFIED`   |
| Destructor semantics           | `SPECIFIED`   |
| Standard container models      | `NOT STARTED` |
| Memory verifier                | `NOT STARTED` |

The exact formal memory calculus is not yet frozen.

---

# Machine arithmetic status

| Capability                       | Status        |
| -------------------------------- | ------------- |
| Mathematical integer distinction | `SPECIFIED`   |
| Fixed-width integer semantics    | `SPECIFIED`   |
| Checked arithmetic               | `SPECIFIED`   |
| Wrapping arithmetic              | `SPECIFIED`   |
| Saturating arithmetic            | `SPECIFIED`   |
| Big integer proof domain         | `SPECIFIED`   |
| Bitvector solver integration     | `NOT STARTED` |
| Overflow diagnostics             | `NOT STARTED` |

---

# Unsafe and trusted boundary status

| Capability                          | Status        |
| ----------------------------------- | ------------- |
| `unsafe` syntax                     | `SPECIFIED`   |
| `trusted` syntax                    | `SPECIFIED`   |
| Unsafe-to-verified transition rules | `SPECIFIED`   |
| Trust propagation                   | `SPECIFIED`   |
| Assumption closure                  | `SPECIFIED`   |
| Trust reporting                     | `SPECIFIED`   |
| Trust report implementation         | `NOT STARTED` |

The intended verification statuses are:

```text
PROVEN
TRUSTED
RUNTIME-CHECKED
UNSAFE
UNVERIFIED
UNRESOLVED
```

---

# FFI status

| Boundary                     | Status        |
| ---------------------------- | ------------- |
| C                            | `SPECIFIED`   |
| Objective-C++                | `SPECIFIED`   |
| JNI                          | `SPECIFIED`   |
| N-API                        | `SPECIFIED`   |
| WASM imports/exports         | `SPECIFIED`   |
| Inline assembly              | `SPECIFIED`   |
| OS APIs                      | `SPECIFIED`   |
| Verified FFI contract format | `NOT STARTED` |
| FFI contract checker         | `NOT STARTED` |

Foreign code must not automatically count as verified.

---

# Proof erasure status

| Capability                       | Status        |
| -------------------------------- | ------------- |
| Proof erasure model              | `SPECIFIED`   |
| Ghost erasure model              | `SPECIFIED`   |
| Dependent argument erasure       | `SPECIFIED`   |
| Erasure implementation           | `PROTOTYPE`   |
| Runtime-equivalence tests        | `PARTIAL`     |
| ABI-equivalence tests            | `NOT STARTED` |
| Formal erasure correctness proof | `NOT STARTED` |

Erasure currently removes law declarations and the `pure` specifier, and the
implementation checks a strong property rather than asserting success: the
runtime program must be the analysed program with formal spans blanked, with
byte positions and line numbering unchanged. Equivalence is therefore
established structurally for the constructs implemented, not proven in general.

C++L's intended mature pipeline is:

```text
verified C++L
    ↓
proof erasure
    ↓
ordinary C++
    ↓
Clang / LLVM
```

---

# Runtime status

C++L does not intend to introduce a mandatory theorem runtime.

| Runtime feature                  | Status        |
| -------------------------------- | ------------- |
| Dedicated proof VM               | `NOT PLANNED` |
| Runtime theorem checker          | `NOT PLANNED` |
| Mandatory C++L garbage collector | `NOT PLANNED` |
| Mandatory alternate runtime      | `NOT PLANNED` |
| Runtime refinement validation    | `SPECIFIED`   |
| Ordinary C++ execution           | `SPECIFIED`   |
| Clang/LLVM native output         | `SPECIFIED`   |

Proofs should normally disappear before runtime.

---

# Automation status

| Capability                       | Status        |
| -------------------------------- | ------------- |
| Definitional-equality strategy   | `PROTOTYPE`   |
| `proof auto`                     | `SPECIFIED`   |
| Simplification                   | `SPECIFIED`   |
| Rewriting                        | `SPECIFIED`   |
| Arithmetic automation            | `SPECIFIED`   |
| Contradiction solving            | `SPECIFIED`   |
| Induction tactic                 | `SPECIFIED`   |
| cvc5 integration                 | `NOT STARTED` |
| Z3 integration                   | `NOT STARTED` |
| Proof certificates               | `NOT STARTED` |
| Independent certificate checking | `NOT STARTED` |
| Counterexample extraction        | `NOT STARTED` |

Automation must not weaken the meaning of `PROVEN`.

---

# AI integration status

| Capability                   | Status        |
| ---------------------------- | ------------- |
| AI-oriented proof workflow   | `SPECIFIED`   |
| Structured proof goals       | `SPECIFIED`   |
| Machine-readable diagnostics | `SPECIFIED`   |
| Counterexample feedback      | `SPECIFIED`   |
| AI patch loop                | `NOT STARTED` |
| AI-generated proof checking  | `SPECIFIED`   |
| AI as trusted authority      | `NOT PLANNED` |

AI output must always be independently verified.

---

# Developer tooling status

| Tool                   | Status        |
| ---------------------- | ------------- |
| Clang-compatible driver | `PROTOTYPE`   |
| `cppl build`            | `SPECIFIED`   |
| `cppl check`            | `SPECIFIED`   |
| `cppl prove`            | `SPECIFIED`   |
| `cppl explain`          | `SPECIFIED`   |
| `cppl trust-report`     | `PARTIAL`     |
| LSP                     | `NOT STARTED` |
| IDE proof goals         | `NOT STARTED` |
| Proof navigation        | `NOT STARTED` |
| Counterexample UI       | `NOT STARTED` |
| Structured diagnostics  | `PROTOTYPE`   |

The driver is Clang-compatible rather than subcommand-based: `cppl` takes the
arguments `clang++` takes. Trust reporting exists as `--cppl-trust-report`; the
subcommand forms above are not implemented.

---

# Standard library verification status

| Area                       | Status        |
| -------------------------- | ------------- |
| Primitive types            | `NOT STARTED` |
| `std::array`               | `NOT STARTED` |
| `std::span`                | `NOT STARTED` |
| `std::optional`            | `NOT STARTED` |
| `std::variant`             | `NOT STARTED` |
| `std::vector`              | `NOT STARTED` |
| `std::string`              | `NOT STARTED` |
| Smart pointers             | `NOT STARTED` |
| Standard algorithms        | `NOT STARTED` |
| libc++ specification layer | `NOT STARTED` |

---

# Concurrency status

| Capability                          | Status        |
| ----------------------------------- | ------------- |
| Sequential semantics                | `SPECIFIED`   |
| Thread model                        | `NOT STARTED` |
| Atomic model                        | `NOT STARTED` |
| Memory-order reasoning              | `NOT STARTED` |
| Race-freedom proofs                 | `NOT STARTED` |
| Concurrent invariants               | `NOT STARTED` |
| Verified synchronization primitives | `NOT STARTED` |

Until concurrency semantics exist, concurrency must not be silently treated using sequential reasoning.

---

# Proof caching status

| Capability                            | Status        |
| ------------------------------------- | ------------- |
| Incremental verification architecture | `SPECIFIED`   |
| Proof dependency graph                | `NOT STARTED` |
| Content-addressed proof cache         | `NOT STARTED` |
| Semantic invalidation                 | `NOT STARTED` |
| Deterministic proof artifacts         | `NOT STARTED` |
| Cross-build proof reuse               | `NOT STARTED` |

---

# Trust model status

| Capability                  | Status        |
| --------------------------- | ------------- |
| Explicit TCB model          | `PROTOTYPE`   |
| Small-kernel architecture   | `PROTOTYPE`   |
| Hidden axioms forbidden     | `PROTOTYPE`   |
| Trust transitivity          | `SPECIFIED`   |
| Solver trust reporting      | `SPECIFIED`   |
| FFI trust reporting         | `NOT STARTED` |
| Per-Law assumption closure  | `NOT STARTED` |
| Trust-report implementation | `PARTIAL`     |

The implemented TCB is stated in `TRUST.md` 41. There are no axioms and no
trusted declarations, because no mechanism to introduce one exists yet: a
`trusted law` is refused rather than accepted. The trust report prints counts it
can substantiate, and says *not analysed* where C++L does not yet look.

See [TRUST.md](TRUST.md).

---

# Security status

| Capability                                           | Status        |
| ---------------------------------------------------- | ------------- |
| Proof-soundness issues classified as security issues | `SPECIFIED`   |
| Responsible disclosure policy                        | `SPECIFIED`   |
| Kernel fuzzing                                       | `NOT STARTED` |
| Parser fuzzing                                       | `NOT STARTED` |
| Proof-certificate fuzzing                            | `NOT STARTED` |
| Erasure fuzzing                                      | `NOT STARTED` |
| Reproducible release metadata                        | `NOT STARTED` |

See [SECURITY.md](SECURITY.md).

---

# Documentation status

| Document                   | Status        |
| -------------------------- | ------------- |
| `README.md`                | `SPECIFIED`   |
| `SPEC.md`                  | `SPECIFIED`   |
| `docs/GRAMMAR.md`          | `SPECIFIED`   |
| `docs/CURRENT_STATUS.md`   | `IMPLEMENTED` |
| `DESIGN.md`                | `SPECIFIED`   |
| `FOUNDATIONS.md`           | `SPECIFIED`   |
| `TRUST.md`                 | `SPECIFIED`   |
| `ACKNOWLEDGEMENTS.md`      | `SPECIFIED`   |
| `ROADMAP.md`               | `SPECIFIED`   |
| `COMPATIBILITY.md`         | `SPECIFIED`   |
| `SECURITY.md`              | `SPECIFIED`   |
| `CONTRIBUTING.md`          | `SPECIFIED`   |
| `STATUS.md`                | `SPECIFIED`   |
| RFC process                | `SPECIFIED`   |
| Formal semantics reference | `NOT STARTED` |
| C++ memory-model reference | `NOT STARTED` |
| Kernel reference           | `NOT STARTED` |
| Erasure reference          | `NOT STARTED` |

---

# What C++L does not currently claim

Until implementation reaches the appropriate status, C++L does **not** claim:

- production readiness
- verified C++ compatibility
- a completed proof kernel
- a completed dependent type checker
- completed theorem proving
- completed refinement solving
- sound C++ pointer verification
- sound concurrency verification
- verified signed arithmetic
- verified standard-library implementations
- completed proof erasure
- completed Clang integration
- completed SMT integration
- a stable language specification
- a stable ABI
- a stable proof artifact format

Documentation of a feature is not evidence that the feature has been implemented.

---

# What may be demonstrated early

Early prototypes may demonstrate narrow vertical slices such as:

```text
law
    ↓
proof obligation
    ↓
kernel
    ↓
PROVEN
```

or:

```text
refinement
    ↓
SMT
    ↓
checked evidence
```

or:

```text
C++L
    ↓
erase proof syntax
    ↓
C++
    ↓
clang++
```

Such prototypes should be marked `PROTOTYPE` rather than presented as broad language support.

The first of these now exists: see *What the current implementation does* above.
It is marked `PROTOTYPE` throughout this document, and the fragment it verifies
is stated explicitly rather than implied.

---

# Promotion rules

A feature should move from:

```text
SPECIFIED
→
PROTOTYPE
```

when executable implementation work exists.

A feature should move from:

```text
PROTOTYPE
→
PARTIAL
```

when the core implementation works but defined semantics remain incomplete.

A feature should move from:

```text
PARTIAL
→
IMPLEMENTED
```

only when:

- required semantics are implemented
- positive tests pass
- negative tests pass
- regression tests exist
- diagnostics are adequate
- trust impact is documented

A feature should move from:

```text
IMPLEMENTED
→
VERIFIED
```

only when the project's additional formal verification requirements for that component have been satisfied.

---

# No silent status promotion

Features must not be marked `IMPLEMENTED` merely because:

- a demo works
- a test passes
- an AI generated working code
- a solver returned `SAT` or `UNSAT`
- Clang accepted generated C++
- one proof example succeeded

Status reflects the defined semantics, not the best-case example.

---

# Current release policy

Before the first stable release, version numbers should communicate experimental status.

Examples:

```text
0.1.0
0.2.0
0.x
```

A `1.0.0` release should mean that the project's declared V1 guarantees are implemented and sufficiently stable to be relied upon.

---

# V1 target

C++L V1 should provide a coherent end-to-end implementation of:

```text
formal Laws
+
proofs
+
dependent/indexed relationships
+
refinement types
+
equality
+
induction
+
termination
+
contracts
+
explicit trust boundaries
+
C++ safety obligations
+
proof erasure
+
ordinary native C++ output
```

The implementation may initially support only a defined subset of difficult C++ constructs inside verified regions.

That subset must be explicit.

Unsupported C++ may remain executable as ordinary/unverified C++.

It must never silently count as formally verified.

---

# Definition of `PROVEN`

A Law may be reported as:

```text
PROVEN
```

only when:

1. its proposition is well-formed;
2. all required proof obligations have been discharged;
3. proof evidence is accepted by the trusted kernel or equivalent trusted foundation;
4. all dependencies and assumptions are known;
5. relevant unsafe dependencies are visible;
6. runtime semantics used by the proof are compatible with the executable semantics;
7. no unresolved obligation is being silently ignored.

A proven theorem may still depend on explicit trusted assumptions.

Those assumptions must remain visible.

---

# Example future status report

A mature build may eventually produce:

```text
C++L Verification Status

Source files:                  142

Laws:
  proven:                      518
  trusted:                       1
  unresolved:                    0

Proof dependencies:
  fully assumption-free:       503
  depend on trusted axioms:     15

Runtime:
  unsafe regions:                4
  runtime validation sites:     11
  unverified FFI boundaries:     0

Kernel:
  status: VERIFIED

Erasure:
  status: VERIFIED

C++ mode:
  c++23

Native backend:
  Clang / LLVM
```

That is the level of transparency C++L should eventually provide.

---

# Guiding rule

C++L must never confuse:

```text
designed
```

with:

```text
implemented
```

or:

```text
implemented
```

with:

```text
proven sound
```

The credibility of a proof-oriented language depends on maintaining those distinctions precisely.
