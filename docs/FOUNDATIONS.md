# C++L Mathematical Foundations

**C++L — Formal Calculus and Mathematical Model**

Status: Formal foundations reference

This document defines the mathematical model underlying C++L's proof system.

It formalizes the logic, typing judgments, proof judgments, equality model,
quantification, machine-number model, mathematical domains, refinement validity,
program-state reasoning, structural proof principles, termination principles, and
the relationship between formal evidence and executable C++ semantics.

It is not the source-language specification.

`SPEC.md` is authoritative for:

- source syntax;
- which C++L constructs exist;
- when they are well-formed;
- source-level verification obligations;
- erasure and runtime semantics.

`GRAMMAR.md` is authoritative for concrete grammar.

`TRUST.md` is authoritative for:

- the Trusted Computing Base;
- which correspondence components must be trusted;
- trusted-assumption provenance;
- assurance reporting.

`ARCHITECTURE.md` is authoritative for implementation component boundaries and
data flow.

This document is authoritative for the **formal interpretation** of the proof
concepts that `SPEC.md` exposes, but it MUST NOT add a source feature, proof
escape hatch, runtime behavior, or assumption that `SPEC.md` does not define.

When a mathematical presentation in this document and source-level wording in
`SPEC.md` appear to disagree, `SPEC.md` determines the language meaning and this
document must be corrected.

---

# 1. Foundational objective

C++L needs a formal layer strong enough to express and check claims about real C++
programs without replacing C++ execution with a second programming language.

The foundational problem is therefore not merely:

```text
define a logic
```

It is:

```text
define a logic
+
define how verified C++ states and values are represented in that logic
+
preserve the distinction between proof and assumption
+
preserve the distinction between mathematical abstraction and machine execution
+
permit proof evidence to erase without changing runtime behavior
```

The central relationship is:

```text
C++ execution
    ↓ modeled by sound correspondence
formal terms / state observations
    ↓ used in
propositions
    ↓ established by
proof evidence
```

A proof is meaningful for C++ only when every correspondence step required by
that proof is sound. The proof calculus itself does not make correspondence bugs
impossible; that boundary is made explicit by `TRUST.md`.

---

# 2. Foundational invariants

The following principles constrain the formal model.

## 2.1 No hidden truth

A proposition may become usable as established evidence only through:

```text
checked proof
```

or through:

```text
an explicit trusted assumption permitted by SPEC.md
```

Failure, timeout, unsupported semantics, `unsafe`, runtime assertions, compiler
recovery, and unverified code do not create proof evidence.

## 2.2 Proof and computation remain distinct

Runtime C++ values and proof evidence may correspond, but they are not the same
semantic category.

A C++ `bool` is a runtime value.

A proposition is a formal statement.

A proof is evidence for a proposition.

C++L may define a lifting from a side-effect-free C++ Boolean expression to a
proposition, but that lifting does not identify `bool` with `Prop`.

## 2.3 Machine values are not silently mathematical values

Machine integers, pointers, floating-point values, arrays, records, library
objects, and other C++ values retain their selected C++ semantics.

Mathematical domains are explicit.

No proof may silently replace bounded machine arithmetic by unbounded integer
arithmetic or replace a C++ object by an abstract mathematical value without an
established correspondence.

## 2.4 Proof-relevant computation is total

Any computation used by definitional equality, normalization, proof construction,
or another proof-relevant reduction must terminate on the inputs for which it is
used.

Runtime C++ may diverge where `SPEC.md` permits partial correctness.

Divergence is never evidence.

## 2.5 Runtime proof irrelevance is not an axiom of logical proof irrelevance

Proof evidence has no runtime observational identity after erasure.

This does **not** require the formal calculus to assert that every two proofs of
the same proposition are definitionally equal.

C++L needs runtime erasure of proof evidence, not an unnecessary proof-irrelevance
axiom.

## 2.6 No implicit classical logic

The foundational logic is constructive unless a classical principle is supplied
by:

- a derivation from the formal model;
- a decidability result for the relevant finite/decidable domain;
- another normative C++L rule;
- an explicit trusted Law.

In particular:

```text
P || !P
```

is not a universal primitive axiom.

## 2.7 Formal soundness is fail-closed

When a formal operation cannot be represented soundly, the implementation must
leave it opaque or reject the stronger proof claim.

Approximation may lose proving power.

It must never gain proving power by changing meaning.

---

# 3. Intellectual lineage

C++L does not claim to invent its mathematical foundations.

The system draws on established work in logic, programming-language theory,
program verification, theorem proving, type theory, and automated reasoning.

Important intellectual lineages include:

- Gerhard Gentzen — natural deduction and structural proof systems;
- Alonzo Church — lambda calculus and foundational links between logic and
  computation;
- Haskell Curry and William Alvin Howard — propositions-as-types and
  proofs-as-programs;
- Nicolaas de Bruijn — AUTOMATH and machine-checked formal mathematics;
- Per Martin-Löf — constructive dependent type theory and identity/equality
  reasoning;
- Robert W. Floyd and C. A. R. Hoare — axiomatic program correctness;
- Edsger W. Dijkstra — weakest preconditions and predicate transformers;
- Thierry Coquand and Gérard Huet — the Calculus of Constructions;
- Tim Freeman and Frank Pfenning, together with later researchers — refinement
  typing;
- the SAT/SMT and decision-procedure communities, including the Nelson–Oppen
  tradition and modern systems such as Z3 and cvc5;
- the designers and communities behind proof-oriented systems including
  Rocq/Coq, Lean, Agda, Idris, F\*, Dafny, and related work.

C++L's intended contribution is the integration of these ideas with the execution
semantics, source compatibility, ABI requirements, and tooling ecosystem of C++.

---

# 4. Metasyntax

The notation in this document is mathematical notation.

It is **not** C++L source syntax unless explicitly identified as such.

We use:

```text
T, U, V          formal/value types
x, y, z          variables
t, u, v          formal terms
P, Q, R          propositions
e                 proof evidence
Γ                 logical/type context
Ω                 storage/capability context
Σ                 abstract program state
Δ                 explicit trusted-assumption environment
ρ                 substitution/environment
p                 C++L source program
erase(p)          runtime C++ projection
```

The primary judgments are written conceptually as:

```text
Γ ⊢ T type
Γ ⊢ t : T
Γ ⊢ P prop
Γ ⊢ e : Proof<P>
Γ; Ω ⊢ obligation
Σ ⊨ capability
```

An implementation does not need to use these exact data structures or symbols.

The judgments define the mathematical obligations its implementation must
preserve.

---

# 5. Semantic strata

C++L has three major semantic strata.

## 5.1 Runtime stratum

The runtime stratum contains ordinary executable C++:

```text
objects
values
storage
lifetime
calls
control flow
exceptions
threads
atomics
I/O
foreign interactions
```

Its semantics are the selected supported C++ semantics.

## 5.2 Specification stratum

The specification stratum describes claims about runtime values and states.

It includes:

- Boolean propositions lifted from modeled C++ expressions;
- formal equality;
- logical connectives;
- quantifiers;
- refinement predicates;
- contracts;
- loop invariants;
- termination measures;
- mathematical-domain expressions;
- memory propositions such as `readable(...)` and `writable(...)`.

Specification expressions have no runtime effect merely because they are used as
specifications.

## 5.3 Proof stratum

The proof stratum contains:

- proof evidence;
- proof binders;
- hypotheses;
- proof declarations;
- proof-only case analysis;
- induction evidence;
- trusted-assumption provenance.

Proof-stratum entities erase unless `SPEC.md` separately assigns runtime meaning.

---

# 6. Types in the formal model

The formal system distinguishes several kinds of types.

## 6.1 Modeled C++ value types

A C++ type may participate directly in formal reasoning when the verifier has a
sound formal model for the values and operations used by the proof.

Examples include, subject to `SPEC.md`:

- modeled integral types;
- `bool`;
- enumerations through their C++ value set;
- pointers as pointer values, not integer addresses;
- records through permitted abstract observations;
- selected standard-library types through formal models.

Not every legal C++ type must automatically be a fully modelable formal value
type.

## 6.2 Refinement types

A refinement:

```text
{ x : T | P(x) }
```

has verification identity beyond `T` while erasing to the runtime representation
of its ultimate base type.

## 6.3 Proof-only mathematical domains

The closed core set defined by `SPEC.md` is:

```text
@N
@Z
@Seq<T>
@Set<T>
@Map<K, V>
```

These domains have no runtime storage, lifetime, layout, address, or ABI.

## 6.4 Abstract nominal sorts

A modeled C++ representation may be assigned an abstract nominal formal sort with
typed observations.

Such a sort represents only the observations the formal model intentionally
exposes.

It does not imply private object layout.

## 6.5 Proposition kind

`Prop` is the semantic category of propositions.

Conceptually:

```text
P : Prop
```

`Prop` is not ordinary runtime C++ `bool`.

---

# 7. Contexts

## 7.1 Logical context

A logical context `Γ` contains typed binders and checked hypotheses.

Conceptually:

```text
Γ =
    x1 : T1,
    ...
    xn : Tn,
    h1 : P1,
    ...
    hm : Pm
```

A hypothesis exists only when introduced by a valid formal rule or by the
translation of a source premise defined by `SPEC.md`.

## 7.2 Storage/capability context

A storage context `Ω` contains proof-relevant facts about the current modeled
program state that are not naturally value propositions in the logical kernel.

Examples include state-sensitive facts such as:

```text
region r is live
place l is initialized
access through pointer p is readable
access through pointer p is writable
extent/provenance relation associated with region r
```

At source level, `readable(...)` and `writable(...)` are C++L specification
propositions.

An implementation may discharge those propositions through a dedicated
state/capability checker rather than represent them as primitive kernel
propositions.

The distinction is implementation-independent:

```text
surface specification proposition
    may denote
state-sensitive capability obligation
```

but:

```text
state-sensitive capability obligation
    must not become
an invented kernel axiom
```

## 7.3 Trusted-assumption environment

`Δ` records explicit trusted premises introduced by the production trusted
surface defined in `SPEC.md`.

