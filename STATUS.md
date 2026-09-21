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

1. preprocesses with Clang and recognizes `law`, `proof`, `pure`, and `verified`
   contextually;
2. projects the unit into an analysis text and a runtime text in one pass;
3. resolves the C++ semantics of the analysis text through libclang;
4. elaborates the resolved semantics into typed VIR;
5. lowers VIR into core definitions and a universally quantified goal - an
   equality, or an implication from the Law's precondition to it - and lowers
   each written proof into a kernel proof term; verified functions generate
   postcondition obligations by substituting their elaborated return term, plus
   precondition obligations for verified calls and separate obligations for each
   return path through supported `if` statements;
6. submits the author's evidence, or its own when none was written, to the
   trusted kernel;
7. reports `PROVEN` only on kernel acceptance, and fails the build otherwise;
8. checks that erasure blanked proof-only text and lowered each runtime-bearing
   declaration to exactly the C++ it means, and hands the runtime program to Clang.

The verified fragment is deliberately small: a Law states a modeled proposition
over built-in integer expressions, optionally under one `expects`
precondition, universally quantified over its parameters, over
functions declared `pure` whose bodies are a single `return` of a modeled
expression. A proof declaration claims a Law at arguments of its choosing or states a modeled
proposition directly, including explicit `Eq<T>(a, b)`,
and its body is a sequence of `refl`, `exact`, `apply`, `assume` and `rewrite`
statements;
`exact` and `apply` may instantiate the evidence they name at terms, as in
`exact q(41u);`. A proof discharges the Law itself when what it claims is the
Law's own proposition; otherwise it proves one instance, which other proofs may
use. Direct propositions have their own written-proof obligations and are counted
separately from Laws. Explicit `Eq<T>` currently supports modeled integer and
Boolean types as a complete Law, proof, precondition, postcondition, or `assume`
proposition. Clang resolves its type and arguments; an unmodeled conversion is
refused.
`exact` and `apply` on equality goals can bridge definitionally equal operands
using explicit equality-substitution and reflexivity evidence.

A proposition may also state explicit universal quantification,
`forall (T x, ...) { P }`, and implication, `P -> Q`, in any of those same
places. The binders are ordinary C++ parameters Clang resolves, and they name the
innermost variables: a binder that shadows a parameter denotes the binder. A
statement written under such a binder means what it would anywhere else, so
`assume` and `rewrite` reach a goal however deeply it quantifies. A binder is not
a name a statement can use, so evidence that stays quantified cannot be
instantiated at one. `forall` and `exists` are formal only in that complete form,
and an `->` outside all brackets is implication, so a program that spells its own
`forall` or dereferences inside an expression keeps its own meaning. Existential
quantification, quantifiers in loop invariants, and formal forms nested inside an
ordinary C++ expression are refused. See `SPEC.md` 8.1-8.3 and 9.1. Both forms
lower onto the quantifier and implication the kernel already had, and added no
kernel rule.
Conjunction of supported Boolean predicates is now `PROTOTYPE`: nested `&&`
works in Laws, direct proofs, preconditions, postconditions and `assume`, under
quantifiers and implications. Introduction proves both sides; elimination exposes
either side of a checked premise. Written `refl`, `exact`, `apply`, and `rewrite`
compose with conjunctive goals, and arithmetic automation uses explicit evidence
for conjunctive facts. This adds two kernel rules (core/kernel 0.4.0), no
assumptions or axioms. Formal propositions now also compose as `&&` operands;
logical equivalence `<->` lowers to both implications, using those same rules.
Disjunction is `PROTOTYPE` on two further kernel rules (core/kernel 0.5.0), again
with no assumptions or axioms: `||` is introduced from one side and used by a
case analysis over both, automation shapes both and the kernel checks them, and
nothing grants `P || not P`. Value/guard/invariant uses of `&&` and `||` remain
unsupported (SPEC.md 7.6-7.8).
Everything else is reported as unsupported and produces no obligation. See
`ARCHITECTURE.md` 97 for the implemented structure and `TRUST.md` 41 for what
must be trusted today.

