# C++L Roadmap

C++L is intended to become a genuine proof-aware C++ superset, not merely an annotation system around C++.

The roadmap therefore prioritizes semantic correctness before language breadth.

This document describes implementation order, not a reduction in the final language goal.

---

# V1 definition

C++L V1 is complete when the language can demonstrate the entire core workflow:

```text
formal Law
    ↓
C++L implementation
    ↓
proof obligations
    ↓
mechanically checked proof
    ↓
proof erasure
    ↓
ordinary C++
    ↓
Clang / LLVM
    ↓
native binary
```

V1 must demonstrate that this workflow is sound enough to build real software against.

---

# Phase 0 - Formal core

Define the mathematical and language core before optimizing tooling.

Deliverables:

- core proposition syntax
- proof-term representation
- equality
- dependent function types
- universal quantification
- existential quantification
- case analysis over C++ types
- induction principles
- proof-only mathematical domains
- refinement propositions
- normalization semantics
- termination semantics
- explicit trusted assumptions
- proof erasure semantics

Required documents:

- `SPEC.md`
- `FOUNDATIONS.md`
- `TRUST.md`

Exit criterion:

> The core calculus can be described independently of the production compiler.

---

# Phase 1 - Trusted kernel

Implement the smallest useful proof checker.

Responsibilities:

- validate core types
- validate propositions
- validate proof terms
- validate equality
- validate substitution
- validate dependent application
- validate quantification
- validate induction
- validate refinements
- validate normalization evidence

Non-responsibilities:

- C++ parsing
- optimization
- AI integration
- IDE features
- full automation
- code generation

Exit criterion:

```text
valid proof     → accepted
invalid proof   → rejected
malformed proof → rejected
```

with extensive adversarial tests.

---

# Phase 2 - Verification IR

Create a stable verification intermediate representation.

The VIR should separate:

```text
C++ syntax
from
logical meaning
```

The VIR should model:

- values
- types
- propositions
- control flow
- state
- function contracts
- proof obligations
- trusted assumptions
- unsafe boundaries

Exit criterion:

> Simple C++ functions can be represented as proof obligations without depending on surface syntax.

---

# Phase 3 - Clang bridge

Integrate with Clang rather than reimplementing C++.

Required capabilities:

- source locations
- Clang AST access
- resolved C++ types
- overload resolution results
- template instantiation information
- constexpr results where soundly usable
- object lifetime information where available
- control-flow information
- diagnostics mapping

Exit criterion:

> Ordinary supported C++ can be consumed using Clang semantics while C++L retains its own verification semantics.

---

# Phase 4 - C++L extension syntax

Implement actual C++L syntax.

Initial constructs:

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
invariant
cases
induction
```

`cases` and `induction` are proof statements only. C++L adds no data-type declarations or runtime pattern matching.

Exit criterion:

> C++L is syntactically a genuine C++ superset rather than an annotation convention.

---

# Phase 5 - Laws and contracts

Current sequence: single-return verified functions and verified-call contract
composition, conditionals with path obligations and integer comparisons, and
locals, assignments and their updates, unsigned machine arithmetic with
kernel-checked linear order reasoning, and `while`/`for` loops with explicit
invariants (partial correctness) are prototyped. The formal language is now
under way: explicit equality, universal quantification, implication, conjunction,
disjunction and equivalence are prototyped as propositions, and formal operands
compose through them. Refinement and indexed refinement types are prototyped: they
declare a verification-level type over an ordinary C++ base type, lower to the
alias the program keeps, make membership an obligation wherever a value enters one -
a declaration, an assignment, a verified call's argument, a return - and treat
crossing between two of them as the implication between their predicates. Next in
that core are existential quantification with its proof surface,
induction and termination. Scalar reference storage, void functions, alias
invalidation and verified call post-state are now prototyped together with their
refinement crossings. General object and pointer memory reasoning, signed
arithmetic with overflow obligations, and SMT remain incomplete. Trust propagation
is implemented within a translation unit: a proof may name a `trusted law`, is
checked relative to it, and the trust report lists every proven claim with the
trusted laws it rests on. Contracts, with that closure, now cross translation
units through verification interfaces (RFC 0017, `SPEC.md` Annex L.2.1), which
record what a unit proved without carrying evidence to re-check; transporting
evidence, and authenticating interfaces, come later. Trusted memory propositions
are not started. See `STATUS.md` for the supported fragment.

Implement:

- Laws
- preconditions
- postconditions
- proof declarations
- proof reuse
- dependency tracking
- trust propagation

Example:

```cpp
law identity<T>(T x)
    proves (id(x) == x);