A proposition derived using an element of `Δ` remains assumption-relative.

The transitive dependency on `Δ` is trust provenance, not a proof rule that
erases the assumption.

---

# 8. Well-formedness

A formal term or proposition is well-formed only if:

- every free variable is bound;
- every referenced type is well-formed;
- every formal operation is applied to operands of the required types;
- every C++-derived term has a sound correspondence to the resolved C++ value it
  models;
- every specification operation is defined under the current proof context;
- proof-only values do not require runtime semantics they do not have.

Ill-formed formal syntax is rejected.

There is no principle:

```text
malformed but probably intended
    ⇒
usable proof evidence
```

---

# 9. Substitution

Capture-avoiding substitution is fundamental.

We write:

```text
P[t / x]
```

for proposition `P` with well-typed term `t` substituted for free occurrences of
binder `x`.

Substitution must preserve:

- binding scope;
- type correctness;
- refinement obligations;
- mathematical-domain distinctions;
- abstract-sort identity.

For a well-formed substitution:

```text
Γ, x : T ⊢ P prop
Γ ⊢ t : T
```

the result must satisfy:

```text
Γ ⊢ P[t / x] prop
```

The same principle applies to terms and proof contexts.

An implementation may use names, unique IDs, de Bruijn indices, locally nameless
representation, or another technique.

The representation must make capture impossible or detect and reject it.

---

# 10. Weakening, exchange, and scope

Formal reasoning may use ordinary structural properties only where they preserve
the meaning of the context.

A hypothesis may be used only within the scope in which it exists.

Proof-local binders do not escape.

A hypothesis introduced in one control-flow path is not automatically available
on another path.

A case-arm discriminator is local to that arm.

An induction hypothesis is local to the corresponding induction case.

A function precondition is a premise of the function theorem; it is not a global
fact.

---

# 11. Curry–Howard interpretation

The conceptual interpretation is:

```text
proposition
    ≈
type of evidence

proof
    ≈
inhabitant of that evidence type
```

For a proposition `P`:

```text
Proof<P>
```

denotes the semantic type of evidence establishing `P`.

C++L source need not expose `Proof<P>` as ordinary runtime syntax for this
interpretation to be useful.

The proof calculus checks inhabitation conceptually:

```text
Γ ⊢ e : Proof<P>
```

A false proposition has no valid evidence unless the assumption environment is
inconsistent or the Trusted Computing Base is unsound.

---

# 12. Propositions

The foundational proposition language includes the forms required by `SPEC.md`.

Conceptually:

```text
Bool(e)
Eq<T>(t, u)
False
P && Q
P || Q
P -> Q
P <-> Q
forall (x : T), P
exists (x : T), P
memory propositions defined by SPEC.md
```

`False` has no surface spelling. It is the proposition a checked contradiction
establishes (§26), and nothing introduces it.

`P <-> Q` is derived:

```text
(P -> Q) && (Q -> P)
```

The exact surface spelling and precedence belong to `GRAMMAR.md`.

---

# 13. Boolean lifting

Let `e` be a side-effect-free modeled C++ expression whose resolved C++ type is
`bool`.

Its proposition lifting is conceptually:

```text
Bool(e)
```

with meaning:

```text
the selected C++ evaluation of e is defined and yields true
```

This requires two things:

1. the expression is defined under the verified path;
2. the result is true.

The lifting does not reinterpret overloaded operations.

Clang/C++ resolution determines what runtime expression `e` means.

The correspondence layer determines the formal term/condition representing that
meaning.

---

# 14. Definitional equality

Definitional equality is written:

```text
t ≡ u
```

It means that the formal reduction/normalization semantics give the same
canonical meaning to `t` and `u`.

Definitional equality is not a proposition requiring a theorem.

It is a checker judgment used while type-checking and validating evidence.

Required properties include:

```text
reflexive
symmetric
transitive
congruent under formal term constructors
stable under capture-avoiding substitution
deterministic
```

A checker must not declare terms definitionally equal merely because an external
solver says they are mathematically equal.

Solver-established equality is propositional evidence unless the equality is part
of the formally defined normalization theory.

---

# 15. Normalization

Normalization may reduce:

- admitted total formal definitions;
- primitive mathematical-domain operations;
- machine operations for which exact normalization is formally defined;
- structural term constructors;
- substitutions.

Normalization must not:

- execute arbitrary impure C++;
- assume termination of unchecked recursion;
- interpret undefined C++ behavior;
- call runtime services;
- depend on mutable process-global state;
- use host overflow as formal machine arithmetic.

If normalization cannot complete soundly within implementation resource limits,
the proof remains unresolved/rejected.

Resource exhaustion is not evidence.

---

# 16. Propositional equality

Formal equality is:

```text
Eq<T>(a, b)
```

with:

```text
Γ ⊢ a : T
Γ ⊢ b : T
----------------
Γ ⊢ Eq<T>(a, b) prop
```

Equality is intensional unless `SPEC.md`/the formal model explicitly supplies a
stronger extensional principle for a domain.

---

# 17. Equality reflexivity

Reflexivity is admissible when the two sides are definitionally equal.

Conceptually:

```text
Γ ⊢ t : T
-------------------------- Refl
Γ ⊢ refl : Proof<Eq<T>(t,t)>
```

More generally, if:

```text
t ≡ u
```

the checker may accept reflexive evidence for:

```text
Eq<T>(t,u)
```

according to the defined conversion rule.

---

# 18. Equality substitution

Equality gains its usefulness from substitution.

If:

```text
Γ ⊢ e1 : Proof<Eq<T>(a,b)>
Γ, x : T ⊢ P(x) prop
Γ ⊢ e2 : Proof<P(a)>
```

then equality elimination permits:

```text
Γ ⊢ subst(P, e1, e2) : Proof<P(b)>
```

The proposition context `P(-)` must be type-checked.

The checker, not an untrusted producer, is responsible for the formal
substitution that determines the resulting proposition.

---

# 19. Derived equality principles

From reflexivity and substitution, the system can derive familiar principles
without making each one primitive.

## 19.1 Symmetry

From:

```text
Eq<T>(a,b)
```

derive:

```text
Eq<T>(b,a)
```

by substituting through a context such as:

```text
Eq<T>(b, -)
```

starting from reflexivity of `b`.

## 19.2 Transitivity

From:

```text
Eq<T>(a,b)
Eq<T>(b,c)
```

derive:

```text
Eq<T>(a,c)
```

through substitution.

## 19.3 Congruence

For a well-typed formal function/constructor `f`:

```text
Eq<T>(a,b)
```

permits transport into:

```text
Eq<U>(f(a), f(b))
```

when the formal semantics of `f` support the corresponding substitution.

## 19.4 Dependent transport

If a type/proposition depends on a value, equality may transport evidence across
that dependency.

The transport is capture-safe and type-checked.

---

# 20. Implication

Implication is constructive.

The introduction rule is:

```text
Γ, h : P ⊢ e : Proof<Q>
-------------------------------- Imp-I
Γ ⊢ λh.e : Proof<P -> Q>
```

The premise is supposed within the subderivation.

It is not globally granted.

Elimination is:

```text
Γ ⊢ f : Proof<P -> Q>
Γ ⊢ p : Proof<P>
----------------------------- Imp-E
Γ ⊢ f(p) : Proof<Q>
```

This is the formal basis for:

```cpp
expects (P)
ensures (Q)
```

being interpreted as proving `Q` under premise `P`.

---

# 21. Conjunction

Conjunction introduction:

```text
Γ ⊢ p : Proof<P>
Γ ⊢ q : Proof<Q>
----------------------------- And-I
Γ ⊢ pair(p,q) : Proof<P && Q>
```

Elimination:

```text
Γ ⊢ e : Proof<P && Q>
---------------------- And-E1
Γ ⊢ left(e) : Proof<P>
```

and:

```text
Γ ⊢ e : Proof<P && Q>
---------------------- And-E2
Γ ⊢ right(e) : Proof<Q>
```

Conjunction is proof composition.

It is not runtime C++ short-circuit execution.

---

# 22. Disjunction

Disjunction introduction:

```text
Γ ⊢ p : Proof<P>
-------------------------- Or-I1
Γ ⊢ inl(p) : Proof<P || Q>
```

and symmetrically:

```text
Γ ⊢ q : Proof<Q>
-------------------------- Or-I2
Γ ⊢ inr(q) : Proof<P || Q>
```

Elimination requires both cases:

```text
Γ ⊢ d : Proof<P || Q>
Γ, p : P ⊢ e1 : Proof<R>
Γ, q : Q ⊢ e2 : Proof<R>
-------------------------------- Or-E
Γ ⊢ cases(d,e1,e2) : Proof<R>
```

Neither side may be selected without evidence.

---

# 23. Logical equivalence

Equivalence is derived:

```text
P <-> Q
```

means:

```text
(P -> Q) && (Q -> P)
```

No additional proof rule is required.

---

# 24. Universal quantification

Universal introduction:

```text
Γ, x : T ⊢ e : Proof<P(x)>
x fresh for Γ
-------------------------------- Forall-I
Γ ⊢ Λx.e : Proof<forall x : T, P(x)>
```

The proof must work for an arbitrary value of the complete formal domain `T`.

Universal elimination:

```text
Γ ⊢ f : Proof<forall x : T, P(x)>
Γ ⊢ t : T
----------------------------------- Forall-E
Γ ⊢ f[t] : Proof<P(t)>
```

Instantiation is capture-avoiding.

If `T` is a refinement type, the term must satisfy the refinement validity
required for membership in `T`.

---

# 25. Existential quantification

Existential introduction requires a witness and proof:

```text
Γ ⊢ w : T
Γ ⊢ e : Proof<P(w)>
-------------------------------- Exists-I
Γ ⊢ pack(w,e) : Proof<exists x : T, P(x)>
```

Existential elimination introduces a fresh witness locally:

```text
Γ ⊢ ex : Proof<exists x : T, P(x)>
Γ, x : T, h : P(x) ⊢ e : Proof<Q>
x not free in Q or escaping Γ
----------------------------------------- Exists-E
Γ ⊢ unpack(ex,e) : Proof<Q>
```

