# C++L Mathematical Foundations

C++L does not invent a new foundation for mathematics. I want to credit original authors here and thank those great people for their work.

Its proof system builds on established ideas from mathematical logic, type theory, program verification, and automated reasoning.

## Central idea: Curry–Howard

The central principle is the **Curry–Howard correspondence**:

```text
proposition ≈ type
proof       ≈ program/value inhabiting that type
```

A theorem such as:

```text
∀ x : ℕ,
    add(x, 0) = x
```

can be represented as a dependent function type:

```text
Π (x : ℕ),
    Eq(add(x, 0), x)
```

A valid proof is a program whose type is exactly that proposition.

Conceptually:

```text
add_zero :
    Π (x : ℕ),
        Eq(add(x, 0), x)
```

Here ℕ is mathematical notation; C++L source writes it `@N`. The same shape applies when `x` is a C++ `unsigned`, under machine semantics (`SPEC.md` §21.2).

If the C++L proof kernel verifies that the proof term has this type, the theorem is established.

The theorem is not established by trying many values of `x`.

It is established symbolically.

## Intellectual lineage

C++L stands on decades of work in logic, programming-language theory, theorem proving, and program verification.

Particular intellectual credit is due to:

- **Gerhard Gentzen** - natural deduction and structural proof systems.
- **Alonzo Church** - lambda calculus and foundational connections between logic and computation.
- **Haskell Curry** - early correspondence between logical propositions and types.
- **William Alvin Howard** - the explicit propositions-as-types / proofs-as-programs correspondence.
- **Nicolaas de Bruijn** - AUTOMATH and early machine-checked formal mathematics.
- **Per Martin-Löf** - intuitionistic dependent type theory, identity types, inductive types, and constructive type theory.
- **Robert W. Floyd** - formal reasoning about program correctness.
- **C. A. R. Hoare** - axiomatic program semantics and Hoare logic.
- **Edsger W. Dijkstra** - weakest preconditions and predicate-transformer semantics.
- **Thierry Coquand** and **Gérard Huet** - the Calculus of Constructions and foundational work leading to modern proof assistants.
- **Tim Freeman** and **Frank Pfenning** - early practical refinement typing, together with later refinement-type research.
- Researchers behind SAT/SMT decision procedures, including the Nelson–Oppen tradition and the teams behind Z3, cvc5, and related systems.
- The designers and communities behind modern proof-oriented systems such as Rocq/Coq, Lean, Agda, Idris, F\*, Dafny, and related projects.

C++L does not claim invention of these mathematical foundations.

Its intended contribution is integrating them into a source-compatible C++ superset whose proof layer can be erased before normal native compilation.

## Foundations by C++L feature

| C++L concept                       | Mathematical foundation              | Key contributors                                  |
| ---------------------------------- | ------------------------------------ | ------------------------------------------------- |
| Proofs as typed values             | Curry–Howard correspondence          | Haskell Curry, William A. Howard                  |
| Formal proof rules                 | Natural deduction                    | Gerhard Gentzen                                   |
| Functional computation             | Lambda calculus                      | Alonzo Church                                     |
| Machine-checked formal mathematics | AUTOMATH                             | Nicolaas de Bruijn                                |
| Dependent types                    | Intuitionistic dependent type theory | Per Martin-Löf                                    |
| Identity/equality types            | Martin-Löf type theory               | Per Martin-Löf                                    |
| Induction and case analysis        | Constructive type theory             | Per Martin-Löf and related type-theory tradition  |
| Preconditions/postconditions       | Hoare logic                          | C. A. R. Hoare                                    |
| Program correctness logic          | Floyd–Hoare reasoning                | Robert W. Floyd, C. A. R. Hoare                   |
| Weakest preconditions              | Predicate-transformer semantics      | Edsger W. Dijkstra                                |
| Calculus of Constructions          | Higher-order dependent type theory   | Thierry Coquand, Gérard Huet                      |
| Refinement types                   | Refinement typing                    | Tim Freeman, Frank Pfenning and later researchers |
| Automated logical reasoning        | SAT/SMT and decision procedures      | Nelson, Oppen, Z3/cvc5 and related communities    |

