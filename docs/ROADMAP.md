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
refinement crossings. Signed arithmetic, division and integral conversions are
prototyped with their defined-behavior obligations (RFC 0019). General object
and pointer memory reasoning, and SMT remain incomplete. Trust propagation
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

Induction over an unsigned machine integer parameter is implemented, as the
kernel's fifteenth rule (`STATUS.md`, Case analysis and induction status). None
of this delivers induction over signed integers or structures, or recursive
proof admission.

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
virtual functions, constructors and destructors, member functions of class
templates, members of container type, and the disjointness of an object from a
reference argument where Clang resolves it, as between a local object and a
reference parameter's referent (`tests/fixtures/cross_feature/client.cpp`,
`first_copied`).

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

Current sequence: unsigned `+ - *` as the ring modulo `2^width` with linear
order reasoning (RFC 0006), and signed `+ - *` and unary `-`, `/`, `%` and
integral conversions with their definedness obligations (RFC 0019), are
prototyped. Next are products of two bounded unknowns, which need a nonlinear
certificate step, then shifts and bitwise operators with their own obligations,
then `Checked<T>`, `Wrapping<T>` and `Saturating<T>`.

Exit criterion:

> Mathematical proofs cannot silently assume arithmetic semantics different from runtime C++.

---

# Phase 10 - Memory model

This is one of the most important systems milestones.

Storage generations (RFC 0020) are the first lifetime rule in verified bodies:
a standard container's views and element references end where its storage may
be reallocated, shrunk, replaced or moved from, a use after is refused, and a
view is formed only over a container that outlives it.

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

Signed overflow, division by zero, the least value divided by -1 and
unrepresentable conversions are obligations today, owed on the path that
evaluates the operation (RFC 0019). So are bounds at every modeled subscript of
an array, a capability region and a standard container (RFC 0014, RFC 0016,
RFC 0020), and, for the container subset, the storage generation a view or an
element reference was formed at (RFC 0020). The rest of the list is not started
or belongs to the memory model.

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

Current sequence: `std::array`, `std::vector`, `std::string` and
dynamic-extent `std::span` are prototyped as the verified sequence subset
(RFC 0020, `SPEC.md` J.17): a container is a length and element places at a
storage generation, its operations are trusted library summaries over that
length (`TRUST.md` 28.1), and every claim resting on one says so in the trust
report. Iterators, range-`for`, algorithms, `std::string_view`, `subspan`,
static-extent spans, the rest of each container's members and element types
beyond integers and `bool` are next; `std::optional` and `std::variant` read
through the case engine (Phase 7) but are not yet written.

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

That is acceptable if the boundary is explicit. For V1 it is RFC 0022: every
construct of `SPEC.md` Annex X is listed as verified or refused, and a test
checks each one (`tests/fixtures/subset/manifest.tsv`). RFC 0022 also fixes the
V1 scope of arithmetic (no shifts or bitwise operators), induction (unsigned
machine integers only) and dependent reasoning (indexed refinements, dependent
contracts and indexed observation; no kernel type families).

It is not acceptable to silently call them verified.

## V1 release gates

The criteria above are release gates. A 1.0.0 release is cut only when every
gate's pass condition holds on the release candidate, the commit the release
would tag. Each gate's status and evidence are recorded in `STATUS.md`, "V1
closure", in the subsection the gate names. A gate is never passed by wording:
its condition is a check that runs, or a stated fact another reader can confirm.

In the conditions below, "green" means that every test the named profile
registers passes on the release candidate built from a clean tree.

| Gate | Criterion | Pass condition | `STATUS.md` |
| --- | --- | --- | --- |
| G1 | real C++L syntax | `GRAMMAR.md` describes every accepted form. The conformance tests (`conformance_*`) and `unit_recognizer_test` are green. | "V1 closure: syntax" |
| G2 | Laws, mechanically checked proofs | PROVEN is reachable only through a kernel acceptance of the claim's goal (`TRUST.md` 36.3). The `kernel_*` tests are green. | "V1 closure: Laws and kernel-checked proofs" |
| G3 | dependent or indexed relationships | Every RFC 0022 dependent construct has a verified fixture and a refused twin, and `e2e_safety_subset` is green. | "V1 closure: dependent and indexed relationships" |
| G4 | refinement types | A refinement is entered only by static proof or by `validate<R>(e)` (RUNTIMECHECK-013). `e2e_runtime_validation` and `negative_runtime_validation` are green. | "V1 closure: refinement types" |
| G5 | equality | `e2e_formal_equality` and `negative_formal_equality` are green. | "V1 closure: equality" |
| G6 | induction | Induction over unsigned machine integers is checked by the kernel rule. `e2e_induction` and `negative_induction` are green. | "V1 closure: induction" |
| G7 | termination | `e2e_termination` and `negative_termination` are green. | "V1 closure: termination" |
| G8 | contracts | The `e2e_verified_*`, `negative_verified_*`, `e2e_cross_tu` and `negative_cross_tu` tests are green. | "V1 closure: contracts" |
| G9 | explicit unsafe/trusted boundaries | `e2e_unsafe_boundary`, `negative_unsafe_boundary` and `e2e_trust_closure` are green. | "V1 closure: unsafe and trusted boundaries" |
| G10 | C++ safety semantics | Every Annex X construct is classified. `e2e_safety_subset`, `negative_refused_twins`, `negative_sequence_attacks`, `negative_sequence_generations` and `negative_sequence_boundaries` are green. | "V1 closure: C++ safety semantics" |
| G11 | proof erasure, native output | The erasure and ABI equivalence tests are green in every standard mode they name. | "V1 closure: proof erasure and native output" |
| G12 | trust reporting | The text report, the JSON report and the editor name every category of every claim's closure, and `TRUST.md` 36.3 is current. | "V1 closure: trust reporting" |
| G13 | kernel assurance | `check_sound` holds with no hypothesis, audited closed by `tools/formal/check.sh`. | "V1 closure: kernel assurance, mechanized model" |
| G14 | adversarial testing | The full mutation suite kills every entry, or classifies it equivalent under `MUTATION_TESTING.md` 5. `ci-asan`, `ci-ubsan` and `ci-fuzz` are green. | "V1 closure: adversarial testing" |
| G15 | correspondence TCB | The trusted translation layer is either verified, or stated as TCB in every document and report that describes a PROVEN claim. | "V1 closure: correspondence TCB" |
| G16 | artifact provenance | Interface provenance is authenticated, or is stated as TCB in every report that uses an interface. | "V1 closure: artifact provenance" |
| G17 | stability | `SPEC.md`, `GRAMMAR.md` and `KERNEL.md` are frozen at the release version, and `STATUS.md` says so. | "V1 closure: stability" |
| G18 | delivery | The release archive is built, tested, attested and installed on every platform the release claims (`integration_installed_package`). `INSTALL.md` names exactly those platforms. The ABI statement matches `COMPATIBILITY.md`. | "V1 closure: delivery" |

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