The hidden witness may not escape in a way that changes the meaning of the
existential claim.

The source proof language may expose only the existential forms specified by
`SPEC.md`; the formal rule exists independently of whether a dedicated proof
statement is provided.

---

# 26. Contradiction and explosion

If the formal logic derives `False`, ordinary constructive logic permits
derivation of any proposition:

```text
Γ ⊢ f : Proof<False>
---------------------- False-E
Γ ⊢ absurd(f) : Proof<P>
```

C++L need not expose `False` as a special surface keyword for the principle to
matter.

The formal core has `False` as a proposition and False-E as a primitive rule.
The goal `P` may be any well-formed proposition: an equality of integers, an
equality of structured values, a quantifier, a connective, or `False` itself.
False-E is ordinary ex falso quodlibet, not an axiom and not an assumption: it
concludes nothing unless evidence for `False` has already been checked.

`False` has no introduction rule. Evidence for it arises in exactly three ways:

```text
a hypothesis supposing it           h : False  in Γ
an elimination that yields it       e.g. Imp-E on  P -> False  and  P
linear arithmetic over facts alone  F1, ..., Fn  refuted with no goal
```

The third is the one a contradiction uses. Linear arithmetic refutes
`F1 /\ ... /\ Fn /\ not G`; for `G = False`, `not G` holds outright and states no
constraint, so the certificate must refute the facts themselves. That is what
separates showing a context cannot occur from closing a goal that merely
follows from it (`SPEC.md` `CASE-011`, `CASE-013`).

The crucial soundness condition is that contradiction itself must be derived from
valid premises.

Undefined behavior, arithmetic overflow bugs, contradictory guessed models, or
unsound trusted assumptions must not be confused with a formally derived
contradiction.

---

# 27. No universal excluded-middle axiom

The calculus does not assume:

```text
forall P : Prop, P || !P
```

as an unrestricted primitive principle.

A particular proposition may still be decidable.

For a finite machine domain, total comparison, Boolean discriminator, or another
normatively modeled operation may justify a split constructively.

The evidence must be built from the semantics of that operation, not from an
implicit global classical axiom.

---

# 28. Laws as universally quantified propositions

A Law with parameters:

```cpp
law L(T x, U y)
    expects (P(x,y))
    proves (Q(x,y));
```

denotes conceptually:

```text
forall x : T,
forall y : U,
    P(x,y) -> Q(x,y)
```

If `expects` is absent, the premise is logically `True`.

A Law declaration does not establish its own proposition.

Its proposition must be discharged by:

- checked explicit proof;
- accepted automation evidence;
- or the explicit trusted Law mechanism when the declaration itself is trusted.

A Law is therefore a named proposition/interface, not an axiom by default.

---

# 29. Proof declarations

A proof declaration denotes named evidence for its `proves` proposition.

Conceptually:

```text
proof q(...)
    proves (P)
```

creates an evidence object:

```text
q : Proof<P>
```

after its body has been checked.

Proof parameters are universally quantified according to `SPEC.md`.

Proof declarations have no runtime identity after erasure.

---

# 30. `assume` is context naming, not axiom introduction

The proof statement:

```cpp
assume h : P;
```

is valid only when the current proof context already supplies evidence for the
matching premise.

Its formal effect is:

```text
existing anonymous/context premise P
    ↓
local name h
```

It is **not**:

```text
write P
    ↓
P becomes true
```

This distinction is foundational.

---

# 31. `exact`, `apply`, `refl`, and `rewrite`

These proof statements are surface proof-program operations over the formal
rules.

## 31.1 `refl`

Requests closure by definitional equality/reflexivity.

## 31.2 `exact`

Supplies evidence expected to establish the current goal exactly, modulo the
defined conversion rules.

## 31.3 `apply`

Uses implication/universal elimination on checked evidence and creates the
remaining premises as subgoals.

## 31.4 `rewrite`

Uses propositional equality plus substitution.

A rewrite may choose a proposition context/occurrence set, but the resulting
transport must be checked by the equality rules.

A rewrite is never "text replacement is proof."

---

# 32. Formal definitions

A formal definition may participate in normalization only when its use is total
under the requirements of `SPEC.md`.

Definitions used merely as runtime implementation bodies are not automatically
admitted into definitional equality.

In particular:

```text
verified normal-return contract
```

does not automatically imply:

```text
total formal function available for normalization
```

Partial-correctness bodies cannot be unfolded as total proof computation.

---

# 33. Machine integer domains

For each modeled machine integer type `T`, the formal model records at least:

```text
width(T)
signedness(T)
value_set(T)
conversion semantics relevant to modeled operations
```

For unsigned width `w`:

```text
value_set(T) = { 0, ..., 2^w - 1 }
```

and same-type unsigned addition/subtraction/multiplication follow the C++ modular
semantics after the actual C++ conversions have selected that operation.

For signed types, the formal model must distinguish:

- representable values;
- operations whose result is defined;
- operations whose overflow is undefined.

Signed overflow is not modeled as wrapping unless the selected C++ semantics
explicitly define a different operation.

---

# 34. Machine literals

A formal representation of a machine literal must be able to denote every value
in the modeled type's value set.

In particular, a 64-bit unsigned type requires representation of:

```text
2^64 - 1
```

without accidental truncation through a signed 64-bit host representation.

Literal typing rejects values outside the destination machine domain.

Host-language numeric limits must not silently narrow the formal language.

---

# 35. Machine arithmetic normalization

For an unsigned type of width `w`, wrapping arithmetic may be represented
algebraically modulo:

```text
2^w
```

where that exactly matches the selected C++ operation.

Canonicalization must preserve:

- width;
- signedness;
- operand conversions;
- definedness;
- operation identity.

A normalization rule valid over mathematical integers is not automatically valid
over modular arithmetic.

A normalization rule valid for one machine width is not automatically valid for
another.

---

# 36. Arithmetic proof rules

Arithmetic reasoning may use:

- normalization;
- linear-arithmetic decision procedures;
- bitvector decision procedures;
- SMT;
- certificate-producing algorithms;
- checked case splits.

The mathematical requirement is:

> the accepted evidence must imply the formal arithmetic goal under the exact
> modeled machine semantics.

A producer may search however it wants.

The checker must not accept a result whose translation changes the problem.

---

# 37. Arithmetic certificates

When an arithmetic producer emits a certificate, the independent checker must
reconstruct or validate every semantic fact required by the certificate.

A certificate checker must not trust:

- producer-supplied bounds that the checker can derive itself;
- producer-supplied type widths without validating them;
- unchecked overflow in certificate arithmetic;
- omitted constraints;
- a solver's textual "unsat" as a certificate.

The concrete certificate format is architectural, not foundational.

The foundational requirement is independent validation of the claimed arithmetic
consequence whenever the solver is not itself explicitly trusted.

---

# 38. Mathematical natural numbers `@N`

`@N` is the unbounded natural-number domain:

```text
0, 1, 2, ...
```

It is proof-only.

Operations defined by `SPEC.md` use exact mathematical meaning.

Subtraction is partial in the sense specified by `SPEC.md`: using a result as
`@N` requires evidence that it remains nonnegative.

`@N` does not wrap.

---

# 39. Mathematical integers `@Z`

`@Z` is the unbounded mathematical integer domain:

```text
..., -2, -1, 0, 1, 2, ...
```

Its arithmetic is exact mathematical arithmetic.

It does not overflow.

`@Z` is not the same type as any C++ signed integer type.

---

# 40. Finite sequences `@Seq<T>`

`@Seq<T>` is a finite mathematical sequence.

The core observations defined by `SPEC.md` include:

```text
size(s) : @N
at(s, i) : T    when i < size(s)
```

Sequence equality is whatever formal equality `SPEC.md`/the mathematical model
defines; no runtime container identity is involved.

A runtime `std::vector<T>` does not become an `@Seq<T>` without an explicit
abstraction relation.

---

# 41. Finite sets `@Set<T>`

`@Set<T>` is a finite mathematical set.

The core membership proposition is:

```text
contains(set, value)
```

The formal set has no allocator, iterator invalidation, bucket layout, comparison
functor object, or runtime address.

Those are properties of runtime C++ containers, not of the mathematical domain.

---

# 42. Finite maps `@Map<K,V>`

`@Map<K,V>` is a finite mathematical map.

Core operations include:

```text
contains(map, key)
at(map, key)    when membership is established
```

The mathematical map is not a `std::map`, `std::unordered_map`, or other runtime
container.

---

# 43. Machine-to-mathematical conversion

The proof-only conversion:

```text
@Z(x)
```

for a modeled integral machine value denotes its exact mathematical represented
value.

The conversion:

```text
@N(x)
```

additionally requires evidence that the represented value is nonnegative.

There is no implicit mathematical-to-machine conversion.

Returning from an unbounded mathematical calculation to a machine value requires
proof that the concrete machine result corresponds to the mathematical result
under the relevant C++ semantics.

---

# 44. Abstract nominal values

Some C++ values are modeled through an abstract formal sort:

```text
V(signature)
```

with finite typed observations:

```text
pi_0(v) : T0
...
pi_n(v) : Tn
```

The observations are logical views supplied by a representation model.

The core does not infer private representation.

Unless the formal model says otherwise:

```text
pi_k
```

is an uninterpreted total observation function in the formal domain.

Congruence applies.

No extensional equality principle is implied merely because all currently
exposed observations happen to match.

---

# 45. Indexed observation

Some modeled values expose a homogeneous family of observations selected by an
index that is itself a formal term, rather than the fixed finite signature of
§44.

Such a value has an indexed domain:

```text
IndexedValue(T, n)
```

where `T` is the observed element type and `n` is the extent fixed at type
formation. The extent is part of type identity: `IndexedValue(T, 4)` and
`IndexedValue(T, 8)` are different formal types.

The observation is written:

```text
Element(a, i)
```

with the typing rule:

```text
Gamma |- a : IndexedValue(T, n)
Gamma |- i : I            I an integer type
-------------------------------------------
Gamma |- Element(a, i) : T
```