A precondition is supposed, never granted: `expects(P) ensures(Q)` states
`P -> Q`, and the premise reaches a proof only through implication
introduction. `assume` names that premise and is an error where the goal
supposes none. `rewrite` then uses an equality to transform the goal, so a
conditional Law whose conclusion needs its premise to be _used_ is provable.

Verified functions support pure return expressions, `if`/`else`, nested blocks,
integer parameters and results, one `ensures` comparison, and any number of
`expects` comparisons, which conjoin (`SPEC.md` 11.5): the body supposes each in
turn, and a verified caller proves each separately. Comparisons are `==`, `!=`,
`<`, `<=`, `>`, `>=`, with logical negation. The generated single-return
goal is `forall parameters. P1 -> ... -> Pn -> Q[R/result]`. A Law still
accepts at most one `expects` clause. Automatic evidence first tries
definitional equality. If needed, it introduces binders, uses an identical
hypothesis or rewrites once per available equality in either direction, newest first, then offers
reflexivity; the kernel
checks every step. Written `refl` retains its
definitional-equality semantics. `result` is erased specification syntax.
Verified calls instantiate their callee's contract at the resolved arguments.
Each precondition must be kernel-proven before its postcondition is available.
Caller reasoning uses abstract call results and proven summaries; kernel-checked
evidence connects that reasoning to the executable return term. Nested calls,
overloads, and forward declarations are supported within an acyclic translation
unit, including headers. Ordinary runtime calls remain unchanged. See `SPEC.md`
12.5–12.8. Each return path additionally supposes its branch conditions. Calls in
guards prove their preconditions before that guard can be used. The kernel
combines the checked paths into the complete function theorem.

Unsigned `+`, `-` and `*` are the ring of integers modulo `2^width`: every
commutative-ring identity is definitional, comparisons are normalized only as
the machine type allows, and order consequences (`i < n` gives `i + 1 <= n`)
are proven by a linear-arithmetic rule whose certificates the kernel checks
against a constraint system it states itself, wrapping included. When
definitional equality and premise rewriting do not close a goal, automation
tries linear arithmetic over the premises, then rewriting with premise
equalities and with equalities between variables that arithmetic establishes.
A contradictory path is proven from its contradiction. Signed arithmetic,
division, remainder, shifts and bitwise operators are rejected. See `SPEC.md`
7.1.1, 7.5 and 29.2.

A body may also declare locals and assign to them. Each write is a logical
version of the declaration Clang resolved, a read denotes the version current
where it stands, and what follows a branch is verified once per arm under that
arm's versions. A call bound to a local is proven where the body makes it, under
the conditions in force there, on every path that reaches it. `+=`, `-=`, `*=`,
increment and decrement are the assignments they abbreviate, for locals not
promoted before arithmetic. Uninitialized, `static`, `thread_local`, reference,
pointer and `volatile` declarations, other compound assignments, assignment to a
parameter, self-initialization, and unmodeled initializer conversions are
rejected. Every value is modeled where it is written, read or not. Each read repeats its
local's value, so bodies whose stated terms exceed a fixed size are rejected
too. Locals add no kernel rule and no runtime change.

`while` and `for` loops with a block body may state `invariant(...)` clauses
(`SPEC.md` 24.3). Each local the loop writes is carried: at the head it is a
fresh value of which only the invariants and the condition are known. Every
invariant is proven on entry and at the end of every iteration path, including
`continue` and the `for` step; what follows the loop, and any `break`, is
verified from what those paths suppose. Calls in the condition and body prove
their preconditions where the loop makes them. A function with a loop, or
calling one, has a **partial-correctness** contract: it is proven from these
conditions, reported separately, and never admitted as a core definition, so no
Law or specification can mention it and nontermination cannot reach the
kernel. Termination is not proven; `decreases`, `do`/`while`, range-based `for`
and `for` without a condition are rejected. Loops add no kernel rule; the loop
rule is correspondence trust (`TRUST.md` 41.2).