## Propositions as types

For example:

```cpp
Proof<x == x> reflexivity(x);
```

means that `reflexivity(x)` must construct valid evidence for:

```text
x = x
```

A false proposition has no valid inhabitant in the verified language.

This should therefore be impossible:

```cpp
Proof<1 == 2> impossible;
```

unless the trusted foundation itself is inconsistent.

This differs fundamentally from:

```cpp
bool theorem = (1 == 2);
```

A `bool` is a runtime truth value.

A proposition type expresses a logical statement whose inhabitants constitute proofs.

## Dependent type theory

Ordinary types classify values:

```text
x : int
```

Dependent types allow types to depend on values.

Examples:

```text
Vector<T, n>
Fin<n>
```

`Fin<n>` represents a value known to satisfy:

```text
0 <= value < n
```

A function can therefore express relationships that ordinary C++ signatures cannot:

```cpp
T get<T, n>(
    const Vector<T, n>& xs,
    Fin<n> index
);
```

The relationship between `index` and `n` is checked formally rather than left as documentation.

## Universal quantification

To prove:

```text
∀ x : T,
    P(x)
```

the proof must construct:

```text
P(x)
```

for an arbitrary `x : T`.

Under Curry–Howard, universal quantification corresponds to a dependent function:

```text
Π (x : T), P(x)
```

This is why C++L does not need to enumerate every possible value.

## Existential quantification

An existential proposition:

```text
∃ x : T,
    P(x)
```

contains:

```text
a witness x
+
proof that P(x)
```

Conceptually:

```text
Exists<T, P>
=
(x : T, Proof<P(x)>)
```

An existential theorem must provide both a witness and evidence.

## Induction

Universal properties over recursively structured domains are proven structurally.

For the natural numbers ℕ, to prove:

```text
∀ n : ℕ,
    P(n)
```

it is sufficient to establish:

```text
P(0)
```

and:

```text
∀ n,
    P(n) → P(n + 1)
```

This proves the property for every natural number without enumerating:

```text
0, 1, 2, 3, ...
```

C++L applies this principle to the values a C++ program already has. It does not ask the program to redeclare them as inductive types. Each principle must be well founded and must match runtime semantics:

- machine integers, over their actual range, where the successor step never wraps
- pointer-linked lists and trees, given an explicit well-founded premise such as finite acyclic reachability
- proof-only mathematical domains: `@N`, `@Z`, `@Seq<T>`, `@Set<T>`, `@Map<K, V>`

Case analysis is the non-recursive form of the same idea: one obligation per case of a value. The cases cover every state the C++ type permits, including residual states such as an enumeration value that matches no enumerator.

Case analysis is derived, not primitive. A decomposition provider describes a
representation's states as **discriminators**: decidable conditions on modeled
values. Splitting on one is conditional elimination, whose two premises the
kernel derives and checks itself; splitting on each in turn leaves one branch in
which all are false, and conjunction introduction combines those negations into
the residual case's fact. Exhaustiveness is therefore a property of checked
evidence rather than an assumption about the representation, and no additional
logical rule is necessary — for scoped enumerations or for any later
representation whose states are distinguished this way.

The scoped-enumeration provider maps values to their exact fixed underlying
machine integer domain, so exhaustiveness never relies on assuming that every
enum value has a name.

Every other implemented representation is one of two shapes over the abstract
value model below, which is why none of them needs a rule of its own.

A **tagged sum** is a value whose signature is one discriminating observation
followed by one payload observation per state. Its discriminators are equalities
or Boolean tests on that first observation, which is total, so the split is
exhaustive by the same argument as above. `std::variant` discriminates on an
alternative index, leaving `valueless` as the residual branch in which no index
matches; `std::optional` and `std::expected` discriminate on a Boolean
observation, leaving `none` and `error`. A pointer is the degenerate case: a
Boolean discriminator, `null`, and a residual `non_null` that binds nothing,
since no observation of a pointer's pointee is admitted.

A **product** is a value whose signature is its component list. It has one
state, so it contributes no discriminator and no split at all: `decompose` binds
each component to `pi_k(v)` and continues with the same goal. Records,
`std::pair`, `std::tuple`, `std::array` and built-in arrays differ only in how
Clang reports that list.