`Element` is an uninterpreted total observation in the formal domain, exactly as
`pi_k` is in §44. It differs only in that its position is a term, so one former
covers a family the fixed signature cannot express.

Bounds are **not** a premise of term formation.

The formal observation is total, and §46 governs the C++ fact obtained from it:
the corresponding C++ subscript is usable only under the premise that it is
defined, which for an array element is

```text
i < n
```

This keeps the core term total and decidably typed while leaving the definedness
boundary exactly where every other partial C++ observer leaves it. In
particular:

```text
Element(a, i)
```

does **not** establish:

```text
i < n
```

No rule derives one from the other in either direction.

Congruence applies:

```text
a == b, i == j   |-   Element(a, i) == Element(b, j)
```

The converse principles are not admitted. Indexed observation implies neither
injectivity:

```text
Element(a,i) == Element(a,j)   =/=>   i == j
```

nor extensionality:

```text
(forall i. Element(a,i) == Element(b,i))   =/=>   a == b
```

An extensional principle for an indexed domain, if ever wanted, must be
introduced explicitly by the formal model and may not be inferred from the
existence of the observation.

`Element` binds nothing, admits no reduction rule, and is not a recursor, so it
affects neither normalization nor termination.

---

# 46. Partial observations

A C++ observer may only be defined in some runtime states.

The formal model may represent its observation as total internally for typing, but
the C++ fact obtained from that observation is usable only under the premise that
the runtime observer is defined.

This avoids introducing a partial-function crash into the core while still
preserving the runtime definedness boundary.

---

# 47. Program states

For program reasoning, let `Σ` denote an abstract verified state.

It records enough semantic information to interpret:

- live Regions;
- current PlaceVersions;
- known values;
- capabilities;
- path conditions;
- trusted-assumption provenance;
- call/exceptional state where relevant.

`Σ` is a proof model.

It is not a runtime object inserted into the executable.

---

# 48. Places

A Place is a proof-level designation of C++ storage.

Conceptually:

```text
Place = root + projection path
```

A root may designate storage such as:

- a local object;
- a parameter referent;
- a temporary;
- a dynamic object;
- a pointer-selected pointee where access is justified.

Projection paths may identify:

- data members;
- base subobjects when modeled;
- array elements;
- nested subobjects.

A Place is not a formal runtime value.

Reading a Place yields a value.

Writing a Place creates a new logical version.

---

# 49. Regions

A Region represents the live C++ object/allocation context to which storage
belongs.

Region facts may include:

- lifetime;
- extent;
- provenance relationship;
- storage identity relevant to alias reasoning.

Region semantics must follow C++.

A Region does not imply ownership in a language-specific sense unless the modeled
C++ abstraction supplies such a property.

---

# 50. Logical value versions

For a Place `l`, verification reasons about versions:

```text
l@0
l@1
l@2
...
```

Conceptually, each successful write establishes a new current logical value.

A fact about:

```text
l@0
```

does not automatically hold for:

```text
l@1
```

This is proof bookkeeping only.

No runtime copy or version object is required.

---

# 51. Read judgment

A read is permitted only when the selected C++ operation is defined and the
current state establishes the necessary access facts.

Conceptually:

```text
Σ ⊢ readable(l)
current(Σ,l) = v
-----------------------
Σ ⊢ read(l) ⇓ v
```

For pointer dereference, the capability must arise from the pointer/Region
relationship required by `SPEC.md`.

Non-nullness alone is insufficient.

---

# 52. Write judgment

A write to Place `l` with value `v` conceptually requires:

```text
l is live and writable
the C++ assignment/construction is defined
the target semantic type accepts v
```

Then:

```text
write(Σ,l,v) = Σ'
```

where:

- `l` receives a new PlaceVersion;
- facts about prior versions of `l` do not constrain the new value unless
  separately related;
- every place that may alias `l` loses facts that the write may invalidate;
- Region/lifetime changes are applied;
- refinement validity for the new target value is established before the target
  is treated as valid refined storage.

---

# 53. Capabilities

Capabilities are state-sensitive judgments.

Representative semantic facts include:

```text
Live(r)
Initialized(l)
Readable(p,n)
Writable(p,n)
```

The exact source propositions are defined by `SPEC.md`.

The formal foundation does not require these atoms to be represented by the same
kernel proposition datatype as equality/arithmetic propositions.

What is required is sound composition.

If a source clause contains both ordinary logical content and capability
content, the verifier may decompose the specification into obligations handled by
different checkers, provided the conjunction/implication meaning of the source
proposition is preserved exactly.

A capability must never be inserted merely because an operation needs it.

---

# 54. Readability

The source proposition:

```text
readable(p,n)
```

means the selected C++ access to the relevant `n` objects beginning at `p` is
valid for reading under the conditions defined by `SPEC.md`.

That includes the required state facts such as:

- live object(s);
- initialization;
- bounds/extent;
- provenance;
- alignment;
- access permission.

This is stronger than:

```text
p != nullptr
```

---

# 55. Writability

The source proposition:

```text
writable(p,n)
```

means the corresponding modeled write access is valid under the C++ object model
conditions defined by `SPEC.md`.

It does not assert the old stored values.

A successful write establishes facts about the new value according to the write
operation and semantic target type.

---

# 56. Aliasing

The safe frame principle is:

```text
a fact about place a survives a write to place b
only when the verifier establishes that the write cannot affect a
```

Equivalently:

```text
may alias
    ⇒
invalidate affected facts
```

rather than:

```text
not yet proven to alias
    ⇒
assume disjoint
```

Disjointness must come from modeled C++ semantics or checked evidence.

---

# 57. The frame rule

In classical Hoare-style notation:

```text
{P} C {Q}
```

may be framed with an independent assertion `R` only when the command cannot
invalidate the state on which `R` depends.

C++L's conservative Place/Region/effect discipline is a concrete way to satisfy
that requirement for imperative C++.

The formal foundation does not assume unrestricted separation of differently
named fields or pointers.

---

# 58. Call effects

A function summary separates:

```text
logical precondition
logical normal-return postcondition
storage/effect summary
termination status
trust dependencies
```

At a verified call:

1. the caller establishes the entry obligations;
2. the modeled call effect transforms the abstract state;
3. invalidated PlaceVersion facts are dropped;
4. accepted postconditions become available in the resulting state;
5. trust dependencies are propagated.

An unverified call cannot contribute arbitrary postconditions.

---

# 59. Hoare-style partial correctness

A partial-correctness judgment is conceptually:

```text
Γ; Ω ⊢ {P} C {Q}_normal
```

meaning:

> for every modeled execution of `C` beginning in a state satisfying `P` and the
> required state obligations, if `C` returns normally, the resulting normal state
> satisfies `Q`.

This does not imply termination.

It does not imply absence of exceptions unless those are separately established.

---

# 60. Function contracts

A verified function:

```cpp
verified R f(A a)
    expects (P)
    ensures (Q)
{
    C
}
```

has a proof obligation corresponding conceptually to:

```text
forall a : A,
    Valid(A,a) ->
    P ->
    NormalExecution(C,a,result,state') ->
    Q
```

where the exact state/call semantics are those defined by `SPEC.md`.

This notation is explanatory.

A compiler need not encode a `NormalExecution` predicate literally.

The key points are:

- parameter semantic validity is an entry premise;
- `expects` is a premise, not an assumed global theorem;
- `ensures` concerns normal return;
- runtime definedness and storage obligations are part of verifying `C`.

---

# 61. Normal-return result

For a non-void function, `result` in the postcondition denotes the value returned
on the normal-return path.

It is a specification binder.

It is not a hidden runtime variable inserted into the executable.

Each return path must establish the postcondition with its own returned value and
resulting normal post-state.

---

# 62. `old`

Within the source context permitted by `SPEC.md`, `old(e)` denotes the semantic
value of `e` in the function entry state.

Conceptually:

```text
old(e) = eval(e, Σ_entry)
```

It is not:

```text
eval(e, Σ_current)
```

with a historical label.

`old` creates a formal snapshot dependency.

It does not require a runtime copy when the verifier can represent the entry
value symbolically.

---

# 63. Total correctness

Total correctness extends partial correctness with termination:

```text
{P} C {Q}_total
```

means:

```text
C terminates for every covered execution satisfying P
and
on normal return Q holds
```

where exception behavior remains subject to the exact claim defined by `SPEC.md`.

A normal-return postcondition alone is partial correctness.

Totality must be requested or required according to `SPEC.md`.

---

# 64. Weakest-precondition view

For a command `C` and desired postcondition `Q`, a weakest-precondition semantics
may be viewed conceptually as:

```text
wp(C,Q)
```

where:

```text
Σ ⊨ wp(C,Q)
```

means every relevant normal execution of `C` from `Σ` establishes `Q`.

C++L does not require one particular implementation algorithm such as classical
backward WP construction.

Symbolic execution, SSA-style conditions, path-sensitive obligation generation,
or another method may be used if it creates equivalent obligations.

---

# 65. Assignment rule

For a pure logical assignment model:

```text
x := e
```

the classical substitution idea is:

```text
wp(x := e, Q) = Q[e/x]
```

C++ requires more obligations.

The actual C++L rule additionally accounts for:

- expression definedness;
- conversions;
- lifetime;
- aliasing;
- target Place;
- new PlaceVersion;
- refinement validity;
- effects.

Thus the substitution law is a mathematical core, not a complete C++ memory
model.

---

# 66. Branches

For runtime condition `c`, verification splits paths according to the semantics
of C++ evaluation.

Conceptually:

```text
then path: Bool(c)
else path: !Bool(c)
```

provided `c` itself is defined and its short-circuit operands are modeled in the
actual C++ evaluation order.

Facts from one path do not become facts on the other.

At a join, only facts established for every reaching path remain usable unless a
sound merge representation preserves path dependence explicitly.

---

# 67. Runtime short-circuit versus logical connectives

Runtime C++:

```cpp
a && b
```

evaluates `b` only when `a` is true.

Formal conjunction:

```text
P && Q
```

requires both propositions to be well-formed and both pieces of evidence.

These are different semantics even though they share familiar glyphs in their
respective grammatical contexts.