```

Exit criterion:

> A failing implementation cannot compile as fully verified when it violates a Law.

---

# Phase 6 - Dependent and refinement types

Implement practical value-dependent typing.

Initial target:

```cpp
Vector<T, n>
Fin<n>

type Percentage = int where self >= 0 && self <= 100;
```

Required:

- introduction rules
- elimination rules
- conversion
- runtime validation boundaries
- erased dependent information where applicable

Exit criterion:

> Value-dependent relationships participate in real type checking and proof checking.

---

# Phase 7 - Induction and recursive proofs

Case analysis is implemented as a representation-independent engine over
decomposition providers (RFC 0013), and the formal value model that the
providers needed — abstract nominal values with checked component projection —
is implemented with it. Scoped enumerations, `std::variant`, `std::optional`,
`std::expected`, pointers and every product form (records, `std::pair`,
`std::tuple`, `std::array`, built-in arrays) each decompose, and nesting
composes across them generically. `cases` and `decompose` also split a verified
body's path over values that can change, with case facts bound to the version
they were read at, so the storage model's own versions invalidate them
(`SPEC.md` 20.7). `STATUS.md` records the details.

What decomposition still leaves at `PROTOTYPE`: omitted impossible cases and
impossible runtime paths. `omit label by contradiction e;` (`GRAMMAR.md` 5.7)
discharges a case through the ordinary proof system rather than letting a
provider guess it, and `contradiction e;` in a verified body claims a runtime
path cannot occur (`VERIFIED-045`). Each is an obligation of its own, under its
own origin (`SPEC.md` `CASE-012`), discharged by one mechanism. A contradiction
closes a goal of any shape, structured-value equalities included, by falsity
elimination.

The reach of a split grows with what verified bodies model, not with the case
engine: writing a `std::optional` or `std::variant`, reassigning a pointer local,
and reading a local aggregate as one value are storage-model work, and a split
over them follows as soon as they exist.

None of this delivers induction or recursive proof admission.

Implement, over ordinary C++ types:

- induction (`induction`) for machine integers and well-founded C++ structures
- impossible runtime paths beyond the `PROTOTYPE` claim (`VERIFIED-045`), such as a claim over values a path learns through a pointer
- recursive proofs
- termination checking
- proof-only mathematical domains `@N`, `@Z`, `@Seq<T>`, `@Set<T>`, `@Map<K, V>`, and explicit conversions into them from machine values

Example:

```cpp
proof add_zero(unsigned x)
    proves (add(x, 0u) == x)
{
    induction x;
}
```

Exit criterion:

> C++L can prove universal properties of C++ values without enumeration and without redeclaring C++ types.

---

# Phase 8 - Imperative verification

Add verification-condition generation for ordinary systems-style code.

Required:

- assignments
- branching
- loops
- function calls
- mutable state
- invariants
- pre/postconditions
- weakest-precondition or equivalent reasoning

Member functions: a statically bound member function is prototyped as a verified
callable over its implicit object's storage, with member writes, aliasing and
member calls on the common storage model (`docs/rfcs/0018-verified-member-functions.md`).
Next are `old(...)` over the implicit object, override substitutability for
virtual functions, constructors and destructors, and member functions of class
templates.

Exit criterion:

> Useful imperative C++ can be verified without rewriting it as a purely functional language.

---

# Phase 9 - Machine arithmetic

Implement correct reasoning for:

- fixed-width integers
- overflow
- shifts
- conversions
- signedness
- division
- bitvectors

Provide explicit semantics where useful:

```cpp
Checked<T>
Wrapping<T>
Saturating<T>
```

Exit criterion:

> Mathematical proofs cannot silently assume arithmetic semantics different from runtime C++.

---

# Phase 10 - Memory model

This is one of the most important systems milestones.

Implement proof rules for:

- object lifetime
- references
- pointers
- nullability
- pointer provenance
- ownership
- borrowing or equivalent restrictions
- aliasing
- mutation
- moves
- destruction
- arrays
- bounds

Exit criterion:

> Verified memory safety claims correspond to actual C++ object and memory behavior.

---

# Phase 11 - UB verification

Create explicit obligations for relevant C++ undefined behavior.

At minimum:

- signed overflow
- invalid shifts
- division by zero
- null dereference
- bounds
- lifetime
- use-after-free
- invalid references
- uninitialized reads
- invalid pointer arithmetic
- invalid casts
- aliasing violations

Exit criterion:

> Verified regions cannot silently depend on UB.

---

# Phase 12 - Automation

Add proof automation after the formal core is stable.

Potential automation:

```cpp
proof auto;
proof simplify;
proof arithmetic;
proof rewrite theorem;
proof induction x;
proof contradiction;
```

Integrations MAY include:

- cvc5
- Z3
- custom simplifier
- congruence closure
- arithmetic solvers
- bitvector solvers

Exit criterion:

> Common proof obligations require minimal manual proof code without weakening the kernel model.

---

# Phase 13 - Proof certificates

Reduce dependency on trusted automation.

Preferred direction:

```text
solver
    ↓
