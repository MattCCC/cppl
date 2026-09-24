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
8. checks that erasure blanked all proof-only text and nothing else, and lowered
   each runtime-bearing declaration to exactly the C++ it means, and hands the
   text it checked to Clang as the runtime program.

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
ordinary C++ expression are refused. See `SPEC.md` 8, 8.1-8.2 and 9. Both forms
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
nothing grants `P || not P`. In a verified `if` condition, `&&`, `||` and `!`
are elaborated into the routes they select between (SPEC.md 12.7). Value and
invariant uses of `&&` and `||` remain unsupported, because a proposition is not
a value (SPEC.md 7.6-7.8).
Everything else is reported as unsupported and produces no obligation. See
`docs/ARCHITECTURE.md` 95 for the implemented structure and `TRUST.md` 4 for what
must be trusted.

A precondition is supposed, never granted: `expects (P) ensures (Q)` states
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

`while` and `for` loops with a block body may state `invariant (...)` clauses
(`SPEC.md` 24). Each local the loop writes is carried: at the head it is a
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
rule is correspondence trust (`TRUST.md` 12.1).

Refinement types are `PROTOTYPE`: `type R = T where (P);` and its indexed form
declare a verification-level type over an ordinary C++ base type, lower to the
alias the program keeps, and make membership an obligation at every site a value
enters the type. Refined parameters supply their predicate to the body and refined
results are proven on every return. The boundary is stated under
[Refinement status](#refinement-status).

`contradiction e;` is `PROTOTYPE`: a proof statement that closes the goal from
evidence that the context where it is written cannot occur (`GRAMMAR.md` 5.6,
`SPEC.md` `CASE-011`, `CASE-013`). The named evidence and every premise standing
there are first refuted into `False` by linear arithmetic, which the goal takes
no part in, and only then is the goal closed from that by falsity elimination,
so a goal that merely follows from the premises establishes nothing. The kernel
checks the certificate against constraints it states itself. Certificates are
found by the same bounded refutation search automation uses
(`compiler/refutation`), which is untrusted: when it finds nothing, the claim is
unproven, never an impossibility (`CASE-015`). The goal may have any shape,
an equality of structured values such as two records included, because falsity
elimination looks only at the evidence for `False`. Automation closes a goal
from contradictory premises the same way, so a path whose premises cannot hold
is proven whatever its goal equates. No axiom or trusted mechanism is added.

Omitting a case is `PROTOTYPE`: `omit label by contradiction e;` inside a
`cases` statement accounts for a case without an arm (`GRAMMAR.md` 5.7,
`SPEC.md` `CASE-004`) and is the only way a case goes uncovered. An absent arm
with no omission stays non-exhaustive, and the engine never searches the
surrounding context to decide that a missing arm was meant (`CASE-005`), so an
accidental omission and a proved impossibility stay distinguishable. The
evidence is checked under the omitted case's own discriminator premise, residual
cases included. Each omission is an obligation of its own (`CASE-012`,
`CASE-016`): origin `OmittedCase`, an identity that includes that origin, a goal
stated apart from the proof it is written in, and evidence the kernel checks
against that goal. The trust report counts them as `Omitted cases proven`, apart
from the laws they occur in.

Claiming a runtime path impossible is `PROTOTYPE`: `contradiction e;` written as
a statement of a verified body claims that no execution reaches it (`GRAMMAR.md`
5.6, `SPEC.md` `VERIFIED-023`, `VERIFIED-045`). The claim is checked where it
stands: the named proof, instantiated at arguments read at the versions current
there, and every fact of the path - preconditions, branch conditions, loop
invariants, callee postconditions - are refuted into `False` by the same
mechanism a proof uses. The path ends at the claim, so what follows it owes
nothing. Each claim is an obligation of its own (`CASE-012`, `CASE-016`): origin
`ImpossiblePath`, an identity that includes that origin, the path's facts closed
over `False` as its goal, and evidence built only once the proof it names has
been admitted. It is never proven any other way, and one resting on a callee's
postcondition waits for that callee to be proven. The trust report counts them
as `Impossible paths proven`. A function with a claim has partial-correctness
conditions, since a path ending in one returns no value. C++ comes first
(`WORD-011`): where the translation unit gives `contradiction` any other
meaning, the statement stays ordinary C++ and a warning says so. In a function
that is not verified it is refused. The statement erases to an empty statement,
so an unbraced `if` whose body it was keeps one (`ERASE-016`). The evidence is a
proof declaration; a law without a written proof cannot be named, exactly as in
a proof body, and neither can a trusted law, which a proof body may name
(`TRUSTED-006`). A claim naming a proof that rests on a trusted law rests on it
too, and so do its function's contract and every caller's.

Proof-side `cases` is `IMPLEMENTED` as a representation-independent engine:
subject analysis, arm matching, binders and scope, nesting, exhaustiveness,
evidence construction, dependency checking, diagnostics and erasure are shared
by every representation and produce evidence for existing kernel rules. What
states a value has comes from a decomposition provider for its resolved C++
type.

Six representation families are `IMPLEMENTED`:

| Representation      | States                                                                                       |
| ------------------- | -------------------------------------------------------------------------------------------- |
| scoped enumerations | one case per distinct enumerator value, residual `unnamed`                                   |
| `std::variant`      | `alternative<i>` per index, residual `valueless`                                             |
| `std::optional`     | `some(value)`, residual `none`                                                               |
| `std::expected`     | `value(payload)`, residual `error(reason)`                                                   |
| pointers            | `null`, residual `non_null`                                                                  |
| products            | one `components(...)` arm: records, `std::pair`, `std::tuple`, `std::array`, built-in arrays |

Tagged sums share one mechanism and products share another, so these are two
provider implementations rather than six. Nesting composes generically in both
directions. `std::expected` is gated on the C++23 library. Representations with
no provider are still refused at the provider boundary by name, and arm syntax
does not make a class a sum. A statement is read with at most 64 arms, omissions
included, and arms nest at most 32 deep. See `SPEC.md` 20.1 and 20.4 for the
boundary, and `TRUST.md` 19 for what each provider does and does not state.

`cases` and `decompose` are also `IMPLEMENTED` as statements of a verified
function's body (`SPEC.md` 20.7, `CASE-017` to `CASE-020`), over values that can
change. The subject is read at the versions current where the split is written;
each arm continues the path with its case's discriminator as a fact, and the rest
of the body is verified once per arm, exactly as the proof-side split supposes
each case. An arm holds only nested splits and a `contradiction` claim that ends
its path; an omitted case is an omitted-case obligation checked against the
path's facts. A case fact is about one version: after a write, a write through a
reference that may alias it, a call that may change it, or at the head of a loop
that writes it, the storage has a new version that no earlier fact describes.
Invalidation is the storage model's own, not a mechanism of the case engine. The
path walk re-checks that every state has exactly one arm, so no state's path can
be dropped. A split erases to an empty statement.

What a split can reach is what verified bodies model. A local aggregate is
tracked member by member and has no single value, so it is split through its
members. Verified code cannot write a `std::optional`, `std::variant` or
`std::expected` or reassign a pointer local, so splits over those read values
that do not change within the body. A split's binders are declared once for the
statement as written, so in a function template whose specializations bind
values of different types, the specializations that disagree are refused.

This slice does **not** implement induction, loop termination, ghost state,
`unsafe`, proof `let`, solvers, proof caching, or any verification of the C++
memory model. Those remain `SPECIFIED` below. `trusted law` is implemented and
described under [Unsafe and trusted boundary status](#unsafe-and-trusted-boundary-status).

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
| `contradiction`               | `PROTOTYPE`   |
| multi-statement proof bodies  | `PROTOTYPE`   |
| proof `let`                   | `SPECIFIED`   |
| proof case analysis `cases`   | `PROTOTYPE`   |
| case splits in verified code  | `PROTOTYPE`   |
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
| impossible-state elimination  | `PROTOTYPE`   |
| impossible runtime paths      | `PROTOTYPE`   |
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
| `trusted`                     | `PARTIAL`     |
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
| Conjunction introduction/elimination | `PROTOTYPE`   |
| Disjunction introduction/elimination | `PROTOTYPE`   |
| Falsity elimination                  | `PROTOTYPE`   |
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

The kernel implements fourteen rules:

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
12. Disjunction introduction (left or right)
13. Disjunction elimination (a case for each side)
14. Falsity elimination (any goal, from evidence for False)
```

They act over propositions built from equality, universal quantification,
implication, conjunction, disjunction and `False`. `False` has no introduction
rule (`TRUST.md` TCB-CORE-017): evidence for it comes from a hypothesis, from an
elimination, or from linear arithmetic refuting its facts with no goal taking
part (core/kernel 0.7.0). The kernel's terms are variables,
machine-integer literals, applications of admitted definitions, observations of
an abstract value (at a constant position, or at an index that is itself a
term), and primitives: wrapping addition, subtraction and multiplication, the
six comparisons, boolean negation and selection. It admits no recursion, which
is why it needs no termination checker yet (`SPEC.md` 22.2, 22.4).

Reflexivity decides definitional equality by normalization, which puts machine
arithmetic in polynomial normal form modulo `2^width` and comparisons in
canonical form (`SPEC.md` 7.1.1). Linear arithmetic concludes an equality or
comparison from facts whose evidence it checks, by checking a certificate
against the integer constraint system it states for them (`SPEC.md` 7.5). It
concludes `False` when the certificate refutes the facts alone, and falsity
elimination then closes any goal from that (`FOUNDATIONS.md` 26).
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
`apply`, `assume`, `rewrite` and `contradiction`, and the arms and omissions of
`cases` and `decompose`, elaborate into terms built from these rules.

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
| C++ template interoperability            | `PROTOTYPE` |
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
either direction (`SPEC.md` 17.4).

Scalar reference parameters (`T&`, `const T&`, `T&&`), local references to modeled
parameters, and verified void functions now use storage versions and post-state
contracts (SPEC.md 12.9). Direct writes and verified calls invalidate possible
aliases, including const references. Callee postconditions can establish facts
about new versions; refined actual storage still owes membership. Repeated actual
arguments share state. Branches and loop invariants use the same version model.
This remains `PROTOTYPE`, not production-complete refinement flow.

Refined data members are implemented over the generic place model. A member is a
place of its own, reached by a path of projections out of the object it belongs
to, so `s`, `s.x` and `s.x.y` are three places and `s.x` and `s.y` are never one.
Construction and every later write cross into the member's own declared type
through the one write path, and the obligation is owed where the value enters
the member rather than deferred to a read (`SPEC.md` 17.6, Annex I
REFINEOBL-007). A write through a reference to a member is a write to that
place, and distinct members do not disturb one another.

Semantic validity is recursive (`SPEC.md` 17.2.1): a record is valid when its
refinement-bearing subobjects are, stated over the projections that name them.
A verified parameter therefore supplies the validity of its refined subobjects
as an entry premise exactly as a refined scalar parameter does (`SPEC.md`
17.2.2), and no proof of the historical construction path is required to use it.
`S{-5}` in a verified body is rejected by the construction obligation, and a
record built outside a verified body is still refused at that boundary, because
ordinary C++ establishes its members without proof. Permanent regression tests
pin both directions, and the erasure test shows a refined member lowering to a
plain member with identical generated code.

Refined array elements use the same place model, at constant and at symbolic
indices alike. General casts, lambdas, methods, alias-return lifetimes, `old`
over mutable state, and dependent object flows remain unimplemented.

A record is decomposed from its resolved type rather than from the cursors of
its definition, so an instantiated class template is decomposed like any other
record and a refined member of `Box<int>` is the refined storage the template
declared (`SPEC.md` 17.6, 42 TEMPLATE-001). Reading, writing, sibling
preservation, nesting and indexed refinements behave in a specialization exactly
as in an ordinary record, and matched pairs pin that the predicate, the index
argument and the component order are each really read.

Asking the type is also what makes a base subobject visible. A record with a
base is not decomposed by its own members -- the base carries state no member
names -- so the member is refused by name instead of read out of a
decomposition that left part of the object out. A union and an inaccessible
member are refused as before.

A refinement erases to its base type, so it is that type as a template argument:
`Box<Positive>` and `Box<int>` are one specialization with one member type, and
no predicate travels with the argument. Formal identity is semantic rather than
spelling-only (`SPEC.md` 43), so such a program fails to prove rather than
quietly reading a predicate that is not there.

A member that is itself an aggregate is the places its own members are, not one
value: an aggregate local is tracked as one version per scalar leaf, reached by
a path of field and element steps. `o.i.v` and `h.items[0]` are places exactly
as `o.a` is, so a write reaches the leaf written and leaves a sibling at depth
alone, and a refined leaf owes its predicate where the value enters it. Nesting
is bounded at eight levels and 256 leaves per declaration, and construction must
stay fully visible at every level: partial initialization, default
initialization and a union member are each refused by name.

Writing a member of a by-value aggregate *parameter* is still refused: the
parameter is not tracked storage, so the write has no modeled effect. The
refusal is the same for an ordinary record and for a specialization.

A contract may name types a template supplies, including dependent names
spelled through one, because each clause is projected under the header its
declaration stands under. A contract on a template itself is parameterized by
the template's own parameters and means what it means after substitution, so it
is checked per specialization (`SPEC.md` 42 TEMPLATE-001).

Clang performs the substitution and selects the specialization; what is checked
is each specialization it produced. A specialization is reached from the uses
that instantiated it, because an implicit instantiation is not a declaration of
the translation unit. Each carries its own instantiated contract: the clause
probes are declared under the same header, and the body names them at its own
template arguments, so C++ instantiates a function's contract alongside the
function at exactly the arguments Clang substituted. A non-type parameter inside
a clause is the value the specialization was instantiated at, read back from
Clang rather than substituted here.

Proof identity separates the specializations structurally rather than by a rule
of its own: obligations are keyed by Clang's USR, which already distinguishes
`f<4>` from `f<5>`, so evidence for one specialization cannot discharge another
(TEMPLATE-003). A contract that is true at one argument and false at another
fails only where it is false. A template nothing instantiates has no
specialization to check, so it is refused rather than reported as verified: an
uninstantiated contract states nothing this unit discharged.

A C++ constraint remains a C++ constraint. It controls which specialization
Clang selects and never becomes a formal premise (TEMPLATE-002).

An explicit specialization is one concrete function, not an instantiation of
the primary. It states its own contract at its own declaration, so it is
checked directly: its arguments are already fixed, its contract probes are
ordinary functions rather than templates, and nothing has to be instantiated to
reach them. The primary's proof never covers it, so a specialization that
replaces the body with one its contract does not describe is refused by name,
with the goal stated at its own arguments (TEMPLATE-003).

An explicit instantiation, `template unsigned f<4u>(unsigned);`, instantiates
the body in this unit, so the specialization it names is checked here. libclang
exposes no cursor for the instantiation itself, so the specialization is reached
the way every other one is -- from a reference to it -- which the projector
emits into the analysis text alone. The runtime text keeps the instantiation the
author wrote, and the reference reaches no object file. Each instantiation is
its own specialization: a contract true at one argument and false at another is
refused whichever order they are written in. `extern template` is an
instantiation declaration rather than a definition, so it instantiates nothing
here and is still reported as uninstantiated (TEMPLATE-001).

Verified function templates are `PROTOTYPE`. A specialization consumed across
translation units carries no exported verification metadata, so a use in another
unit is not verified there; `docs/rfcs/0017-cross-translation-unit-verification.md`
proposes the mechanism and nothing implements it yet.

A lambda is a closure object with its own call operator, and it is refused on
every route into a verified body: bound to a local, called without ever
becoming one, and written inside a clause. A by-reference capture is why this
has to be closed rather than merely unimplemented, since one can write a
refined local after its fact was established; regression tests pin each route.
A lambda spelled to resemble the text a capability probe is projected to grants
no capability either, because a capability comes from the recognized
`readable`/`writable` form and never from what the projection happens to emit.

A value crossing from one refinement into another owes the target's predicate
like any other crossing, and the implication is proved rather than read off the
names: a stronger refinement enters a weaker one, the converse is refused with
the failing goal named, and two spellings of one predicate cross while two
arithmetically related predicates cross only because the kernel relates them.
Verification identity is what a refinement means, not how it is spelled.

A call that takes a pointer to non-const may write through it, so what the
caller knew about the pointee does not survive the call. A pointer is passed by
value, so the parameter keeps its own version while the storage it designates
goes stale; that distinction is what separates this from the by-reference case,
and missing it once let a contract promising a positive result verify while
returning zero. A pointee reached only through a pointer to const survives,
because writing through one is not something the callee may do.

Methods are refused at the declaration, which is what closes virtual dispatch
rather than leaving it open: the dynamic type decides which body runs, so a
contract proved from a base's body would not cover an override that replaces
it. A refinement does not cross a translation unit on a declaration's word
either. An ordinary function's refined return is refused as evidence at the
boundary, including through a header, so the only way a refined value enters is
where its predicate was proved.

Binding a conditional to a local splits the route on its condition, so each arm
is proved under what its own path supposes rather than as one opaque `select`
term. An arm that is itself a conditional splits again, and a refinement
crossing may be discharged arm by arm. This adds proof power and no fact: a
single failing arm still rejects the binding, and a guarded arm supposes only
what its condition states.

Which conditional a route splits on follows what the bound value denotes, not
how it is written. A read denotes the value its version was given, so resolution
follows reads transitively to any depth: a conditional reached through any
number of intervening locals splits as a directly written one does, and a
conditional whose arm reads an earlier conditional local resolves through it.
Resolution is bounded without a fixed hop limit, because a version's value reads
only versions established before it, and it does not cross a version boundary.

`&&`, `||` and `!` are modeled in a verified condition. They are not lowered as
values — a proposition is not a value, and outside a condition they stay refused
— but elaborated into the routes they select between, recursively, so nesting
works to any depth. This models C++ short-circuit evaluation exactly rather than
approximating it: an operand appears only on the routes where C++ evaluates it.
The route where `A && B` fails is the union of `!A` and `A && !B`, never one
route supposing both sides false, and the route where `A || B` holds is likewise
a union that establishes neither side alone. `e2e_refinement_flow` pins the
proven crossings together with the guards showing the added power creates no
fact; `negative_verified_paths` pins the false-route behavior.

Nested effectful expressions without represented C++ sequencing are rejected.
Route splitting and proof composition derive branch structure separately and
must agree, so a body whose splits cannot be kept in step with the `select`
nesting of its lowered value is refused rather than proven. These are
implementation gaps, not completed capability.

Pointer dereference resolves to a place and reads and writes through the common
machinery. `*p`, `*p = e`, `p->m` and `p[i]` all form a `Deref` place rooted in
the pointer and the version whose value they dereference, so `*p` before and
after a write to `p` are different places. Forming one requires a capability,
which the contract states as `readable(p)` or `writable(p, n)` and nothing else
supplies: `p != nullptr` establishes neither, and a failed capability is a
diagnostic rather than a silent assumption (`SPEC.md` 12.10 VERIFIED-037,
VERIFIED-043). A pointer computed by arithmetic or returned by a call names no
place this implementation can identify and stays refused. Reading requires
`readable` and writing requires `writable`; neither entails the other.

`readable` and `writable` are built-in specification propositions, not calls to
user functions, and they never become runtime calls. They are recognized
contextually, so ordinary C++ that already spells a function or variable
`readable` keeps its own meaning. Because a contract states one `expects`
clause, several capabilities are written joined by `&&`; mixing a capability
with an ordinary predicate in one clause is refused, since the two belong to
different channels.

A capability never reaches the proof kernel. It is a property of the execution
state rather than a computable function of any value, so encoding it as a term
would need an uninterpreted constant and adding a proposition former for it
would put memory semantics inside the trusted kernel (RFC 0014 §10). Instead the
obligation layer carries capabilities as context hypotheses, structurally
separated: `vir::Capability` is deliberately not a node of `Expr`, so there is
no path from a capability to the kernel's proposition language. Capability
tracking is a correspondence-layer responsibility and carries a stated TCB delta
(`TRUST.md` 15); it adds no kernel rule, axiom or logical assumption.

Bounds are the opposite case and are *proved*. A symbolic subscript forms a
symbolic element place and owes `index < extent`. Both sides are terms, so the
kernel checks it with the existing arithmetic rules. Two symbolic elements are
disjoint only when their indices are proved unequal: a write at `a[j]`
invalidates what was read at `a[i]` unless `i != j` is established, and a false
rejection is preferred to a stale fact. Refined elements owe their predicate at
every write, symbolic or not.

Identity is the separate question, with the opposite conservative answer, and it
is decided on the index term (`SPEC.md` STORAGE-010, `TRUST.md` TCB-ALIAS-006).
A place's path records that a step was symbolic, not which element it chose, so
two subscripts are one place only where the index terms are seen to be one term
read at one set of versions. `a[i]` and `a[j]` are therefore two places and
carry no fact between them, and an index reassigned between two accesses names
another element at the second. The comparison is structural and errs toward
difference: an index shape it does not decide gets its own place, which costs
precision and never soundness. One term, including a compound one such as
`a[i + 1]`, still names one place, so an element written is read back. Being two
places withholds a fact and does not make the storage disjoint, so a write at
either still invalidates the other, including an element selected at a constant.

Each subscript also owes the capability its own access needs, which is how a
region held only as `writable(p, n)` refuses a read of an element it just
wrote: `writable` does not entail `readable`, and writing an element first does
not earn it.

A symbolic element read now supplies the element type's refinement (`SPEC.md`
REFINE-060 to REFINE-062, `TRUST.md` TCB-REFINE-009). The value is unknown, but
it is unknown *within* the declared type: the array entered the modeled state
through an initializer that established validity for every element, and every
later write was modeled here and charged the same predicate, so the element's
current version holds a value of its type even though which element is
undecided. The predicate belongs to that version rather than to the storage, and
it is derived and not charged again — demanding it at the read would make the
body pay for one crossing twice. `a[i]` of a `Positive[3]` therefore proves
`result > 0`, and a refinement of a refinement supplies both predicates and no
third one.

The rule needs closed accounting for the writes that may reach the location, so
this implementation applies it only where it has that: a local array, entered
through an aggregate initializer, whose address it never let escape. Indirection
is not what disqualifies a location — the spec allows the rule to reach storage
behind an alias once the accounting is closed — but this implementation does not
close it there, so a pointee gets nothing, and a pointer to a refined type is
never itself the evidence. The escape test is deliberately stricter than the one
aliasing uses: the array-to-pointer decay a subscript performs on its own base is
not an escape, while a decay into a call argument or into pointer arithmetic is.
Only the pointee limit is observable today; storage a reference parameter
designates and a local whose address escapes describe bodies this implementation
already refuses for containing an unmodeled `&` or decay, so those conditions are
the contract for when those forms are modeled rather than gates that fire now.

A parameter passed by value owns a distinct parameter object, so its ordinary
contained subobjects are places of the callee and writing one is an ordinary
write (`SPEC.md` STORAGE-011). `s.x = 5; return s.x;` verifies, a sibling keeps
the value it arrived with without becoming known, a nested member is reached by a
longer path, and a refined member owes its predicate on the way in exactly as a
local's does. A write inside the parameter object does not reach the caller's
argument and does not reach another parameter.

Ownership stops at indirection, and this implementation stops well short of it:
a parameter whose type has a pointer or reference member is not tracked at all,
because such a member is not a modeled value type, so writing any member of such
a parameter is refused rather than treated as callee-owned. `*s.p` is therefore
never reached through this rule, and a fact about a pointee does not survive a
write through a pointer that may designate it. A parameter that may designate
caller storage gets none of this and is refused where it is written. A member
array of a by-value parameter is tracked only once something writes it, so
reading one at a symbolic index is still refused for an unknown extent.

The extent is a term rather than a count. A constant extent canonicalizes to a
literal, and the extent a capability states does not: `readable(a, n)` bounds a
region by a runtime value that no enumeration of elements can recover. A
subscript through a capability-held pointer therefore owes `index < n` against
that term, and the bound is proved the same way an array's is. The capability
and the bound stay on their separate channels: the capability permits reaching
the region, and the arithmetic decides which element was named.

The one-object form states no extent at all, so it bounds no element and a
subscript under it fails closed. An unstated extent is not an unbounded one.
An index and an extent of different integer types are refused rather than
converted, because the conversion between them is not modeled.

The obligation is generated wherever the index is a term, and the extent comes
from the array's resolved type rather than from whichever of its elements an
earlier access happened to form. A symbolic subscript into `const T (&a)[N]`
is therefore bounded without any prior `a[0]`, and a dependent extent states
the same obligation at each specialization's own substituted `N`: `i < 8` does
not bound an index into an array of 4.

Two routes reach an indexed array, and they answer different questions
(`ARCHITECTURE.md` 21). A C++ expression that denotes storage takes the Place
route, which owns capabilities, versions, writes and aliasing; a failure to
build that Place fails closed rather than falling back. An array already held
as a formal value takes the indexed observation of `FOUNDATIONS.md` 45, which
is read-only and has no version of its own. Both prove the same bound against
the same index term.

Observing an element establishes nothing about the index: the observation is
total, and `index < extent` is owed separately. An index Clang folds to a
constant outside the extent is refused as the decided out-of-bounds access it
is, rather than becoming an obligation that merely fails to prove. Indexed
observation admits neither injectivity nor extensionality, so equal elements
never prove equal indices or equal arrays.

A capability names the pointer whose storage it describes, `readable(p)`, as
`SPEC.md` 12.10 states it. RFC 0014 §12 writes the same capability over the
place, `readable(*p)`; that spelling is refused by name, because projecting it
as written would emit the dereference the capability exists to permit.

Pointer values and proof-side `null`/`non_null` case analysis are `IMPLEMENTED`
and unaffected. Reference capture and returned aliases remain sequenced behind
the rest of the storage model (RFC 0014 §17).

Steps 1 to 7 of that model are implemented. A place is a root - a local, the
referent a by-reference parameter designates, or the pointee a pointer
designates - and a path of projections into it, so a member of a member is an
ordinary place rather than a special case.
`PlaceVersion` and `PlaceRef` are the only version and read nodes in the VIR;
there is no parallel local-only path. Every read resolves a place to its current
version through one mechanism, and every write - a declaration, an assignment, a
compound update, an increment, a member initialization, a write through a
reference - establishes a version through one mechanism, which is where the
refinement crossing is generated.

Aliasing is conservative and proved only from what Clang resolves: distinct
locals never share storage, and within one object paths that differ at some step
select different members. A write to an object reaches the members inside it and
a write to a member reaches the object it belongs to, because they are the same
storage at different granularity. Two by-reference parameters may designate one
object, so a write through either havocs the other. A dereference is the
conservative case: two dereferences of different pointers may always alias, and
a dereference may reach any local whose address the body takes, which Clang
resolves. A local whose address is never taken cannot be a pointee and is left
alone. No type-based argument is used: strict aliasing presupposes the
undefined-behavior freedom a proof has not established.

An array local is the same model: it is a record whose members are its elements,
so a constant index names a place and a write reaches exactly that element. A
variable index names a symbolic element place instead, which is not resolved to
any particular element: it owes its bound, its value is opaque, and it is never
concluded disjoint from a sibling. Only aggregate initialization is admitted for
a tracked record, because a constructor call or default initialization would
leave a tracked member holding a value the body cannot state.

The same membership checks cover partial-correctness bodies containing loops and
their callers, including unused refined locals. Corrupt or unresolved refinement
metadata fails closed. This closes a verification gap without promoting the
overall refinement feature beyond `PROTOTYPE`.

A verified refined return can now supply the postcondition without a repeated
`ensures`. Ordinary refined-return declarations and unverified refined storage
are diagnosed explicitly. Mutable object construction remains outside the body
model; this boundary audit does not implement refined fields or aggregates.

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
| Case splits on a runtime path               | `IMPLEMENTED` |
| Case facts invalidated with their version   | `IMPLEMENTED` |
| Impossible cases (`omit ... by ...`)        | `PROTOTYPE`   |
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
| `trusted law`                       | `IMPLEMENTED` |
| Trusted memory propositions         | `NOT STARTED` |
| Unsafe-to-verified transition rules | `SPECIFIED`   |
| Trust propagation                   | `IMPLEMENTED` |
| Assumption closure                  | `IMPLEMENTED` |
| Trust reporting                     | `PARTIAL`     |
| Trust report implementation         | `PARTIAL`     |

The intended verification statuses are:

```text
PROVEN
TRUSTED
RUNTIME-CHECKED
UNSAFE
UNVERIFIED
UNRESOLVED
```

`PROVEN` and `TRUSTED` are distinct implemented states, and `UNRESOLVED` is the
fail-closed default. A `trusted law` is assumed rather than proved and is named
in the trust report with its location and a content-derived identity; writing a
proof for one is refused, since the declaration would ask both to assume and to
prove it.

A proof uses a trusted law by naming it in `exact`, `apply`, `rewrite` or
`contradiction` (`SPEC.md` TRUSTED-006 to TRUSTED-009, PROOFSRC-005). The proof
is then established relative to it: its evidence is closed over the law's
proposition as a premise, and the kernel checks the claim under exactly the
premises its verdict names, so a claim cannot use an assumption it is not
reported as resting on. The law's own premise is still owed, and the law is not
a premise anywhere it is not named. A proof that uses such a proof rests on the
same laws, an omitted case rests on those of the proof it is written in, a
runtime path claimed not to occur rests on those of the proof it names, and a
verified function's contract rests on those of the obligations of its body and
of every contract it calls, closed to a fixed point over recursive calls. Each
proven law, proof declaration, proof of a law instance, contract, omitted case
and impossible path is reported with its closure, marked as named directly or
reached through what it uses, and each category is split into
`assumption-free` and `relative to trusted laws`. Trusted laws nothing rests on
are listed as unused. A dependency the report cannot attribute is an internal
error, not an omission.

A runtime path claim is the one way a trusted premise enters a verified body, so
a contract rests on a trusted law only through one, in its own body or in a
function it calls. Recursive call graphs, which no source program here yet
produces with a trusted premise, are exercised by
`tests/unit/trust_closure_test.cpp`. Trust is propagated within one translation
unit: no proof artifact or cache carries a closure across units yet.
A trusted law cannot yet admit a memory proposition such as `readable(p)`
(`SPEC.md` TRUSTED-003); one is refused.

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
| ABI-equivalence tests            | `PARTIAL`     |
| Formal erasure correctness proof | `NOT STARTED` |

Erasure currently removes law and trusted-law declarations, proofs, contracts,
loop invariants and measures, and the `verified` and `pure` specifiers; reduces a
claim that a path cannot occur to the empty statement its `;` leaves; and lowers
a refinement type declaration to the alias it means. The implementation checks a
strong property rather than asserting success: the runtime program must be the
analysed program with every proof-only span blanked, keeping only its newlines,
each runtime-bearing declaration replaced by the canonical C++ recomputed from
that declaration, and nothing else changed, with line numbering unchanged
(`TRUST.md` 29). The text Clang compiles is written only after that check, from
the text checked. Equivalence is therefore established structurally for the
constructs implemented, not proven in general.

The check trusts the recognizer's spans. What shows a span wrong is comparison
with programs written without C++L: each construct family has a C++L fixture and
its erasure written by hand, and the two must print the same and compile to
identical assembly at `-O0` and `-O2` in `c++17`, `c++20` and `c++23`
(`tests/e2e/erasure_equivalence.sh`). ABI equivalence is tested the same way for
a library whose interface uses refinements and contracts, and by linking an
ordinary C++ client, compiled by Clang alone, against it
(`tests/e2e/abi_equivalence.sh`). Both are `PARTIAL`: they cover the constructs
this implementation accepts, on the host's ABI; ghost state, `unsafe` and
cross-translation-unit verification metadata are not implemented, and are
refused rather than erased (`tests/negative/erasure.sh`).

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

| Tool                                 | Status        |
| ------------------------------------ | ------------- |
| Clang-compatible driver              | `PROTOTYPE`   |
| `cppl build`                         | `SPECIFIED`   |
| `cppl check`                         | `SPECIFIED`   |
| `cppl prove`                         | `SPECIFIED`   |
| `cppl explain`                       | `SPECIFIED`   |
| `cppl trust-report`                  | `PARTIAL`     |
| LSP: sync and diagnostics            | `PARTIAL`     |
| LSP/CLI: canonical clause formatting | `PROTOTYPE`   |
| LSP: case completion and hover       | `PROTOTYPE`   |
| LSP: C++ and C++L completion         | `PROTOTYPE`   |
| LSP: signature help                  | `PROTOTYPE`   |
| LSP: hover over C++ and C++L names   | `PROTOTYPE`   |
| LSP: verification status in editors  | `PROTOTYPE`   |
| LSP: code actions                    | `PROTOTYPE`   |
| LSP: semantic tokens (all names)     | `PROTOTYPE`   |
| LSP: definition and declaration      | `PROTOTYPE`   |
| LSP: references and highlights       | `PROTOTYPE`   |
| LSP: document outline                | `PROTOTYPE`   |
| LSP: folding and selection ranges    | `PROTOTYPE`   |
| LSP: build flags (compile_commands)  | `PROTOTYPE`   |
| LSP: inlay hints                     | `PROTOTYPE`   |
| LSP: background compiles, cancel     | `PROTOTYPE`   |
| LSP: workspace index and symbols     | `PROTOTYPE`   |
| LSP: rename                          | `PROTOTYPE`   |
| IDE proof goals                      | `PROTOTYPE`   |
| Proof navigation                     | `PROTOTYPE`   |
| Counterexample UI                    | `NOT STARTED` |
| Structured diagnostics               | `PROTOTYPE`   |

The driver is Clang-compatible rather than subcommand-based: `cppl` takes the
arguments `clang++` takes. Trust reporting exists as `--cppl-trust-report`; the
subcommand forms above are not implemented. The report counts partial-
correctness contracts and loop-invariant obligations separately, lists every
proven claim that rests on a trusted law with each law it rests on, and lists
the trusted laws nothing rests on. It is text only: there is no
machine-readable form yet, and it prints no proposition or evidence hashes
(`TRUST.md` 36.1).

`cppl-lsp` implements `initialize`, `shutdown`, `exit`, incrementally synced
`textDocument/didOpen`, `didChange` and `didClose`, and
`textDocument/publishDiagnostics`. Diagnostics come from the ordinary compile
pipeline over the live buffer (`driver::compile_buffer`), so the server has no
decomposition, exhaustiveness or verification engine of its own; a structural
linter adds contextual C++L checks over the same recognized syntax rather than
re-recognizing it. The buffer compile names the buffer by the document's own
path, so a diagnostic is shown where it was written, at the column its author
wrote it at, and one located in an included header is shown on the document's
`#include` that brought the header in, with the header's location as related
information. Compiles run in the background, a change's once typing pauses for
300 ms, so requests are answered while one runs. A compile of text since edited
is dropped. A request withdrawn while it waits its turn is answered as
cancelled, each compile is reported as work in progress to a client that shows
it, and after a compile the client is asked to fetch its code lenses and
semantic tokens again. Each document is compiled, and read by its editor unit,
with the
flags its build gives it in the nearest `compile_commands.json`, followed by
the server's `--clang-arg` flags. A quoted `#include` is looked for beside the
document. Transport is separate from analysis, and the library is tested
without an editor. The server also advertises
`documentFormattingProvider`, `documentRangeFormattingProvider` and
`documentOnTypeFormattingProvider`, backed by one shared `compiler/formatter`
engine that also backs the standalone `cppl-format` CLI: `expects`, `ensures`,
`invariant` and `proves` clauses are relocated onto their own canonically
indented line, ordinary C++ layout is delegated to `clang-format`, and a
`check_style` pass reuses the same clause-placement rule to add style warnings
to `publishDiagnostics`. `codeActionProvider` serves the same engine's syntax
migrations as quick fixes where their edits land, and canonical formatting as
`source.fixAll.cppl`, computed only when asked for rather than as the cursor
moves.

`completionProvider` and `hoverProvider` are advertised and serve C++L's own
syntax: inside a `cases`/`decompose` arm block, completion offers each state
the subject's provider lists that the statement has no arm for yet — the
residual state included, since it is a real semantic state and not a catch-all
— and each item inserts an arm carrying the provider's own binder names. Hover
names the subject's resolved representation, its provider, and the full
partition with written arms marked; an omitted case is marked as claimed
impossible, not as proven. Both read the states the compiler's case
engine recorded while elaborating the buffer (`elaboration::SubjectStates`),
so the server still has no decomposition or exhaustiveness engine of its own;
where the compiler has not confirmed a subject's states, they offer nothing
rather than guess. Elaboration runs on publish rather than per keystroke, so
offered labels may lag the buffer by one edit. A case split in a verified body
is served the same way, from the states the compiler recorded while elaborating
the body.

Outside a case block, completion offers what Clang would accept at the position,
matched against what has been typed and ranked by Clang's own priority, with a
call's parameters as snippet placeholders for a client that takes snippets, and
never a name the projection generated. C++L's own words are offered as snippets
where the compiler's recognizer says they may be written: declarations at
namespace scope, proof statements at a statement's start, the assumptions in
scope, trusted Laws and proofs a statement can name after `exact`, `apply`,
`rewrite` or `contradiction`, and the clauses a declaration may still take.
The recognizer reads text still being written through its draft mode, which
keeps a Law or a proof not yet written whole and reads past a statement it
cannot read; the server reads no C++L grammar of its own. A draft has no
authority (ARCH-LSP-007): nothing only a draft keeps is offered as evidence,
and elaboration refuses a syntax that holds any of it.

`signatureHelpProvider` shows, while a call's arguments are written, every
declaration Clang says the call could resolve to, with the argument being
written marked.

`documentSymbolProvider` outlines the document: every declaration Clang finds
whose name the document writes outside a function body, nested as declared,
with each Law, proof and refinement type the compiler's recognizer finds placed
among them where it is written. A declaration the projection generated is never
in it. A client that cannot nest an outline gets a flat list naming each
entry's container.

`foldingRangeProvider` folds what Clang parsed:

- each body between its braces;
- each run of `#include`s, as imports;
- each branch of a conditional directive, as a region, paired from the
  directives Clang lexed.

It also folds each C++L proof body, arm block, arm body, bodiless Law and
refinement type that the recognizer found, and each comment block or run of
whole-line comments that the frontend's lexer passed over.

`inlayHintProvider` labels each argument with the name of its parameter and
each variable declared `auto` with its deduced type, from Clang, where the text
was written. An argument that already spells the name, a default argument, an
overloaded operator's operands and a call a macro's body writes get none.

`selectionRangeProvider` grows a selection from the token under the cursor. It
goes through each construct Clang parsed and each C++L span the recognizer
recorded: clause, statement, arm, body, declaration. The server reads no
structure from the text itself.

Outside a case block, hover describes any name. For C++ it shows what Clang
reports: the declaration's kind and qualified name, the declaration without a
body, a variable's type, a constant's value where Clang evaluates it, a type's
size and alignment, the declaration's comment, and the file it is declared in.
For a name that stands for a C++L declaration -- a Law, a proof, a refinement
type, a verified function, a name `assume` binds -- it shows the declaration as
written rather than what the projection generated for it. `result` and `self`
are explained, not shown as the generated parameters they are to Clang.

`codeLensProvider` states, over each Law, proof and verified function the
document writes, what became of its obligations in the last compile, and hover
over any of them lists each obligation's status, goal, trusted premises and
evidence, or why it is not proven. The compile copies these out of the
verdicts after the kernel has decided (`driver::ObligationRecord`), so an
editor says `PROVEN` only where the trust report would; a verdict is shown only
for the buffer version it was computed for. This is the goal of each
obligation, not an interactive proof state: the goal a proof has reached after
each of its statements is not reported. Counterexamples are not reported
because nothing in the compiler produces one.

`semanticTokensProvider` colors every name by what it names.

- **C++ names come from Clang.** The types are namespace, type, class, struct,
  enum, type parameter, parameter, variable, field, enumerator, function,
  method and macro. The modifiers are declaration, readonly, static member,
  deprecated and system-header.
- **C++L comes from the recognizer.** Every C++L word is a keyword. The names of
  Laws, proofs and refinement types are declared, and so is each name `assume`
  binds. The names proof statements use are colored as the compile resolved
  them.

Positions are the ones recorded in the buffer as written, and a name inside a
Law's proposition is where the Law writes it. This includes the proof
statements the editors' TextMate grammar cannot tell from C++ declarations,
such as `exact h;` and `contradiction name;`. A `contradiction` statement in a
verified body is reported only when the compile
of the whole unit, headers included, also recognized claims, since a header
that names `contradiction` makes the statement ordinary C++ (WORD-002). A case
split in a verified body, with the keywords of its arms, is reported on the same
terms, when the compile of the whole unit recognized splits (WORD-012).

`definitionProvider`, `declarationProvider`, `typeDefinitionProvider` and
`implementationProvider` are answered by Clang, through libclang, over an
editor unit per document: the analysis projection the compiler makes, made from
the buffer as written so that its `#include`s stay directives, kept with a
precompiled preamble and reparsed after an edit. A header that holds C++L, and
every other open buffer, is read as its projection. The projection records
where it kept written text in place and where it copied written text into a
declaration it generated, so a name inside a Law's proposition, a Law's
parameter, a parameter named in a contract, a Law named by a proof's `proves`
clause and a refinement type all lead to what was written; a position Clang
reports in generated text that stands for nothing written is not shown.

A name a proof statement uses (`exact p;`, `apply p;`, `rewrite h;`,
`contradiction e;`, the evidence of an omitted case) is not C++. Elaboration
records what it resolved each one to, a proof, a trusted Law or an `assume`
binding, and where that is declared (`elaboration::ResolvedName`), as a
byproduct nothing in the compiler reads; the server navigates and lists
references from those records, and only where the buffer still spells the name
where it was recorded.

`referencesProvider` and `documentHighlightProvider` are answered from the same
units: every open document's unit reports where a name, identified by Clang's
USR, is written in it and in the headers it includes, and a parameter the
projection repeats in several generated declarations counts as the one the
author wrote. Highlights mark declarations, reads and writes.

A workspace index (`lsp::WorkspaceIndex`) reads every other file of the folders
the client opened, and each file a compilation database there lists. It reads
each as an open document of it would be read, several at once at a lower
priority than the editor's requests: by Clang through its projection with its
build's flags, and by the compile as far as elaboration. Files edited, added or removed on disk are read again within 2
seconds. References reach those files, and `workspace/symbol` lists every
declaration an outline would show in the open documents and the index. An open
document always answers as the editor holds it (ARCH-LSP-009), and each pass
that reads files is reported as progress.

Rename (`textDocument/prepareRename`, `textDocument/rename`) rewrites every
place references finds, declarations and a class's constructors and destructor
included, whole or not at all. It refuses, with the reason, a new name that is
no C++ identifier or is a keyword, a place in a file neither open nor indexed,
a place that no longer spells the name, a use a macro's body spells, a C++L
word written inside C++L, and any edit after which the recognizer reads a
file's C++L differently (ARCH-LSP-010). Collisions with other declarations are
not checked; the compile reports them.

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
| Trust transitivity          | `IMPLEMENTED` |
| Solver trust reporting      | `SPECIFIED`   |
| FFI trust reporting         | `NOT STARTED` |
| Per-Law assumption closure  | `IMPLEMENTED` |
| Trust-report implementation | `PARTIAL`     |

The TCB is stated in `TRUST.md` 4 and 5. There are no axioms. A
`trusted law` (`SPEC.md` 27) is the one implemented way to admit a proposition
without proof: it is stated to the formal core, given status `TRUSTED` rather
than `PROVEN`, never counted among proven laws, and named individually in the
trust report with its source location. Declaring a law trusted and also writing
a proof for it is refused. A build with no such declaration reports zero trusted
axioms, because that is true of it. The trust report prints counts it can
substantiate, and says _not analysed_ where C++L does not yet look.

Transitivity and per-claim closure are described under
[Unsafe and trusted boundary status](#unsafe-and-trusted-boundary-status). The
closure of a claim is the set of premises the kernel checked it relative to,
joined across verified calls; the reporting code that computes and prints it is
reporting TCB (`TRUST.md` TCB-REPORT-006) and fails the build rather than
report a claim it cannot account for.

See [TRUST.md](./TRUST.md).

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

See [SECURITY.md](../SECURITY.md).

---

# Documentation status

| Document                   | Status        |
| -------------------------- | ------------- |
| `README.md`                | `SPECIFIED`   |
| `SPEC.md`                  | `SPECIFIED`   |
| `GRAMMAR.md`               | `SPECIFIED`   |
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
providers built on it are implemented and listed above, over proof parameters
and, through case splits on a runtime path, over values read from storage. The
language server offers only what
[Developer tooling status](#developer-tooling-status) lists.