The same distinction applies to `||`.

---

# 68. Loops and invariants

A loop invariant `I` has three fundamental obligations.

## 67.1 Entry

Before the first loop body/head state required by `SPEC.md`:

```text
I
```

must hold.

## 67.2 Preservation

Every path that continues to another iteration must establish `I` for the next
loop-head state.

## 67.3 Exit

On a normal condition-controlled exit, code after the loop may use:

```text
I
```

plus the false condition at the point defined by the C++ loop semantics.

`break`, `continue`, `return`, and `throw` follow their own path semantics.

---

# 69. Loop-carried state

Every Place that may be modified by:

- loop condition;
- body;
- iteration expression;
- aliases;
- calls;

is loop-carried.

At a generic head, a pre-loop value of carried state cannot be reused unless the
invariant or another sound relation establishes it.

This is a fixed-point principle over program states.

---

# 70. Termination measures

A `decreases` tuple:

```text
(M1, ..., Mk)
```

is ordered lexicographically over well-founded component relations.

For every recursive edge or continuing loop edge, the verifier must establish:

```text
(M1', ..., Mk') <lex (M1, ..., Mk)
```

and definedness of all measure expressions.

No measure may rely on a relation merely named `<` unless its well-foundedness is
part of the formal environment.

---

# 71. Well-founded relations

A relation `<_W` on domain `W` is well-founded when there is no infinite
descending chain:

```text
w0 >_W w1 >_W w2 >_W ...
```

Equivalently for proof purposes, well-founded induction is admissible.

Core examples specified by C++L include:

- `<` on `@N`;
- the finite natural order on suitable unsigned machine domains for the
  non-wrapping induction/termination principles defined by `SPEC.md`;
- lexicographic products of well-founded relations.

Custom well-founded orders require formal justification.

---

# 72. Recursion

A recursive proof-relevant function is admissible for normalization only when its
recursive calls are proven to descend under an accepted well-founded measure.

Mutual recursion requires a common ranking sufficient for every edge in the
strongly connected component.

A recursive function that has only a partial normal-return contract is not
automatically a total formal definition.

---

# 73. Induction over `@N`

The induction principle is:

```text
P(0)

forall n : @N,
    P(n) -> P(n + 1)

--------------------------------
forall n : @N,
    P(n)
```

The induction hypothesis is evidence supplied by the principle.

It is not obtained by recursively calling the proof declaration.

---

# 74. Induction over unsigned machine integers

For unsigned machine type `T` with maximum `max(T)`:

```text
P(0)

forall n : T,
    n < max(T) ->
    P(n) ->
    P(n + 1)

--------------------------------
forall n : T,
    P(n)
```

The range premise is essential.

Without it, the successor step would wrap at the maximum value and would not be
the intended well-founded successor relation.

---

# 75. No generic pointer induction

A raw pointer does not imply:

- acyclicity;
- finite reachability;
- ownership;
- unique predecessor;
- well-founded recursive structure.

Therefore pointer shape alone does not supply a generic structural induction
principle.

Any stronger recursive structure requires an explicitly modeled well-founded
domain/abstraction allowed by `SPEC.md`.

---

# 76. Structural case analysis

A case split is valid only relative to a complete formal partition of the
modeled value domain.

Let a subject value be `v` and let discriminators be:

```text
D1(v), ..., Dn(v)
```

A sound case partition must ensure that every represented runtime state maps to
one of:

```text
D1
...
Dn
residual
```

where the residual denotes the states in which all listed discriminators are
false.

The correspondence between C++ representation and this partition is not proved
merely by the logical case rule; it is a correspondence obligation described by
`TRUST.md`.

---

# 77. Derived case evidence

Case analysis need not introduce a representation-specific kernel rule.

For a discriminator `D`, the proof may be structured using the existing logical
rules:

```text
D -> goal
!D -> goal
----------------
goal
```

when the formal semantics provide a checked decidable split for `D`.

Repeated splitting yields one branch per explicit discriminator plus a residual
branch.

The kernel checks each branch according to ordinary evidence rules.

---

# 78. Scoped enumeration partition

A scoped enum's logical state domain is the complete value set permitted by its
fixed underlying integer type.

Named enumerators identify particular values.

The residual:

```text
unnamed(value)
```

covers every value equal to no named enumerator.

Two enumerators with the same underlying value denote one logical value case, not
two distinct runtime states.

---

# 79. Variant partition

A modeled `std::variant<Ts...>` has logical cases:

```text
alternative<0>
...
alternative<n-1>
valueless
```

Alternative identity is by index.

Repeated alternative types remain distinct states.

`valueless` is not omitted unless independently proved impossible.

---

# 80. Optional partition

A modeled `std::optional<T>` has:

```text
some(value)
none
```

The payload observation is available only in the engaged case.

No runtime dereference call is inserted merely for proof decomposition.

---

# 81. Expected partition

A modeled `std::expected<T,E>` has:

```text
value(payload)
error(reason)
```

subject to availability in the selected C++ library mode.

The formal partition models public semantic states, not private layout.

---

# 82. Pointer partition

A pointer case split provides only:

```text
null
non_null
```

The `non_null` arm binds no pointee value and establishes no capability.

It does not prove:

```text
liveness
provenance
bounds
initialization
readability
writability
ownership
uniqueness
```

---

# 83. Product decomposition

A product decomposition exposes modeled components of one existing logical value.

Conceptually:

```text
v
    ↓
pi_0(v), ..., pi_n(v)
```

This is not a sum/case split.

It generates no alternative-state assumption.

Bindings denote observations of existing subobjects/values; they do not create
runtime copies.

---

# 84. Refinement types

A refinement:

```text
R = { x : T | P(x) }
```

has:

```text
runtime representation: T
verification identity:   R
membership condition:    Valid(R,x)
```

Refinement introduction is proof obligation construction, not runtime
construction.

---

# 85. Semantic validity

The conceptual predicate:

```text
Valid(T,v)
```

means that `v` satisfies the verification-level validity requirements of type
`T`.

For an ordinary modeled type with no refinement-bearing substructure:

```text
Valid(T,v)
```

adds no refinement predicate.

For:

```text
R = { x : T | P(x) }
```

validity is:

```text
Valid(R,v)
    iff
Valid(T,v) && P(v)
```

This formulation is recursive.

---

# 86. Recursive object validity

For an object value `o` of object type `C`, semantic validity includes validity of
the live refinement-bearing subobjects required by the normative type semantics.

Conceptually:

```text
Valid(C,o)
    ⇒
Valid(T_i, projection_i(o))
```

for each relevant refinement-bearing member/base/element `i`.

For arrays, validity applies elementwise.

For unions, only the active member contributes.

For references, validity concerns the referred current logical value/version.

For pointers, validity of the pointer value does **not** recursively imply
validity of the pointee.

Pointer access remains governed by lifetime/capability rules.

---

# 87. Refinement introduction

To introduce `v` into refinement `R`:

```text
Γ ⊢ v : T
Γ ⊢ Valid(T,v)
Γ ⊢ P(v)
-------------------------------
Γ ⊢ v : R
```

where the exact representation of `Valid(T,v)` may be expanded recursively.

No hidden runtime validation is implied.

---

# 88. Refinement elimination

A valid refined value may be used as its base value:

```text
Γ ⊢ v : R
R refines T
----------------
Γ ⊢ erase_type(v) : T
```

No additional proof is required merely to forget refinement information.

The runtime value is the same representation.

---

# 89. Refinement-to-refinement crossing

For:

```text
R = {x:T | P(x)}
S = {x:T | Q(x)}
```

a value known to inhabit `R` may cross to `S` when the current context establishes
the required `Q(v)`.

A reusable theorem:

```text
forall x : T, P(x) -> Q(x)
```

is sufficient but not required if a stronger path-specific fact already proves
`Q(v)`.

---

# 90. Versioned refinement validity

If:

```text
Valid(R, l@0)
```

holds and a write establishes:

```text
l@1
```

then validity of `l@1` must be established independently.

There is no rule:

```text
Valid(R,l@0)
----------------
Valid(R,l@1)
```

merely because the storage identity is unchanged.

---

# 91. Verified-parameter validity

At a verified boundary, a parameter of semantic type `T` supplies:

```text
Valid(T,parameter)
```

as an entry premise of that verified claim.

For aggregate/object types this premise is recursive as described above.

This is a semantic precondition, not a runtime ABI check.

An unverified caller may physically pass a representation that violates it; in
that execution, the verified theorem's premise is not satisfied.

---

# 92. Mutation and validity

A write that creates a new logical value must establish the validity required by
the target semantic type.

A may-alias write invalidates validity facts for affected current observations.

A checked postcondition may re-establish validity for the post-state.

Historical construction provenance is not itself required when the current
logical value is already established valid.

---

# 93. Indexed refinements

A parameterized refinement may be modeled as a family:

```text
R : I -> Type
```

where an application:

```text
R<i>
```

substitutes `i` into the refinement predicate.

Distinct index arguments may produce distinct verification-level types even when
all applications erase to the same runtime base type.

Type equality of indexed refinements therefore depends on:

- refinement declaration identity;
- formal equality/identity of required index arguments;
- capture-safe substitution.

Native overload identity still follows the erased C++ type rules in `SPEC.md`.

---

# 94. Dependent types

A dependent formal type is a type whose verification meaning depends on a value.

Conceptually:

```text
T(x)
```

The foundational requirement is that dependencies are:

- well-scoped;
- type-correct;
- substitution-safe;
- stable under the source rules governing their indices.

C++L does not thereby make arbitrary runtime values part of native C++ type
identity.

The runtime/verification distinction remains explicit.

---

# 95. Runtime validation

Runtime validation is ordinary C++ execution that establishes a path fact.

For:

```cpp
if (raw >= 0 && raw <= 100) {
    Percentage p = raw;
}
```

the proof model uses the successful branch premises to establish the refinement
crossing.

The runtime check remains runtime code.

The proof interpretation of the path fact erases.

There is no foundational rule:

```text
untrusted runtime value
    magically becomes refined
```

without a checked runtime branch or another established premise.