Refinement types are `PROTOTYPE`: `type R = T where (P);` and its indexed form
declare a verification-level type over an ordinary C++ base type, lower to the
alias the program keeps, and make membership an obligation at every site a value
enters the type. Refined parameters supply their predicate to the body and refined
results are proven on every return. The boundary is in `SPEC.md` 17.3.1.

Proof-side `cases` is `IMPLEMENTED` as a representation-independent engine:
subject analysis, arm matching, binders and scope, nesting, exhaustiveness,
evidence construction, dependency checking, diagnostics and erasure are shared
by every representation and produce evidence for existing kernel rules. What
states a value has comes from a decomposition provider for its resolved C++
type.

Six representation families are `IMPLEMENTED`:

| Representation | States |
| --- | --- |
| scoped enumerations | one case per distinct enumerator value, residual `unnamed` |
| `std::variant` | `alternative<i>` per index, residual `valueless` |
| `std::optional` | `some(value)`, residual `none` |
| `std::expected` | `value(payload)`, residual `error(reason)` |
| pointers | `null`, residual `non_null` |
| products | one `components(...)` arm: records, `std::pair`, `std::tuple`, `std::array`, built-in arrays |

Tagged sums share one mechanism and products share another, so these are two
provider implementations rather than six. Nesting composes generically in both
directions. `std::expected` is gated on the C++23 library. Representations with
no provider are still refused at the provider boundary by name, and arm syntax
does not make a class a sum. See `SPEC.md` 20.5 for the boundary and resource
limits, and `TRUST.md` 41.6 for what each provider does and does not state.

`cases` and `decompose` appear only in proof bodies, which contain no mutation,
so no case fact can go stale; they are not yet available over values that can
change. That extension is sequenced in `ROADMAP.md`.

This slice does **not** implement induction, loop termination, ghost state,
`unsafe`, `trusted`, proof `let`, solvers, proof caching, or any verification of
the C++ memory model. Those remain `SPECIFIED` below.

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

Verified functions now verify every return path through `if`/`else`, including
compositional calls in guards and returns, locals, assignments and their
updates, and `while`/`for` loops against explicit invariants (partial
correctness). All six integer comparisons are
represented structurally. The original function/call slices use seven rules;
path composition adds one conditional-elimination rule, and machine arithmetic
adds one linear-arithmetic rule; conjunction adds introduction and elimination,
bringing the core to eleven, with zero logical
assumptions and zero runtime checks. Locals and loops add none. Unsigned arithmetic is
normalized as a ring modulo `2^width`, and order consequences are kernel-checked.
This completes the imperative foundation (ROADMAP large slice 1). Next is the
formal language and proof core: propositions, proofs, dependent and refinement
types, induction and termination. Memory and reference reasoning, and SMT
automation come later.

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