Because a binding is an observation of an existing value rather than a new
value, decomposition composes without any pairwise rule: `pi_k(v)` is itself a
term whose type may carry its own signature, and decomposing it is the same
construction applied again.

The core also admits abstract nominal value sorts with finite typed observation
signatures. For `v : V(i; T0, ..., Tn)`, observation `pi_k(v)` has type `Tk`.
This is a total uninterpreted function, with no computation rule beyond
congruence and no axiom stating anything about its result. Abstract equality
remains intensional; products gain no extensionality rule. Signatures are
finite trees, so substitution and normalization remain structurally bounded.
Partial C++ observers are exposed only under their defined-state premise;
outside that state their logical totalization supplies no usable C++ fact.

The normative rules are in `SPEC.md` §20–§21.

## Equality

C++L distinguishes two important forms of equality.

### Definitional equality

Two terms are definitionally equal when computation reduces them to the same canonical form.

For example:

```text
add(0, x)
```

may normalize to:

```text
x
```

when `add` is defined by recursion on its first argument, so:

```text
add(0, x) ≡ x
```

can be established directly.

No separate theorem is required.

### Propositional equality

Some equalities require explicit evidence:

```text
Eq<T>(a, b)
```

or conceptually:

```cpp
Proof<a == b>
```

C++L must support reasoning principles such as:

- reflexivity
- symmetry
- transitivity
- substitution
- rewriting
- congruence
- transport across dependent types

## Hoare logic

C++L contracts draw from Hoare-style program logic.

A Hoare triple has the form:

```text
{ P } C { Q }
```

meaning:

> If precondition `P` holds before executing program `C`, then postcondition `Q` holds afterward under the verified execution semantics.

Example:

```cpp
pure int divide(int a, int b)
    expects (b != 0)
    ensures (result * b == a);
```

corresponds conceptually to:

```text
{ b != 0 }

divide(a, b)

{ result * b == a }
```

This makes preconditions and postconditions formal proof obligations rather than documentation.

## Weakest preconditions

The implemented conditional fragment checks both implications
`condition -> Q[true_return]` and `not condition -> Q[false_return]` before
establishing `Q[select(condition, true_return, false_return)]`. This is a
conditional-elimination rule, not an assumption that either path's condition
holds globally. Comparisons compute only on concrete integer operands;
symbolic order reasoning is outside this fragment. See `SPEC.md` 12.7.

C++L may use Dijkstra-style weakest-precondition reasoning when verifying imperative code.

Conceptually:

```text
wp(C, Q) = P
```

means:

> `P` is the condition that must hold before executing `C` in order to guarantee `Q` afterward.

This allows the verifier to reason backward from a desired postcondition through:

- assignments
- branches
- loops
- function calls
- state updates

## Storage, capabilities and framing

Hoare logic assigns meaning to a *state*, and the classical difficulty is not
the assignment rule but the **frame**: which parts of the state a command leaves
alone. C++L's storage model (`SPEC.md` 12.10, RFC 0014) is the answer to that
question, and it is deliberately the conservative one.

A **place** is where a value lives, a **region** is the object it belongs to,
and a **capability** is what the state permits there. Assignment to a place is
the ordinary Hoare assignment rule applied to one place, with two additions:
the write must satisfy the declared refinement of its target before it is bound,
and every place that **may alias** the target loses its facts.

That last clause is the frame rule, stated in the direction that fails safe:

```text
a fact survives a write   only if   its place is proved disjoint from the target
```

rather than the more permissive reading, under which a fact survives unless
aliasing is proved. Disjointness is derived from Clang-resolved structure —
distinct locals, distinct members, distinct proved indices — and never from
type-based aliasing, because that inference presupposes the
undefined-behavior freedom the proof has not yet established and would make the
reasoning circular.