---

# 96. Ghost state

Ghost locals are proof-only state.

Their semantics may be represented in the formal state/context but they do not
exist in runtime state after erasure.

For erasure soundness:

- runtime control flow cannot depend on ghost;
- runtime return values cannot depend on ghost;
- ghost initialization/destruction cannot have observable runtime effects;
- ghost addresses cannot escape into runtime code.

The grammar restriction to ghost locals is defined by `SPEC.md`.

---

# 97. Unsafe boundaries

`unsafe` marks runtime behavior for which the strongest verified guarantee is not
claimed.

Formally:

```text
unsafe execution
```

does not introduce a hypothesis into `Γ`.

It does not add an element to trusted assumptions `Δ`.

It does not prove capability facts in `Ω`.

Any fact needed after the boundary must come from:

- ordinary C++ semantics;
- runtime validation;
- checked proof;
- explicit trusted Law.

---

# 98. Trusted Laws

A `trusted law` explicitly admits its proposition relative to its declared
premises.

Conceptually, if source declares trusted Law `L : P`, the formal environment may
use:

```text
trust_L : Proof<P>
```

but the evidence is tagged with trusted-assumption identity:

```text
trust dependency = {L}
```

Any theorem derived from it carries that dependency transitively.

This is not the same as kernel-derived proof.

The realization adds no rule and no constant `trust_L`. A derivation of `G` that
uses trusted Laws `L1 : P1, ..., Ln : Pn` is presented to the checker as
evidence for

```text
P1 -> ... -> Pn -> G
```

built by implication introduction, and each use of `trust_Li` is the hypothesis
that introduction supplies. The accepted proposition is then literally the
`Deps(e) ⊨ G` of §130, with `Deps(e)` as its premises: the checker has verified
the derivation relative to exactly those, and evidence that used any other
assumption would name a hypothesis not in scope and be refused. Reusing such a
theorem inside another derivation eliminates each of its premises with that
derivation's own hypothesis for the same Law, so the dependency is carried by
evidence the checker sees rather than by bookkeeping beside it.

The source restriction that `trusted` is only the production trusted-Law surface
is defined by `SPEC.md`.

---

# 99. Trusted memory propositions

A trusted Law may admit a specification proposition such as:

```text
readable(p,n)
```

or:

```text
writable(p,n)
```

The implementation may route such an admitted proposition into the
storage/capability environment `Ω`.

The formal meaning remains:

```text
explicit assumption with trust provenance
```

not:

```text
capability inferred from demand
```

and not:

```text
runtime memory check
```

---

# 100. Exceptions

Normal postconditions concern normal return.

A throwing path has its own state transition:

```text
Σ
    ↓ throw/unwind
Σ_exceptional
```

including destructor and lifetime effects required by C++.

There is no foundational rule that applies normal `ensures` to a propagated
exceptional exit.

If a future language version introduces exceptional contracts, their formal
meaning must be added normatively before the calculus assumes them.

---

# 101. `noexcept`

C++ `noexcept` is runtime C++ semantics.

An escaping exception from a `noexcept` function causes the C++ behavior defined
by the selected language mode.

A proof that a function does not throw is distinct from the fact that its
declaration is `noexcept`.

The formal model must not conflate:

```text
cannot throw
```

with:

```text
throwing terminates the program
```

---

# 102. Construction and destruction

Construction and destruction affect:

- lifetime;
- initialization;
- storage capabilities;
- refinement validity;
- effects;
- exception paths.

A fully initialized object value used in verified reasoning must satisfy the
semantic validity required by its type.

During partial construction, only subobjects whose C++ lifetime/initialization has
actually begun may be treated as established.

Destruction ends lifetime according to C++ order.

Proof erasure cannot reorder these operations.

---

# 103. Moves

A move is modeled according to the actual C++ operation selected.

There is no general theorem:

```text
move(x) leaves x unchanged
```

or:

```text
move(x) destroys x
```

The moved-from state is whatever the checked C++ type contract/model establishes.

Refinement facts survive only when justified for the resulting logical versions.

---

# 104. Virtual dispatch

For a virtual call verified against a base contract, the formal call rule relies
on that base interface.

Override verification establishes substitutability according to `SPEC.md`.

Conceptually, if:

```text
P_base, Q_base
P_over, Q_over
```

are the relevant contracts, the override must satisfy the direction required by
behavioral subtyping:

```text
P_base -> P_over
Q_over -> Q_base
```

under the corresponding state/result bindings and effects.

A caller does not gain facts merely from guessing the dynamic override.

---

# 105. Templates

Template semantics remain C++ semantics.

A formal theorem associated with a template may be:

- proved generically under template parameters and constraints, when a valid
  generic derivation exists;
- or proved for concrete specializations.

There is no principle:

```text
one successful specialization
    ⇒
all specializations proven
```

Template constraints are not automatically theorem evidence.

---

# 106. Lambdas

A lambda is a C++ object with capture semantics determined by C++.

For verification:

- by-value capture introduces closure storage containing a value/copy/move as C++
  defines;
- by-reference capture aliases existing storage;
- mutable lambdas may write captured-by-value closure members;
- calls through captures participate in ordinary effects/aliasing.

There is no lambda-specific escape from the Place/version model.

---

# 107. Concurrency

Sequential reasoning is sound only when concurrent interference is excluded or
accounted for.

A shared mutable fact may remain stable only under a proven synchronization or
other C++ memory-model relation sufficient for the claim.

The foundations do not silently impose sequential consistency on all atomics or
data-race-free behavior on ordinary memory.

Concurrency claims require the C++ concurrency semantics defined by the supported
model.

---

# 108. Data races and undefined behavior

A reachable C++ data race that is undefined under the selected C++ model cannot
be treated as a nondeterministic but otherwise valid execution for a fully
verified path.

Definedness is a prerequisite for the execution semantics being proved.

This is one instance of the general UB rule.

---

# 109. Undefined behavior

For a verified runtime operation `op`, the proof system requires the
defined-behavior preconditions applicable to that operation.

Conceptually:

```text
Defined(op, Σ)
```

must hold before the operation's ordinary result/effect semantics are used.

The exact obligations are defined by `SPEC.md`.

If definedness cannot be established, the stronger verified claim fails closed.

---

# 110. Floating point

Floating-point values are modeled according to the selected C++/target semantics
required by the claim.

They are not identified with mathematical reals.

A mathematical-real abstraction, if introduced in a future normative model, would
need an explicit relation to floating-point execution.

No such relation may be assumed merely from similar notation.

---

# 111. Formal state partitions and correspondence

A representation provider may describe a C++ value through a formal partition and
observations.

The formal proof rules assume the supplied partition has the meaning claimed by
the model.

Whether that partition correctly describes C++ is a correspondence question.

Therefore:

```text
kernel proof soundness
```

and:

```text
representation-model correspondence
```

are distinct obligations.

`TRUST.md` owns the second trust boundary.

---

# 112. Standard-library models

A formal model for a standard-library type defines only the abstract behavior
claimed by that model.

It does not automatically prove that every implementation of the standard
library satisfies an implementation-specific hidden representation.

Models should therefore prefer public semantic guarantees and canonical type
identity.

Assumed correspondence to external implementations is explicit trust/runtime
correspondence, not a logical theorem arising from the model's existence.

---

# 113. Foreign code

Foreign code has no special proof privilege.

A foreign operation contributes formal facts only through:

- checked wrapper semantics;
- explicit trusted Law;
- runtime validation;
- independently proved correspondence.

Its ABI return value is not proof evidence merely because a declaration has a
refined or suggestive type spelling.

---

# 114. Cross-translation-unit proof composition

A separately compiled verified interface must preserve the semantic information
needed to reconstruct the same theorem at the caller.

Conceptually, a checked summary contains enough identity for:

```text
entity
contract
effect summary
refinement identities
purity/totality facts
trust closure
proof/evidence identity
semantic environment
```

The exact serialization is architectural.

Mathematically, using a summary is theorem application.

It is sound only if the imported summary denotes the same formal entity and
evidence that was checked in its defining environment.

---

# 115. Erasure

Let:

```text
erase : C++L -> C++
```

be the language-defined removal/lowering of proof-only constructs and
runtime-transparent formal declarations.

The central semantic goal is:

```text
Sem_runtime(p)
=
Sem_runtime(erase(p))
```

for every well-formed C++L program `p` within the selected semantics.

This is an end-to-end correspondence property.

The logical calculus alone does not prove the implementation's erasure correct.

---

# 116. Erasure of proof evidence

Proof declarations, proof statements, Law proof machinery, quantifier binders,
case proofs, induction proofs, and other proof-only evidence have no runtime
effect.

Erasure removes them.

A proof may affect:

```text
whether compilation succeeds
```

but not:

```text
which runtime branch executes
```

unless the branch already exists as ordinary runtime C++.

---

# 117. Refinement erasure

A refinement erases to its ultimate ordinary C++ base representation according to
`SPEC.md`.

Therefore:

```text
verification identity
```

and:

```text
runtime type identity
```

are deliberately different notions.

This is why two refinement-only overloads cannot become distinct native overloads
without an explicit ABI-changing language feature.

---

# 118. Ghost erasure

Ghost locals erase completely.

For erasure to be semantics-preserving, their proof-side initialization and
destruction semantics must be observationally irrelevant to runtime.

A ghost construct that would require observable runtime effects is invalid rather
than executed and then erased.

---

# 119. Runtime validation and erasure

A runtime `if`, comparison, parser, length check, bounds check, or other ordinary
C++ validation remains runtime code.

Only the formal interpretation of the successful branch is proof-only.

Thus erasure removes no runtime validation that the programmer actually wrote.

---

# 120. Trusted assumptions and erasure

A trusted Law erases from runtime just as an ordinary proof declaration does.

Its **trust provenance does not disappear from the assurance result** merely
because the source declaration has no runtime representation.

Erasure concerns runtime execution.

Trust reporting concerns the proof claim.

They are orthogonal.

---

# 121. Soundness theorem schema

The desired logical soundness property has the form:

```text
if Γ ⊢ e : Proof<P>
then P is valid in every model satisfying Γ
```