proof/certificate
    ↓
independent checker
```

Exit criterion:

> Solver trust is minimized and clearly reported.

---

# Phase 14 - Erasure

Implement verified proof erasure.

Erase:

- proofs
- ghost state
- theorem-only values
- compile-time-only indices where runtime representation does not require them

Required properties:

- no change in observable runtime behavior
- no removed runtime validation
- no new UB
- stable ABI where promised

Exit criterion:

> Verified C++L produces clean ordinary C++ suitable for normal Clang/LLVM optimization.

---

# Phase 15 - Standard-library specifications

Provide verified models for common abstractions.

Priority candidates:

- primitive integer types
- `std::array`
- `std::span`
- `std::optional`
- `std::variant`
- `std::vector`
- strings
- smart pointers
- standard algorithms

Exit criterion:

> Real applications can use common C++ facilities without dropping immediately into unverified code.

---

# Phase 16 - FFI

Define verified interface specifications for:

- C
- Objective-C++
- JNI
- N-API
- WASM
- operating-system APIs

Exit criterion:

> Foreign calls have explicit proof/trust boundaries.

---

# Phase 17 - Concurrency

Only after the sequential memory model is solid.

Required design areas:

- threads
- atomics
- memory ordering
- synchronization
- race freedom
- ownership transfer
- concurrent invariants

Exit criterion:

> C++L does not apply unsound sequential reasoning to concurrent C++.

---

# Phase 18 - Developer tooling

Implement:

```bash
cppl build
cppl check
cppl prove
cppl explain
cppl trust-report
```

Also:

- LSP semantic tokens for a range or as a delta, and interactive proof state
  (incremental sync, diagnostics, formatting, code actions, hover, completion,
  signature help, definition, declaration, type definition, implementation,
  references and highlights, the document outline, folding and selection
  ranges, inlay hints, navigation from proof statements to what they name,
  verification status and each obligation's goal, background compiles, a
  workspace index for workspace symbols and references in closed files, and
  rename are implemented; see `STATUS.md`)
- IDE diagnostics
- proof goals
- counterexamples
- quick fixes
- theorem navigation
- proof dependency graph

Exit criterion:

> Verification is practical during normal development.

---

# Phase 19 - AI interface

Expose machine-readable proof obligations.

Required:

- structured goal representation
- structured diagnostics
- available theorem list
- assumptions
- proof state
- counterexamples
- safe patch loop

Conceptually:

```text
AI writes implementation
    ↓
C++L returns proof obligations
    ↓
AI repairs implementation/proof
    ↓
kernel independently verifies
```

The AI itself is never trusted.

---

# Phase 20 - Performance

Optimize only after semantics are stable.

Targets:

- incremental verification
- content-addressed proof cache
- dependency-aware invalidation
- parallel obligation solving
- fast normalization
- reusable theorem artifacts
- minimal Clang reparsing
- deterministic caching

Verification performance must not change proof meaning.

---

# V1 release criteria

V1 should not ship as "complete" unless all of the following core properties work together:

- real C++L syntax
- Laws
- mechanically checked proofs
- dependent or indexed relationships
- refinement types
- equality
- induction
- termination
- contracts
- explicit unsafe/trusted boundaries
- proof erasure
- C++ native output
- trust reporting
- enough C++ safety semantics to make demonstrated guarantees meaningful

Some advanced C++ constructs may initially remain outside verified regions.

That is acceptable if the boundary is explicit.

It is not acceptable to silently call them verified.

---

# Guiding rule

Implementation may be staged.

The semantic goal must not be weakened merely to make an intermediate milestone easier.

C++L should prefer:

```text
small and sound
```

over:

```text
broad and unsound
```

and then expand verified C++ coverage systematically.