| Capability                    | Status        |
| ----------------------------- | ------------- |
| C++L language mission         | `SPECIFIED`   |
| Genuine C++ superset model    | `PROTOTYPE`   |
| `law` declarations            | `PROTOTYPE`   |
| `ensures` clauses on laws     | `PROTOTYPE`   |
| `proves` clauses              | `PROTOTYPE`   |
| proof declarations            | `PROTOTYPE`   |
| `refl` / `exact` / `apply`    | `PROTOTYPE`   |
| proof instantiation `q(t)`    | `PROTOTYPE`   |
| `expects` clauses on laws     | `PROTOTYPE`   |
| `assume`                      | `PROTOTYPE`   |
| `rewrite`                     | `PROTOTYPE`   |
| multi-statement proof bodies  | `PROTOTYPE`   |
| proof `let`                   | `SPECIFIED`   |
| proof case analysis `cases`   | `PROTOTYPE`   |
| proposition types             | `PROTOTYPE`   |
| explicit `Eq<T>` propositions | `PROTOTYPE`   |
| direct proposition proofs     | `PROTOTYPE`   |
| universal quantification      | `PROTOTYPE`   |
| implication                   | `PROTOTYPE`   |
| conjunction                   | `PROTOTYPE`   |
| logical equivalence           | `PROTOTYPE`   |
| disjunction                   | `PROTOTYPE`   |
| existential quantification    | `SPECIFIED`   |
| dependent types               | `PROTOTYPE`   |
| refinement types              | `PROTOTYPE`   |
| algebraic data types          | `NOT PLANNED` |
| runtime pattern matching      | `NOT PLANNED` |
| impossible-state elimination  | `SPECIFIED`   |
| definitional equality         | `PROTOTYPE`   |
| propositional equality        | `PROTOTYPE`   |
| normalization                 | `PROTOTYPE`   |
| `induction`                   | `SPECIFIED`   |
| well-founded recursion        | `SPECIFIED`   |
| termination checking          | `SPECIFIED`   |
| `expects` on functions        | `PROTOTYPE`   |
| `ensures` on functions        | `PROTOTYPE`   |
| `pure`                        | `PROTOTYPE`   |
| `verified`                    | `PROTOTYPE`   |
| verified-call composition     | `PROTOTYPE`   |
| path-sensitive `if`/`else`    | `PROTOTYPE`   |
| integer comparison predicates | `PROTOTYPE`   |
| locals and assignments        | `PROTOTYPE`   |
| `invariant` on loops          | `PROTOTYPE`   |
| partial-correctness contracts | `PROTOTYPE`   |
| `ghost`                       | `SPECIFIED`   |
| `unsafe`                      | `SPECIFIED`   |
| `trusted`                     | `SPECIFIED`   |
| `decreases`                   | `SPECIFIED`   |
| proof erasure                 | `PROTOTYPE`   |

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
| Universal elimination                | `PROTOTYPE`   |
| Implication introduction             | `PROTOTYPE`   |
| Implication elimination              | `PROTOTYPE`   |
| Hypothesis context                   | `PROTOTYPE`   |
| Equality substitution (rewriting)    | `PROTOTYPE`   |
| Conditional elimination              | `PROTOTYPE`   |
| Machine-arithmetic normal form       | `PROTOTYPE`   |
| Linear arithmetic (certificates)     | `PROTOTYPE`   |
| Existential introduction/elimination | `NOT STARTED` |
| Induction checking                   | `NOT STARTED` |
| Refinement introduction/elimination  | `NOT STARTED` |
| Normalization engine                 | `PROTOTYPE`   |
| Termination checker                  | `NOT STARTED` |
| Proof certificate format             | `NOT STARTED` |
| Kernel fuzzing                       | `NOT STARTED` |
| Kernel property testing              | `PARTIAL`     |
| Kernel rejection tests               | `PROTOTYPE`   |
| Mechanized core calculus             | `NOT STARTED` |
| Meta-theory / soundness proofs       | `NOT STARTED` |

The kernel implements eleven rules:

```text
1. Reflexivity
2. Equality substitution
3. Universal introduction
4. Universal elimination
5. Implication introduction
6. Implication elimination
7. Hypothesis use
8. Conditional elimination
9. Linear arithmetic
10. Conjunction introduction
11. Conjunction elimination (left or right)
```

They act over propositions built from equality, universal quantification,
implication and conjunction. The kernel's terms are variables, machine-integer literals,
applications of admitted definitions, and primitives: wrapping addition,
subtraction and multiplication, the six comparisons, boolean negation and
selection. It admits no recursion, which is why it needs no termination checker
yet (`ARCHITECTURE.md` 97.7).

Reflexivity decides definitional equality by normalization, which puts machine
arithmetic in polynomial normal form modulo `2^width` and comparisons in
canonical form (`SPEC.md` 7.1.1). Linear arithmetic concludes an equality or
comparison from facts whose evidence it checks, by checking a certificate
against the integer constraint system it states for them (`SPEC.md` 7.5).
Property testing currently covers the normal form only: random terms and their
normal forms are evaluated by an independent evaluator on every assignment of
small types.