relative to:

- the formal calculus;
- explicit trusted assumptions;
- the correctness of primitive formal semantics.

For C++-connected propositions, this theorem must be composed with
source/runtime correspondence assumptions defined in `TRUST.md`.

---

# 122. Type preservation

A foundational meta-property is:

> well-typed formal reduction does not change the type of a term.

Conceptually:

```text
Γ ⊢ t : T
t -> t'
----------------
Γ ⊢ t' : T
```

for every reduction admitted by definitional equality.

This property is required for trustworthy normalization.

---

# 123. Substitution lemma

A core meta-property is:

```text
Γ, x : T ⊢ u : U
Γ ⊢ t : T
-------------------------
Γ ⊢ u[t/x] : U[t/x]
```

with the corresponding proposition/evidence forms.

Capture-avoiding substitution must preserve well-formedness.

---

# 124. Progress is not a universal runtime theorem

Traditional type-safety metatheory often states progress.

C++L cannot simply claim a global "well-typed C++L program never gets stuck"
property because ordinary C++ includes:

- partial runtime operations;
- foreign code;
- unsafe regions;
- unverified code;
- environment interactions.

Instead, verified fragments establish definedness obligations for the operations
covered by the verification claim.

The relevant theorem is local assurance, not a claim that all C++L programs are
memory-safe by syntax alone.

---

# 125. Consistency objective

The proof calculus must not admit evidence for contradiction from an empty,
consistent formal environment.

Conceptually:

```text
not (∅ ⊢ e : Proof<False>)
```

subject to the meta-theory and primitive semantics being sound.

Explicit trusted assumptions may themselves be inconsistent.

If they are, consequences are valid only relative to that inconsistent assumption
set and trust reporting must expose the dependency.

---

# 126. Normalization objective

Proof-relevant definitional reduction should be strongly normalizing for the
fragment admitted into normalization.

Runtime C++ as a whole is not required to be strongly normalizing.

This is why partial runtime functions cannot automatically become formal
definitions.

---

# 127. Determinism objective

Given identical formal inputs and semantic configuration, core checking should
produce the same validity result.

Automation search may be nondeterministic.

The evidence that is accepted must validate deterministically under the same core
semantics.

---

# 128. Decidability boundary

Not every proposition C++L can state must have an automatically decidable proof
search procedure.

The language may be expressive while proof search remains incomplete.

The important separation is:

```text
proof search may fail
```

without implying:

```text
proposition is false
```

and without allowing:

```text
proof search failed
    ⇒
assume proposition
```

---

# 129. Automation completeness is not logical completeness

An arithmetic solver, simplifier, or tactic may support only a subset of the
logic.

Its inability to find evidence says nothing by itself about whether evidence
exists.

This permits automation to evolve without changing theorem meaning.

---

# 130. Trust-relative theorem validity

Let:

```text
Deps(e)
```

be the transitive set of explicit trusted assumptions used by evidence `e`.

Then an accepted theorem is more precisely interpreted as:

```text
Deps(e) ⊨ P
```

with kernel evidence showing derivation of `P` relative to those premises.

Tooling may still classify the resulting theorem according to the status model in
`SPEC.md`/`TRUST.md`, but the assumption closure is never mathematically erased.

---

# 131. Runtime correspondence theorem schema

For a verified source claim about executable behavior, the end-to-end statement
has the shape:

```text
source correspondence
+
formal proof
+
erasure/runtime correspondence
--------------------------------
runtime guarantee
```

No one term in that sum can replace the others.

A perfect proof kernel does not prove source correspondence.

A perfect source model does not prove the theorem without evidence.

A perfect theorem about source does not guarantee a different emitted program.

---

# 132. Refinement soundness schema

For a refined semantic type `R`, soundness requires:

```text
every verified introduction/crossing into R establishes Valid(R,v)
```

and:

```text
every use of refinement facts refers to a current logical value/version whose
validity remains established
```

plus:

```text
erasure preserves the runtime base representation
```

These three pieces correspond to:

- obligation completeness;
- mutation/alias validity;
- runtime representation preservation.

---

# 133. Storage soundness schema

For a verified memory access, soundness requires:

```text
correct Place/Region correspondence
+
required capability/lifetime facts
+
bounds/definedness proof
+
sound alias/effect invalidation
```

The proof kernel need not contain a primitive memory logic if those obligations
are checked soundly outside it.

But whatever checker grants those facts belongs to the appropriate TCB layer.

---

# 134. Loop soundness schema

For a partial-correctness loop proof, soundness requires:

```text
invariant entry
+
invariant preservation on every continuing edge
+
sound carried-state/frame model
+
correct exit conditions
```

For total correctness, add:

```text
well-founded measure
+
strict descent on every continuing edge
```

A missed back edge, aliasing write, `continue`, exceptional edge, or call effect is
a correspondence error, not a permissible simplification.

---

# 135. Case-analysis soundness schema

For structural case proof:

```text
correct complete C++ state partition
+
checked proof of goal in each reachable case
```

implies the enclosing goal.

The proof engine may derive the case logic from ordinary logical rules.

The partition correspondence remains separately trust-sensitive.

---

# 136. Induction soundness schema

For an induction principle over well-founded domain `W`:

```text
for every x,
    (for every y < x, P(y)) -> P(x)
-----------------------------------
for every x, P(x)
```

is sound when `<` is well-founded.

C++L exposes specialized induction principles defined by `SPEC.md`.

The system does not infer well-foundedness from arbitrary recursive C++ shape.

---

# 137. Relationship to TRUST.md

This document defines what the formal rules are intended to mean.

`TRUST.md` answers:

```text
which software components must be correct for those rules and their C++
correspondence to be trustworthy?
```

The distinction matters.

For example:

- equality substitution is a formal rule;
- the checker implementing substitution is logical TCB;
- the frontend choosing the C++ terms that become the equality operands is
  correspondence TCB.

Foundational soundness and implementation trust are complementary.

---

# 138. Relationship to ARCHITECTURE.md

This document does not require a particular compiler pipeline.

A conforming architecture may use:

- SSA;
- symbolic execution;
- weakest preconditions;
- explicit path trees;
- certificate-producing solvers;
- different internal term representations.

It must preserve the judgments and source semantics defined here and in
`SPEC.md`.

`ARCHITECTURE.md` owns how that preservation is engineered.

---

# 139. Relationship to DESIGN.md

`DESIGN.md` explains why C++L chose:

- C++ runtime fidelity;
- proof erasure;
- refinements without wrappers;
- explicit trust;
- proof/check separation;
- structural reasoning over C++ types;
- mathematical domains distinct from machine types.

This document supplies the mathematical form of those choices.

It does not repeat their product rationale.

---

# 140. Relationship to STATUS.md

Implementation coverage is not part of the mathematical foundation.

A missing checker, unsupported syntax path, or temporary blanket rejection does
not change the formal rules.

`STATUS.md` records what exists.

`FOUNDATIONS.md` records the target formal model.

---

# 141. Formal non-goals

This foundation intentionally does not claim that:

- all valid C++ is automatically verified;
- all C++ is memory-safe;
- every theorem is decidable automatically;
- all C++ libraries are formally verified;
- Clang is part of the logical calculus;
- trusted assumptions are proven;
- unsafe code is correct;
- runtime validation is static theorem proving;
- machine integers are mathematical integers;
- proof erasure proves the native compiler correct;
- a small kernel alone establishes source-level soundness;
- AI reasoning is formal evidence.

---

# 142. Foundational acceptance criteria

The formal foundation is adequate for C++L only if it supports all of the
following without semantic contradiction:

```text
ordinary C++ runtime values remain ordinary C++ values

formal propositions are distinct from runtime bool

checked proof and explicit trust remain distinct

universal and existential quantification are capture-safe

implication, conjunction and disjunction compose evidence soundly

formal equality supports checked substitution

machine arithmetic preserves width/signedness/definedness

mathematical domains are explicit and proof-only

verified functions support path-sensitive Hoare-style reasoning

PlaceVersions prevent stale mutation facts

capabilities do not arise from non-nullness or demand

refinement validity is versioned and recursive through required subobjects

casts do not manufacture refinement

runtime validation remains runtime execution

case analysis covers the complete modeled C++ state space

induction requires a specified well-founded principle

proof-relevant computation terminates

normal-return contracts remain distinct from total correctness

unsafe does not create assumptions

trusted Laws remain explicit assumptions with transitive provenance

proof/ghost data erase without runtime influence

cross-TU theorem reuse preserves semantic identity and dependencies
```

---

# 143. Fundamental formal rule

The core principle can be stated as:

```text
C++L may conclude no stronger proposition than is justified by

    checked formal evidence
    +
    explicit premises
    +
    explicit trusted assumptions
    +
    sound correspondence to the C++ semantics being modeled.
```

Equivalently:

```text
no evidence
and no explicit trust
    ⇒
no theorem
```

and:

```text
proof of the wrong model
    ≠
proof of the program
```

---

# Annex A — Judgment reference

This annex collects the principal conceptual judgments in one place.

## A.1 Type formation

```text
Γ ⊢ T type
```

`T` is a well-formed verification type.

## A.2 Term typing

```text
Γ ⊢ t : T
```

`t` is a well-typed formal value of `T`.

## A.3 Proposition formation

```text
Γ ⊢ P prop
```

`P` is a well-formed proposition.

## A.4 Evidence typing

```text
Γ ⊢ e : Proof<P>
```

`e` is checked evidence for `P`.

## A.5 Definitional equality

```text
Γ ⊢ t ≡ u : T
```

`t` and `u` have the same canonical formal meaning at type `T`.

## A.6 Program-state satisfaction

```text
Σ ⊨ P
```

state `Σ` satisfies proposition `P` under the modeled semantics.

## A.7 Capability satisfaction

```text
Σ ⊨ readable(p,n)
Σ ⊨ writable(p,n)
```

the relevant state-sensitive C++ access requirement is established.

## A.8 Hoare judgment

```text
Γ; Ω ⊢ {P} C {Q}_normal
```

normal-return partial correctness.

## A.9 Total-correctness judgment