Capabilities are not propositions in the kernel's logic. They are hypotheses the
obligation layer tracks, because validity is a property of the *state*, not a
computable function of any value: `free(p)` destroys the validity of `*p`
without changing `p`. Predicates over values cannot express that, which is why
the model attaches lifetime and extent to regions. What does reach the kernel
is everything genuinely requiring proof — refinement membership on each write,
and `index < extent` for each subscript, both ordinary propositions over terms.

Separation logic answers the same framing question with more precision, and the
place/region model is compatible with adding it later as a layer above. It is
not adopted now because its precision would come at the cost of a much larger
trusted core than the conservative frame rule requires.

## Refinement typing

A refinement type augments a base type with a predicate.

For example:

```cpp
type Percentage = int where self >= 0 && self <= 100;
```

corresponds mathematically to:

```text
{ x : Int | 0 <= x <= 100 }
```

A function returning `Percentage` must produce an integer together with sufficient evidence that the predicate is true.

Simple refinement obligations may be discharged automatically by arithmetic solvers.

Refinement is a property of values, not a change of representation. The subset
relation is what carries evidence in each direction:

```text
{ x : T | P(x) }  <:  T                        needs nothing
T                 <:  { x : T | P(x) }         needs a proof of P
{ x : T | P(x) }  <:  { x : T | Q(x) }         needs  forall x : T, P(x) -> Q(x)
```

The first is why a refined value is usable as its base value, and the second is why
every flow into a refinement type is an obligation. The third is what crossing
between two refinements of one base type amounts to: the value already carries `P`,
so what it owes is only the part of `Q` that `P` does not give. Composing refinements conjoins
their predicates, so `{ x : {y : T | P(y)} | Q(x) }` is `{ x : T | P(x) /\ Q(x) }`,
with the same base type underneath.

For mutable storage, membership is indexed by the value version: `P(v0)` does
not imply `P(v1)` after a write. A possible alias mutation introduces a universally
quantified unknown for the new observation. A verified call supplies its proved
postcondition at that new value, conditional on its checked entry requirements.
This changes obligation construction, not the logical inference rules.

Because the base type is what exists at run time, two distinct refinements of one
base type are the same type to the machine and different types to the
verifier. Nothing about the machine's view is allowed to depend on which one a value
was given.

## Termination and consistency

Proof-producing computation cannot be allowed to justify arbitrary propositions by never returning.

For example, a hypothetical function:

```text
prove_false() : Proof<False>
```

cannot be accepted merely because its implementation loops forever.

Therefore C++L requires verified proof-producing recursion to be:

- structurally decreasing,
- well-founded,
- or accompanied by a proved termination measure.

This connects program termination directly to logical consistency.

## Trusted proof kernel

C++L follows the small-kernel philosophy used by major proof systems.

Complex components may generate proofs:

```text
AI
tactics
rewriters
SMT solvers
elaborator
automation
proof search
```

but they should not independently define what is true.

The intended architecture is:

```text
        complex automation
               ↓
          proof evidence
               ↓
      ┌─────────────────┐
      │ trusted kernel  │
      │                 │
      │ small           │
      │ deterministic   │
      │ auditable       │
      └────────┬────────┘
               ↓
          valid / invalid
```

The kernel is the final authority.

This matters especially for AI-generated software.

An AI should not be able to claim:

```text
I proved the Law.
```

The compiler must independently verify the proof.

Likewise, an SMT solver should ideally generate checkable evidence rather than becoming the permanent definition of truth.

## Automated reasoning

Not every proof should require manual construction.

C++L may use:

- SMT solving
- SAT solving
- rewriting
- normalization
- congruence closure
- arithmetic decision procedures
- bitvector reasoning
- proof search
- simplification

The mathematical role of automation is to construct or discharge proof obligations.

The trusted kernel remains responsible for accepting proof evidence whenever practical.

## C++L mathematical summary

C++L combines several established traditions:

```text
Curry–Howard
+
Dependent Type Theory
+
Induction
+
Hoare / Floyd Program Logic
+
Dijkstra Weakest Preconditions
+
Refinement Types
+
Automated Reasoning
+
Machine-Checked Proof
+
C++ Runtime Semantics
=
C++L
```

The central objective is:

> Express intent as mathematics, prove implementation against that intent, erase the proof layer, and execute ordinary optimized C++.