Universal elimination instantiates quantified evidence at a term. The kernel
checks the evidence against the proposition it is eliminated from, derives the
argument's type itself, and obtains the resulting proposition by its own
capture-safe substitution. Several arguments are several eliminations; there is
no multi-argument rule.

Implication introduction supposes a premise and puts it in the kernel's own
hypothesis context; implication elimination discharges one against evidence for
it. A hypothesis is usable only where an enclosing introduction placed it, and
is restated for the binders it is used beneath. No rule anywhere admits a
premise on its own.

Equality substitution transports evidence through a proposition context. It is
a genuinely new capability rather than sugar over the others: without it,
evidence for `a = b` closes a goal that already is `a = b` and can do nothing
else. The kernel performs the substitution itself - the context is given to it,
its hole type-checked against the type the equality is stated at, and the
resulting proposition derived rather than accepted. Symmetry, and rewriting in
the opposite direction, are this rule at another context; neither is primitive
and neither is inferred.

Written proof declarations added no rule of their own: `refl`, `exact`,
`apply`, `assume` and `rewrite` elaborate into terms built from these rules.

---

# Mathematical foundation status

| Area                            | Status        |
| ------------------------------- | ------------- |
| Curry–Howard foundation         | `SPECIFIED`   |
| Dependent type theory direction | `SPECIFIED`   |
| Inductive reasoning             | `SPECIFIED`   |
| Equality model                  | `SPECIFIED`   |
| Hoare-style contracts           | `PROTOTYPE`   |
| Weakest-precondition reasoning  | `PROTOTYPE`   |
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

| Capability                               | Status      |
| ---------------------------------------- | ----------- |
| Clang-based C++ semantic integration     | `PROTOTYPE` |
| Clang AST bridge                         | `PROTOTYPE` |
| Source mapping                           | `PROTOTYPE` |
| C++ name lookup reuse                    | `PROTOTYPE` |
| C++ overload-resolution reuse            | `PROTOTYPE` |
| C++ template interoperability            | `SPECIFIED` |
| C++ `constexpr` interoperability         | `SPECIFIED` |
| C++ exceptions model                     | `SPECIFIED` |
| C++ RTTI model                           | `SPECIFIED` |
| C++ ABI preservation                     | `SPECIFIED` |
| libc++ interoperability                  | `SPECIFIED` |
| Existing native library interoperability | `SPECIFIED` |
| C interoperability                       | `SPECIFIED` |
| Objective-C++ interoperability           | `SPECIFIED` |
| JNI interoperability                     | `SPECIFIED` |
| N-API interoperability                   | `SPECIFIED` |
| WASM target compatibility                | `SPECIFIED` |

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
| VIR contract model             | `PROTOTYPE`   |
| VIR proof obligations          | `PROTOTYPE`   |
| VIR unsafe/trust annotations   | `NOT STARTED` |
| VIR serialization              | `NOT STARTED` |
| VIR deterministic hashing      | `NOT STARTED` |

Obligation identities are content-derived today, but they are computed from the
core representation rather than from VIR, so VIR hashing has no consumer yet and
is not implemented.

---

# Contracts status

| Capability                                        | Status        |
| ------------------------------------------------- | ------------- |
| Preconditions (supported fragment)                | `PROTOTYPE`   |
| Postconditions (supported fragment)               | `PROTOTYPE`   |
| Function invariants                               | `SPECIFIED`   |
| Loop invariants                                   | `PROTOTYPE`   |
| Loop termination (`decreases`)                    | `SPECIFIED`   |
| Verification-condition generation (returns/paths) | `PROTOTYPE`   |
| Verification-condition generation (loops)         | `PROTOTYPE`   |
| Local versioning (declarations/assignments)       | `PROTOTYPE`   |
| Weakest-precondition engine                       | `NOT STARTED` |
| Contract composition                              | `PROTOTYPE`   |
| Contract reuse across translation units           | `NOT STARTED` |