```text
Γ; Ω ⊢ {P} C {Q}_total
```

partial correctness plus required termination.

## A.10 Refinement validity

```text
Γ; Ω ⊢ Valid(T,v)
```

the current logical value `v` satisfies semantic validity of `T`.

---

# Annex B — Core proof-rule catalogue

This annex summarizes the admissible logical rules.

It specifies semantics, not implementation primitive count.

An implementation may implement a rule directly or derive it from smaller rules
when the accepted evidence has the same meaning.

## B.1 Reflexivity

```text
Γ ⊢ t : T
-------------------------
Γ ⊢ refl : Eq<T>(t,t)
```

## B.2 Equality substitution

```text
Γ ⊢ e1 : Eq<T>(a,b)
Γ ⊢ e2 : P(a)
-------------------------
Γ ⊢ subst(e1,e2) : P(b)
```

where `P(-)` is a checked proposition context.

## B.3 Implication introduction

```text
Γ, h:P ⊢ e:Q
----------------
Γ ⊢ λh.e : P -> Q
```

## B.4 Implication elimination

```text
Γ ⊢ f : P -> Q
Γ ⊢ p : P
---------------
Γ ⊢ f(p) : Q
```

## B.5 Conjunction introduction

```text
Γ ⊢ p : P
Γ ⊢ q : Q
----------------
Γ ⊢ (p,q) : P && Q
```

## B.6 Conjunction elimination

```text
Γ ⊢ e : P && Q
----------------
Γ ⊢ left(e) : P
```

and:

```text
Γ ⊢ e : P && Q
----------------
Γ ⊢ right(e) : Q
```

## B.7 Disjunction introduction

```text
Γ ⊢ p : P
----------------
Γ ⊢ inl(p) : P || Q
```

or:

```text
Γ ⊢ q : Q
----------------
Γ ⊢ inr(q) : P || Q
```

## B.8 Disjunction elimination

```text
Γ ⊢ d : P || Q
Γ, p:P ⊢ r1 : R
Γ, q:Q ⊢ r2 : R
----------------------
Γ ⊢ cases(d,r1,r2) : R
```

## B.9 Universal introduction

```text
Γ, x:T ⊢ e : P(x)
x fresh
-------------------------
Γ ⊢ Λx.e : forall x:T, P(x)
```

## B.10 Universal elimination

```text
Γ ⊢ f : forall x:T, P(x)
Γ ⊢ t : T
--------------------------
Γ ⊢ f[t] : P(t)
```

## B.11 Existential introduction

```text
Γ ⊢ w : T
Γ ⊢ e : P(w)
-----------------------------
Γ ⊢ pack(w,e) : exists x:T, P(x)
```

## B.12 Existential elimination

```text
Γ ⊢ ex : exists x:T, P(x)
Γ, x:T, h:P(x) ⊢ e : Q
x does not escape into Q
----------------------------
Γ ⊢ unpack(ex,e) : Q
```

## B.13 False elimination

```text
Γ ⊢ f : False
--------------
Γ ⊢ absurd(f) : P
```

for any well-formed `P`. `False` is a proposition of the core with no
introduction rule; see §26 for how evidence for it arises.

## B.14 Conditional/case composition

A decidable modeled discriminator may compose proofs of both outcomes into a
proof independent of the discriminator.

The implementation may express this through an explicit conditional-elimination
proof constructor or derive it from a formal finite decision principle.

It must check both branches and must not grant either discriminator globally.

---

# Annex C — Refinement equations

For ordinary base type `T` without refinement-bearing substructure:

```text
Valid(T,v) = True
```

For refinement:

```text
R = {x:T | P(x)}

Valid(R,v)
=
Valid(T,v) && P(v)
```

For nested refinement:

```text
S = {x:R | Q(x)}

Valid(S,v)
=
Valid(T,v) && P(v) && Q(v)
```

For a record/object whose semantic validity includes refinement-bearing
subobjects:

```text
Valid(C,o)
=
AND_i Valid(T_i, pi_i(o))
```

over the live relevant refinement-bearing subobjects defined by `SPEC.md`.

For an array:

```text
Valid(Array<T,n>, a)
=
forall i in [0,n), Valid(T, at(a,i))
```

as a semantic schema; implementation need not literally expand large arrays.

For a union:

```text
Valid(U,u)
=
Valid(T_active, active_projection(u))
```

for the active member only.

For a reference:

```text
Valid(T&, r)
=
Valid(T, current(referent(r)))
```

subject to reference/lifetime semantics.

For a pointer:

```text
Valid(T*, p)
```

does not imply:

```text
Valid(T, *p)
```

and does not imply `readable(p)` or `writable(p)`.

---

# Annex D — Partial-correctness schemas

## D.1 Sequence

For:

```text
C1;
C2;
```

a sound verification composes the post-state of `C1` into the pre-state of `C2`.

## D.2 Assignment

A write updates the target PlaceVersion and invalidates every fact not proven
stable under alias/effect analysis.

## D.3 Conditional

Both reachable branches must establish the continuation obligation under their
respective path conditions.

## D.4 Loop

Invariant entry and every continuing-edge preservation obligation must be proved.

## D.5 Call

The caller establishes entry obligations, applies effects, then gains only the
accepted post-state facts.

## D.6 Return

The returned value, refined return validity, destructors/effects required before
normal return, and instantiated `ensures` must all be accounted for.

## D.7 Throw

The path transfers to exceptional semantics and does not owe the normal
postcondition merely by throwing.

---

# Annex E — Mathematical-domain reference

## E.1 `@N`

```text
unbounded natural numbers
exact arithmetic
well-founded natural order
```

## E.2 `@Z`

```text
unbounded mathematical integers
exact arithmetic
```

## E.3 `@Seq<T>`

```text
finite sequence
size : @N
at : index-in-bounds -> T
```

## E.4 `@Set<T>`

```text
finite mathematical set
contains : proposition
```

## E.5 `@Map<K,V>`

```text
finite mathematical map
contains(map,key) : proposition
at(map,key) : V under membership premise
```

## E.6 No runtime identity

None of these domains has:

```text
runtime object layout
address
storage duration
ABI
constructor/destructor
allocator
```

merely from its mathematical-domain identity.

---

# Annex F — Meta-theory obligations

A mature formalization should mechanize or otherwise establish the following
properties for the formal core.

## F.1 Well-formedness preservation

Formal constructors preserve typing/well-formedness under their premises.

## F.2 Substitution

Capture-avoiding substitution preserves typing and proposition formation.

## F.3 Normalization soundness

If normalization reports:

```text
t ≡ u
```

the two terms have equal formal meaning.

## F.4 Proof checking soundness

Accepted proof evidence establishes its claimed proposition in the formal model.

## F.5 Arithmetic checker soundness

Accepted arithmetic certificates imply the formal arithmetic consequence they
claim under exact machine/mathematical semantics.

## F.6 Abstract-observation type safety

Every observation projection has the declared result type for its nominal
signature and substitution preserves that typing.

## F.7 Termination of proof-relevant reduction

Every admitted proof-relevant reduction sequence terminates.

## F.8 No hidden axiom admission

The formal core has no path that converts arbitrary propositions into checked
evidence without either a formal rule or explicit trust provenance.

## F.9 Erasure correspondence target

For language-defined erasure, the executable behavior of the C++L program and its
runtime projection agree for the semantics promised by `SPEC.md`.

This theorem necessarily composes formal reasoning with implementation/runtime
correspondence and therefore extends beyond the proof kernel alone.

---

# Annex G — Foundational review checklist

When adding or changing a proof feature, review all of the following.

## G.1 Proposition formation

- What proposition does the feature state?
- Is it value-based, state/capability-based, or a combination?
- Are all binders explicit and scoped?

## G.2 Evidence

- What evidence establishes the proposition?
- Is the rule primitive or derived?
- Could an automation success bit bypass evidence checking?

## G.3 Equality

- Does the feature rely on definitional equality or propositional equality?
- Is substitution capture-safe?
- Are machine and mathematical equalities distinguished?

## G.4 C++ correspondence

- Which C++ semantic facts are needed?
- Are conversions/overloads/value categories resolved by the selected C++
  authority?
- Could the proof describe a different operation than runtime executes?

## G.5 State

- Which Places/Regions does the property depend on?
- Which writes/calls can invalidate it?
- Are capabilities and bounds modeled separately where required?

## G.6 Refinements

- Does any new value cross into refinement-bearing semantics?
- Is `Valid(T,v)` established for the current version?
- Does mutation invalidate stale validity?

## G.7 Termination

- Is any new computation proof-relevant?
- What makes it total?
- Is an induction argument being confused with a termination argument?

## G.8 Trust

- Does the change introduce a new assumption?
- If yes, is it expressible only through the normative trusted surface?
- Is trust provenance transitive and reportable?

## G.9 Erasure

- Does proof-only information remain runtime-irrelevant?
- Is any runtime check accidentally erased or inserted?
- Is ABI unchanged where `SPEC.md` requires it?

---

# Annex H — Intellectual-credit summary

C++L's formal model combines ideas from several established traditions:

```text
Curry–Howard
+
natural deduction
+
dependent and refinement typing
+
Floyd–Hoare program reasoning
+
weakest-precondition / predicate-transformer reasoning
+
well-founded induction and termination
+
machine arithmetic semantics
+
automated decision procedures
+
small-checker proof validation
+
explicit C++ state and runtime correspondence
```

The project does not claim invention of those foundations.

Its engineering challenge is to make them coexist with real C++ semantics,
incremental adoption, native ABI compatibility, proof erasure, and explicit
trust.

---

# Final foundational statement

C++L's mathematical foundation can be summarized as:

```text
state a proposition precisely

derive it only from checked evidence and explicit premises

track every assumption

model the machine operation that really executes

treat mutable storage as versioned state

require validity at every refinement crossing

require capability before memory access

require well-foundedness before proof-relevant recursion

erase proof-only information

and never confuse proof of a formal model with correspondence of that model to C++.
```

The proof system is successful only when:

```text
formal truth
+
source correspondence
+
runtime correspondence
```

remain aligned.