---

# Refinement status

| Capability                              | Status        |
| --------------------------------------- | ------------- |
| Predicate refinements                   | `PROTOTYPE`   |
| Static refinement construction          | `PROTOTYPE`   |
| Runtime checked refinement construction | `SPECIFIED`   |
| Refinement elimination                  | `PROTOTYPE`   |
| Refinement subtyping                    | `PROTOTYPE`   |
| Indexed refinements                     | `PROTOTYPE`   |
| Arithmetic refinement solving           | `NOT STARTED` |
| Bitvector refinements                   | `NOT STARTED` |
| User-defined refinement predicates      | `SPECIFIED`   |

A refinement declaration lowers to the alias it means and adds no runtime
representation. Membership is an obligation at every modeled flow into the type - a
local declaration, an assignment or update, a verified call's argument, a return -
closed under the path conditions where the value enters, so a branch fact discharges
it. Subtyping is the implication between predicates and carries no runtime check in
either direction (`SPEC.md` 17.3.2). Refined returns of an unverified function,
refined members, references and pointers, and refinements in templated contexts are
refused rather than approximated.

The same membership checks cover partial-correctness bodies containing loops and
their callers, including unused refined locals. Corrupt or unresolved refinement
metadata fails closed. This closes a verification gap without promoting the
overall refinement feature beyond `PROTOTYPE`.

---

# Case analysis and induction status

C++L reasons over C++ types and adds no data types of its own (`SPEC.md` §19,
RFC 0005).

| Capability                                  | Status        |
| ------------------------------------------- | ------------- |
| Generic case engine (all representations)   | `IMPLEMENTED` |
| Arm binders, scope and named premises       | `IMPLEMENTED` |
| Exhaustiveness from a provider's partition  | `IMPLEMENTED` |
| Scoped-enumeration provider                 | `IMPLEMENTED` |
| Enum residual `unnamed(value)`              | `IMPLEMENTED` |
| `std::variant` provider                     | `IMPLEMENTED` |
| `std::optional` / `std::expected` providers | `IMPLEMENTED` |
| Pointer null / non-null provider            | `IMPLEMENTED` |
| Product decomposition providers             | `IMPLEMENTED` |
| Formal value model for the above            | `IMPLEMENTED` |
| Cross-provider nested decomposition         | `IMPLEMENTED` |
| `cases` over values that can change         | `SPECIFIED`   |
| Impossible cases                            | `SPECIFIED`   |
| `induction` with explicit arms / short form | `SPECIFIED`   |
| Machine-integer induction principles        | `SPECIFIED`   |
| Pointer-structure induction (premised)      | `SPECIFIED`   |
| `@N` `@Z` `@Seq` `@Set` `@Map` domains      | `SPECIFIED`   |
| Machine-to-domain conversions               | `NOT STARTED` |
| Wildcard arms                               | `NOT PLANNED` |
| General-purpose algebraic data types        | `NOT PLANNED` |
| Runtime pattern matching (`match`)          | `NOT PLANNED` |

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
| Fixed-width integer semantics    | `PARTIAL`     |
| Unsigned `+` `-` `*` (modular)   | `PROTOTYPE`   |
| Order reasoning (linear)         | `PROTOTYPE`   |
| Signed `+` `-` `*`               | `NOT STARTED` |
| Division, remainder, shifts      | `NOT STARTED` |
| Checked arithmetic               | `SPECIFIED`   |
| Wrapping arithmetic              | `SPECIFIED`   |
| Saturating arithmetic            | `SPECIFIED`   |
| Big integer proof domain         | `SPECIFIED`   |
| Bitvector solver integration     | `NOT STARTED` |
| Overflow diagnostics             | `NOT STARTED` |

Fixed-width semantics are `PARTIAL`: unsigned `+`, `-`, `*` and all six
comparisons are modeled exactly at every width from 1 to 64 bits, and signed
comparisons are modeled; signed arithmetic is refused until its no-overflow
obligations exist. `Wrapping arithmetic` above means the explicit `Wrapping<T>`
facility of the roadmap, which is not the same as C++ unsigned arithmetic.

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

Erasure currently removes law declarations, proofs, contracts, loop invariants and
the `pure` specifier, and lowers a refinement type declaration to the alias it
means. The implementation checks a strong property rather than asserting success:
the runtime program must be the analysed program with proof-only spans blanked and
each runtime-bearing declaration replaced by the canonical C++ recomputed from that
declaration, with line numbering unchanged (`TRUST.md` 10.1). Equivalence is
therefore established structurally for the constructs implemented, not proven in
general.

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
| Rewriting                        | `PROTOTYPE`   |
| Arithmetic automation            | `PROTOTYPE`   |
| Contradiction solving            | `PROTOTYPE`   |
| Induction tactic                 | `SPECIFIED`   |
| cvc5 integration                 | `NOT STARTED` |
| Z3 integration                   | `NOT STARTED` |
| Proof certificates               | `PROTOTYPE`   |
| Independent certificate checking | `PROTOTYPE`   |
| Counterexample extraction        | `NOT STARTED` |

Automation must not weaken the meaning of `PROVEN`.

Rewriting, arithmetic and contradiction solving are automatic evidence for
contracts and for Laws without a written proof; there is no proof statement that
invokes them yet. Proof certificates exist for linear arithmetic only: automation
finds them by Fourier-Motzkin elimination, and the kernel checks them against a
system it states itself. No external solver is involved or trusted.

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

| Tool                    | Status        |
| ----------------------- | ------------- |
| Clang-compatible driver | `PROTOTYPE`   |
| `cppl build`            | `SPECIFIED`   |
| `cppl check`            | `SPECIFIED`   |
| `cppl prove`            | `SPECIFIED`   |
| `cppl explain`          | `SPECIFIED`   |
| `cppl trust-report`     | `PARTIAL`     |
| LSP: sync and diagnostics | `PARTIAL`   |
| LSP: hover, definition, completion | `NOT STARTED` |
| IDE proof goals         | `NOT STARTED` |
| Proof navigation        | `NOT STARTED` |
| Counterexample UI       | `NOT STARTED` |
| Structured diagnostics  | `PROTOTYPE`   |

The driver is Clang-compatible rather than subcommand-based: `cppl` takes the
arguments `clang++` takes. Trust reporting exists as `--cppl-trust-report`; the
subcommand forms above are not implemented. The report counts partial-
correctness contracts and loop-invariant obligations separately.

`cppl-lsp` implements `initialize`, `shutdown`, `exit`, full-document
`textDocument/didOpen`, `didChange` and `didClose`, and
`textDocument/publishDiagnostics`. Diagnostics come from the ordinary compile
pipeline over the live buffer (`driver::compile_buffer`), so the server has no
decomposition, exhaustiveness or verification engine of its own; a structural
linter adds contextual C++L checks over the same recognized syntax rather than
re-recognizing it. Transport is separate from analysis, and the library is
tested without an editor. The server advertises only `textDocumentSync`: hover,
go-to-definition, completion and incremental sync are designed in
`tools/cppl-lsp/README.md` but not implemented, and are deliberately not
advertised as capabilities.

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
can substantiate, and says _not analysed_ where C++L does not yet look.

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

The first of these now exists: see _What the current implementation does_ above.
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

## Abstract observation core

The core supports nominal abstract values and typed logical projections, with
independent malformed-evidence and substitution tests. The source decomposition
providers built on it are implemented and listed above. Nothing here promotes
mutation analysis or the unimplemented editor capabilities to implemented
status: `cases` is still unavailable over values that can change, and the
language server still offers sync and diagnostics only.
