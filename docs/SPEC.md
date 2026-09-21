# C++L Language Specification

**C++L - C++ with Laws**

Status: Draft specification

This document defines the normative language semantics of C++L.

It defines what C++L programs mean.

It does **not** define:

- compiler architecture;
- implementation component boundaries;
- proof-kernel implementation;
- solver implementation;
- caching;
- editor integration;
- release planning;
- current implementation status.

Those belong in:

```text
ARCHITECTURE.md
TRUST.md
DESIGN.md
FOUNDATIONS.md
COMPATIBILITY.md
STATUS.md
```

---

# 1. Normative terminology

The words:

```text
MUST
MUST NOT
SHOULD
SHOULD NOT
MAY
```

are normative requirements.

C++L is defined relative to a selected supported C++ language mode.

The supported C++ versions and implementation-specific compatibility guarantees are defined in `COMPATIBILITY.md`.

---

# 2. Language relationship to C++

C++L is a source-compatible superset of supported C++.

The fundamental relationship is:

```text
C++ ⊂ C++L
```

For every supported C++ program:

```text
valid supported C++
    remains
valid C++L
```

when no C++L-specific semantics are requested.

C++L MUST preserve the observable runtime semantics of ordinary supported C++.

C++L adds formal specification and verification semantics.

It does not redefine ordinary C++ merely because the program is compiled as C++L.

---

## 2.1 Zero-change adoption principle

An existing supported C++ program MUST NOT be required to adopt C++L syntax merely to continue compiling.

Formal verification is additive.

A project MAY contain:

```text
ordinary C++
verified C++L
trusted boundaries
unsafe boundaries
runtime-validated boundaries
```

at the same time.

Migration to verification is incremental.

---

## 2.2 Implementation standard is independent of source standard

The language used to implement a C++L compiler is independent of the C++ language mode selected for user source.

For example:

```text
compiler implementation
    C++23

user source
    C++17
```

is valid.

A C++L implementation MUST NOT introduce runtime constructs unavailable in the selected C++ target mode merely because the compiler itself was built using a newer C++ standard.

---

# 3. Contextual C++L words

C++L introduces contextual language words.

They are not globally reserved identifiers. Outside the grammatical contexts
defined by this specification, they remain ordinary C++ identifiers.

The core contextual words defined by this specification are:

```text
law
proof
proves
pure
verified
ghost
unsafe
trusted
type
where
expects
ensures
decreases
invariant
forall
exists
```

The following identifiers have special meaning only inside their corresponding specification contexts:

```text
result
old
self
```

The following words have special meaning only as proof statements inside a proof body (§15):

```text
refl
exact
apply
assume
rewrite
cases
induction
```

C++L does not define `data` or `match`. It introduces no algebraic data types and no runtime pattern matching (§19).

The mathematical-domain spellings `@N`, `@Z`, `@Seq`, `@Set` and `@Map` are C++L tokens (§19.1). No valid C++ program contains them outside literals and comments.

Residual case labels such as `unnamed` and `valueless` have meaning only as arm labels of the construct that defines them (§20.1). They are not reserved identifiers.

Existing C++ keywords retain their existing C++ meaning.

C++L MUST NOT redefine an existing C++ keyword for unrelated C++L semantics.

In particular:

```text
requires
```

belongs to C++ and is not a C++L contract keyword.

---

## 3.1 C++-first disambiguation

When a token sequence is valid ordinary C++ in the current context, the ordinary C++ interpretation takes precedence.

C++L contextual interpretation applies only where the complete grammatical context identifies a C++L construct.

For example:

```cpp
int law = 1;

void proof();

struct ghost {};
```

remains ordinary C++.

C++L MUST NOT globally reinterpret those identifiers.

---

## 3.2 Preprocessing

Normal C++ preprocessing occurs before C++L contextual interpretation.

Therefore existing macros remain meaningful.

A macro MAY expand into C++L syntax.

C++L MUST NOT change ordinary preprocessor token semantics merely because a token has contextual C++L meaning after preprocessing.

There is one exception. Each mathematical-domain spelling `@N`, `@Z`, `@Seq`, `@Set` and `@Map` is lexed as a single preprocessing token (§19.1), so a macro named `N`, `Z`, `Seq`, `Set` or `Map` does not expand inside it. `@` cannot appear in valid C++ outside literals and comments. The only programs whose meaning this could change are those that stringize such a spelling after macro expansion.

---

# 4. Semantic domains

C++L distinguishes three semantic domains:

```text
runtime
specification
proof
```

---

## 4.1 Runtime domain

The runtime domain contains ordinary executable C++ values and operations.

Examples:

```cpp
int
std::string
std::vector<T>
objects
references
pointers
exceptions
I/O
```

Runtime semantics remain governed by the selected C++ language mode and target environment.

---

## 4.2 Specification domain

The specification domain expresses properties of runtime or formal values.

Examples include:

```text
preconditions
postconditions
Laws
refinement predicates
loop invariants
termination measures
```

Specification expressions MUST be side-effect-free.

---

## 4.3 Proof domain

The proof domain contains evidence establishing propositions.

Proof-domain values:

```text
do not have runtime identity
do not affect runtime control flow
do not affect runtime ABI
are erased before ordinary execution
```

unless this specification explicitly states otherwise.

---

# 5. Propositions

A proposition is a formal statement that may be established by proof.

Conceptually:

```text
P : Prop
```

C++L defines a proof type conceptually equivalent to:

```text
Proof<P>
```

A value inhabiting:

```text
Proof<P>
```

is evidence for proposition `P`.

A proof value MUST NOT exist unless it is derivable according to the formal rules of the language or introduced explicitly through `trusted`.

---

# 6. Boolean propositions

A side-effect-free C++ expression of type `bool` MAY be lifted into a proposition.

For example:

```cpp
x >= 0
```

inside:

```cpp
ensures (x >= 0)
```

means:

```text
the C++ expression x >= 0 evaluates to true
```

under the selected C++ semantics.

This does not turn C++ `bool` into the same type as `Prop`.

It is a defined conversion from a pure Boolean expression into a proposition.

---

# 7. Logical equality

C++L distinguishes:

```text
definitional equality
```

from:

```text
propositional equality
```

They MUST NOT be conflated.

---

## 7.1 Definitional equality

Two formal terms are definitionally equal when the formal reduction and normalization rules reduce them to the same canonical meaning.

Conceptually:

```text
a ≡ b
```

Definitional equality requires no explicit equality theorem.

Example:

```text
identity(x)
```

may reduce definitionally to:

```text
x
```

if `identity` is defined to return its argument.

### 7.1.1 Machine-integer arithmetic

The formal core's wrapping addition, subtraction and multiplication of a
machine integer type of width `w` are the operations of the ring of integers
modulo `2^w`, whatever the type's signedness. Normalization MUST read a term
built from them as a polynomial over its non-arithmetic subterms, with
coefficients modulo `2^w`, and MUST render that polynomial in one canonical
form. Two such terms are therefore definitionally equal exactly when they are
equal as polynomials; every commutative-ring identity, including associativity,
commutativity, distributivity, the identities of zero and one, cancellation of
addition, and exact folding of constants, is definitional. Such equality implies
equality of machine values under every assignment, never the converse: a
polynomial identity that holds only for particular widths is not definitional.

Comparisons MUST be normalized only by rewrites that are identities of the
machine type:

```text
a > b   is  b < a
a <= b  is  !(b < a)
a >= b  is  !(a < b)
a != b  is  !(a == b)
!!c     is  c
a == b  is  (a - b) == 0, stated of the difference or its negation
```

`a < b` is decided only when both operands are literals, when they are
identical, or at the bounds of the type (`a < min` and `max < b` are false). No
arithmetic is moved across `<`: order is not cancellative under wrapping, so
`x + 1 < y + 1` and `x < y` stay distinct. A selection whose arms are equal is
that arm; a selection on a negated condition exchanges its arms.

Normalization MUST fail rather than approximate when a polynomial exceeds the
implementation's size bounds.

---

## 7.2 Propositional equality

Propositional equality is an explicit proposition:

```text
Eq<T>(a, b)
```

or an equivalent surface form defined by C++L.

Evidence for equality is proof evidence.

Reflexivity establishes:

```text
Eq<T>(x, x)
```

Conceptually:

```text
refl : Proof<Eq<T>(x, x)>
```

---

### 7.2.1 Current explicit-equality fragment

The current implementation accepts `Eq<T>(a, b)` as a complete Law, proof,
precondition, postcondition, or assumed proposition for modeled built-in
integer and Boolean types. `T` and both arguments are resolved by Clang;
conversions outside the modeled fragment are refused. The logical form remains
distinct from a C++ `operator==` invocation. Explicit equalities compose through
conjunction, implication and equivalence and under universal quantification, but
are not themselves values another explicit equality can compare. Formal
propositions used as ordinary C++ values are refused. Inside a specification
expression the `Eq<T>(a, b)` spelling is read as this formal form even where an
ordinary C++ declaration of that name is visible; that declaration keeps its own
meaning everywhere else, including at runtime. These implementation limits do not
narrow the semantics above.

---

## 7.3 C++ `operator==`

A C++ expression:

```cpp
a == b
```

inside a specification expression retains its ordinary C++ semantics.

It produces a Boolean predicate that may be lifted into a proposition.

It is not automatically identical to formal `Eq<T>(a, b)`.

For built-in or formally modeled values the verifier MAY prove a correspondence between the two.

---

## 7.4 Equality operations

The formal system MUST support sound equivalents of:

```text
reflexivity
symmetry
transitivity
substitution
congruence
transport
```

where applicable.

Approximate equality MUST NOT silently become formal equality.

---

## 7.5 Linear arithmetic over machine integers

Order consequences such as `i < n -> i + 1 <= n` are not identities and are
not definitional. They are established by a proof rule whose premises are facts,
each an equality or comparison with its own evidence, and whose conclusion is an
equality or comparison.

The kernel MUST state the facts and the negation of the conclusion as integer
linear constraints itself:

```text
each distinct monomial m of type T      an integer variable v, min(T) <= v <= max(T)
a term t of type T whose polynomial     sum(c_m * v_m) + c - 2^w * k,
  is not a single monomial                with k a fresh integer variable,
                                          bounded by min(T) and max(T)
a == b  (values)                        L_a = L_b
(a < b) == 1,  (a < b) == 0              L_a + 1 <= L_b,  L_b <= L_a
(a == b) == 0                           L_a + 1 <= L_b  or  L_b + 1 <= L_a
```

This encoding is exact for two's-complement arithmetic: the machine value of
`t` is the only value within the bounds of `T` that differs from its polynomial
by a multiple of `2^w`. A certificate MUST then show that the system has no
integer solution, by a tree of Farkas sums (nonnegative multiples of standing
constraints whose sum has no variable and a positive constant), integer splits
(a linear form is at most zero or at least one) and case splits on disjunctions.
The kernel MUST check every step with exact arithmetic and MUST refuse, never
wrap, on overflow. Contradictory facts establish any arithmetic conclusion, as
they do in any sound logic. The rule adds no assumption.

---

## 7.6 Conjunction

The proposition `P && Q` requires evidence for both `P` and `Q`. Its kernel
introduction rule checks each proof against its own side. Elimination takes
checked evidence for the whole conjunction and yields the selected side; the
other side MUST NOT be silently dropped before checking that evidence.
Conjunction binds no variables. Substitution and shifting act on both sides
under the same surrounding binders.

### 7.6.1 Current conjunction fragment

The implementation lifts Clang-resolved built-in `&&` between modeled Boolean
specification expressions into conjunction. It supports nested conjunctions in
Laws, direct proof propositions, `expects`, `ensures`, and `assume`, including
inside a `forall` body or on either side of implication. Ordinary C++ operands,
operator selection, types and conversions remain Clang's responsibility.
Only pure, modeled operands are admitted; short-circuiting cannot conceal an
unsupported or effectful operand.

`refl` introduces a conjunction when each side closes definitionally. `exact`
can reuse evidence for the whole conjunction; `apply` can establish it after
discharging the evidence's premises. `rewrite` traverses both sides. Automation
may construct or project conjunction evidence, including for arithmetic facts,
but the kernel checks every introduction and elimination. No written statement
selects one side of a conjunctive premise: a named premise is used whole, and
elimination is left to automation, which the kernel still checks.

Explicit formal forms may be operands of conjunction. `&&` used as a value,
runtime guard in a verified body, or loop invariant is not yet modeled and is
refused.
These limits do not change ordinary unverified C++ expressions or their runtime
evaluation. See RFC 0009 for the implementation and trust rationale.

---

## 7.7 Logical equivalence

`P <-> Q` denotes `(P -> Q) && (Q -> P)` as specified in GRAMMAR.md 30.
Both directions require explicit kernel-checked evidence. It adds no kernel
rule or assumption. Equivalence is looser than implication and conjunction;
repeated equivalence associates to the left. It composes with explicit equality,
quantifiers, implications and conjunctions wherever a proposition is accepted.

---

## 7.8 Disjunction

The proposition `P || Q` requires evidence for one of its sides. Its kernel
introduction rule checks that evidence against the side it selects, which the
goal states; evidence MUST NOT select a side the goal does not state.
Elimination takes checked evidence for the whole disjunction together with
evidence that each side is enough for the conclusion, and yields that
conclusion. Neither side follows from the disjunction alone.

`P || not P` is not granted. A conclusion that requires knowing which side holds
is unproven, never assumed. Disjunction binds no variables; substitution and
shifting act on both sides under the same surrounding binders.

### 7.8.1 Current disjunction fragment

The implementation lifts Clang-resolved built-in `||` between modeled Boolean
specification expressions into disjunction, and admits explicit formal forms as
its operands. It is supported in Laws, direct proof propositions, `expects`,
`ensures`, and `assume`, including inside a `forall` body, on either side of an
implication, and nested within itself. Only pure, modeled operands are admitted;
short-circuiting cannot conceal an unsupported or effectful operand.

`refl` introduces a disjunction when a side closes definitionally. `exact` can
reuse evidence for the whole disjunction and `apply` can establish it after
discharging the evidence's premises, as for any other proposition. A disjunctive
premise is used by automation, which proves the goal under each side and submits
the case analysis to the kernel; no written statement selects a side or takes
cases, as none selects a side of a conjunction (7.6.1).

`||` used as a value, runtime guard in a verified body, or loop invariant is not
modeled and is refused. These limits do not change ordinary unverified C++
expressions or their runtime evaluation. See RFC 0011 for the implementation and
trust rationale.

---

# 8. Universal quantification

Parameters of a `law` are universally quantified unless explicitly stated otherwise.

For example:

```cpp
law nonnegative_square(int x)
    proves (square(x) >= 0);
```

denotes conceptually:

```text
∀ x : int,
    square(x) >= 0
```

subject to the selected C++ machine semantics.

Explicit universal quantification MAY be written in specification context as:

```cpp
forall (T x) {
    proposition
}
```

Conceptually:

```text
∀ x : T, P(x)
```

---

## 8.1 Current explicit-quantification fragment

The current implementation accepts `forall (T x, ...) { P }` as a complete Law,
proof, precondition, postcondition, or assumed proposition. At least one binder
is required, and each binder type must be a modeled built-in integer or Boolean
type; anything else is refused. The binders are ordinary C++ parameters resolved
by Clang, and they name the innermost variables of the proposition: a binder
that shadows a parameter denotes the binder, as it would in C++.

A binder is not a name any proof statement can use. Evidence is written of the
parameters a Law or proof declares, so a proposition quantified over its own
binder cannot be instantiated at a term chosen in a proof body. What a statement
may do under such a binder is suppose the premise standing there and prove the
proposition it leaves.

Loop invariants and quantifiers nested inside an ordinary C++ expression are
refused. `forall` is a formal form only in the complete form above: spelled
anywhere else, it is an ordinary C++ identifier with its own meaning.

These implementation limits do not narrow the semantics above.

---

## 8.2 Implication

A specification expression may state implication with `->`, which is looser than
every ordinary C++ operator and right associative (GRAMMAR.md 29, 33):

```cpp
ensures (x == 0u -> identity(x) == 0u)
```

denotes conceptually:

```text
P -> Q
```

which claims nothing about `P`: what it states is `Q` under the supposition of
`P`. A Law written `expects (P) ensures (Q)` states the same proposition, and both
are discharged the same way.

---

## 8.3 Current implication fragment

The current implementation accepts `->` between two specification expressions in
the same contexts as 8.1. Because `->` is also C++ member access, and because an
implication is looser than every C++ operator, `->` outside all brackets in a
specification expression is implication. Member access inside a specification
expression is therefore written inside parentheses, where the enclosing
expression is C++ and the whole of it is resolved by Clang.

---

# 9. Existential quantification

C++L supports existential propositions.

Conceptually:

```text
∃ x : T, P(x)
```

The specification form is:

```cpp
exists (T x) {
    proposition
}
```

Proof of an existential proposition requires:

```text
a witness
+
proof that the witness satisfies the proposition
```

Failure to find a witness does not prove that none exists.

---

## 9.1 Current existential fragment

Existential quantification is not implemented. The complete form above is
recognised and refused with a diagnostic saying so; nothing approximates it.
Spelled in any other form, `exists` is an ordinary C++ identifier.

---

# 10. Laws

A `law` declares formal intent.

A Law is a proposition.

It is not:

```text
a test
a runtime assertion
documentation
a solver hint
an implementation heuristic
```

---

## 10.1 Law syntax

Functions ensure. Laws prove. A Law has at most one `expects (P)` premise and
exactly one `proves (Q)` conclusion, in that order. A Law has no `result`.

```cpp
law identity(unsigned x)
    proves (x + 0u == x);

law given_zero(unsigned x)
    expects (x == 0u)
    proves (x == 0u)
{
    assume h : x == 0u;
    exact h;
}
```

A semicolon requests automatic construction and checking of evidence. An
explicit body supplies proof steps through the same proof pipeline as `proof`.
Failure of either form MUST fail compilation; neither creates an axiom. A
`trusted law` is the separately explicit assumption form and MUST NOT have a
proof body. The concrete grammar is [GRAMMAR.md](./GRAMMAR.md#3-law-declaration).

---

## 10.2 Law semantics

For:

```cpp
law L(T x)
    expects (P(x))
    proves (Q(x));
```

the meaning is conceptually:

```text
∀ x : T,
    P(x) → Q(x)
```

With no `expects` clause:

```cpp
law L(T x)
    proves (Q(x));
```

means:

```text
∀ x : T,
    Q(x)
```

---

## 10.3 Laws are not axioms

Declaring:

```text
law L(...)
    proves (...);
```

does **not** make `L` true.

An ordinary Law begins unresolved.

It becomes `PROVEN` only when valid proof evidence exists.

An unresolved Law MUST NOT be used as if it were established.

---

## 10.4 Law application

A named Law may be instantiated with arguments inside a proposition or proof context.

Conceptually:

```text
L(x)
```

means the proposition represented by that particular Law instance.

A Law has no ordinary runtime callable identity.

---

## 10.5 Law changes

Changing a Law changes the formal specification of the program.

A compiler, verifier, tactic, or automated agent MUST NOT weaken a Law merely to make an implementation verify.

---

# 11. Function contracts

C++L supports compile-time contracts on runtime functions.

The core contract clauses are:

```text
expects
ensures
```

---

## 11.1 Preconditions

A precondition is written:

```cpp
expects (condition)
```

Example:

```cpp
int divide(int x, int y)
    expects (y != 0)
{
    return x / y;
}
```

The precondition states what a verified caller must establish before the call.

It is not automatically a runtime assertion.

---

## 11.2 Postconditions

A postcondition is written:

```cpp
ensures (condition)
```

Example:

```text
int abs_value(int x)
    ensures (result >= 0)
{
    ...
}
```

The postcondition describes the required state after normal return.

---

## 11.3 `result`

Inside a non-void function postcondition:

```text
result
```

denotes the function's returned value.

Example:

```cpp
int identity(int x)
    ensures (result == x)
{
    return x;
}
```

`result` is not globally reserved. Its return-value meaning is invalid in a Law, a void postcondition, a precondition or a refinement predicate; ordinary C++ names retain their ordinary meaning outside the special context.

It has special meaning only within a relevant postcondition.

---

## 11.4 `old`

Inside a postcondition:

```cpp
old(expression)
```

denotes the semantic value of `expression` in the function pre-state.

Example:

```cpp
void withdraw(Account& account, int amount)
    expects (amount >= 0)
    expects (amount <= account.balance)
    ensures (account.balance == old(account.balance) - amount)
{
    account.balance -= amount;
}
```

`old(expression)` is legal as a snapshot only in a function postcondition. Its expression is resolved in the function entry state, must be well-defined there, and cannot use `result` or nested `old`. Elsewhere `old` is an ordinary C++ name.

`old(expression)` is a formal snapshot.

It does not imply that a runtime copy must be created.

---

## 11.5 Clause cardinality, ordering and layout

Specification predicates and measures MUST be parenthesized. A construct has
at most one clause of each kind. Function clauses are `expects`, `ensures`,
`decreases`; Law clauses are `expects`, `proves`; loop clauses are `invariant`,
`decreases`, in those orders. Conjoined predicates belong in a single `&&`
expression. A measure list is lexicographic and MUST NOT be merged as conjunction.

Canonical presentation puts one space before each clause's opening parenthesis
and puts clauses on continuation lines. Refinement `where (P)` stays attached
to the declaration. Ordinary C++ prefix specifiers precede `verified pure`.
The shared formatter owns presentation; spelling/cardinality/order are defined
by the [normative grammar](./GRAMMAR.md).

### 11.5.1 Declaration contracts

A function entity has one logical contract. The public declaration carries it;
the matching definition inherits it and need not repeat `verified` or clauses.
Matching uses Clang-resolved entity identity, not spelling. Repeated contracts
MUST be identical under parameter renaming and semantic resolution; conflicting
contracts are errors. A visible declaration lets callers state entry obligations
and the promised post-state without body access. Use of a verified summary still
requires checked evidence for its implementation, or an explicit recorded trust
boundary; a declaration alone MUST NOT manufacture proof evidence.

Templates retain their contract and refinement metadata at instantiation sites.
Across translation units the same semantic metadata and evidence dependencies
must accompany the public interface; erased native symbols alone carry no proof.
A verified function with only an entry precondition still incurs all body safety
obligations. Omission of `ensures` does not waive those obligations.

---

## 11.6 Normal-return semantics

Unless a separate exceptional contract mechanism is explicitly defined, `ensures` applies to normal function return.

It does not by itself claim:

```text
the function terminates
the function never throws
```

These are separate properties.

---

## 11.7 Call-site obligations

Inside verified reasoning, a call to a function with:

```cpp
expects (P)
```

requires proof that `P` holds at the call site.

An ordinary unverified caller is not automatically rejected merely because it cannot statically prove the precondition.

This preserves incremental adoption.

---

## 11.8 Contracts are compile-time specifications

`expects` and `ensures` do not automatically generate runtime checks.

Failure to prove a required contract MUST NOT silently be transformed into:

```text
assert(...)
```

or equivalent runtime behavior.

Runtime validation is a distinct mechanism.

---

# 12. `verified`

`verified` requests formal verification of a declaration or definition.

Example:

```cpp
verified int identity(int x)
    ensures (result == x)
{
    return x;
}
```

---

## 12.1 Verified-function obligation

A function marked `verified` MUST discharge every verification obligation required by its declared verification scope.

These may include:

```text
precondition preservation
postconditions
refinement invariants
purity claims
defined behavior
lifetime requirements
termination where required
loop invariants
```

---

## 12.2 Failure semantics

If a required obligation for a `verified` declaration cannot be established, that declaration MUST NOT be accepted as verified.

The implementation MUST NOT silently downgrade:

```text
verification failure
```

into:

```text
PROVEN
```

---

## 12.3 Runtime representation

`verified` does not by itself change:

```text
function signature
calling convention
object layout
ABI
runtime value representation
```

---

## 12.4 Trusted dependencies

A verified result MAY logically depend on explicit trusted assumptions.

Such a result may still have a valid proof derivation from those assumptions.

The dependency on those assumptions MUST remain explicit.

The trust policy and reporting requirements are defined in `TRUST.md`.

---

## 12.5 Single-return verification fragment

For a supported definition of the form:

```cpp
verified T f(parameters)
    expects (P)
    ensures (Q)
{
    return expression;
}
```

let `R` be the typed return term elaborated from the actual function body
resolved by Clang. Its required obligation is:

```text
forall parameters. P -> Q[R/result]
```

When `expects` is absent, the obligation is `forall parameters. Q[R/result]`.
Several `expects` clauses `P1`, ..., `Pn` conjoin (section 11.5) and are
supposed in source order: `forall parameters. P1 -> ... -> Pn -> Q[R/result]`.
A verified call must establish each `Pi` separately (section 12.6). No
conjunction connective is required.
Substitution MUST avoid variable capture. `result` is a specification binding
of type `T`; it MUST NOT introduce a runtime variable, parameter, or computation.
The return expression MUST be checked even when `Q` does not mention `result`.

The body MUST NOT be replaced by an assumed summary or a `body_semantics` axiom.
Every generated obligation requires evidence accepted by the existing kernel.
This fragment adds zero kernel rules and zero logical assumptions.

The initial implementation accepts namespace-scope functions with an explicit
built-in integer return type, integer value parameters, one `ensures` clause,
and any number of `expects` clauses, each a comparison under section 12.7. A
Law accepts at most one `expects` clause. Bodies contain exactly one return of a
modeled pure expression: parameters, integer literals, unsigned addition,
subtraction and multiplication, or calls to admitted pure definitions or
verified functions under section 12.6. Unsigned `+`, `-` and `*` denote the
core's wrapping operations (section 7.1.1), which C++ defines them to be for
unsigned operands of one type (section 29.2). Clang remains authoritative for
overloads, integer widths, and conversions; unsupported conversions, signed
arithmetic, division, remainder, shifts and bitwise operators are rejected. Type
aliases are resolved by Clang.

Branches and multiple returns extend this fragment under section 12.7,
locals and assignments under section 12.8, and `while` and `for` loops under
section 24.3. Reference storage, call effects and void returns extend it under
section 12.9. Pointer dereferences, floating point, exceptions, recursion, and
unsupported declarators remain rejected.
An ordinary parameter named `result` is currently unsupported on a verified
definition because that name binds the returned value in its postcondition.

Verified calls compose contracts under section 12.6. Ordinary callers remain
permitted under section 11.7. `verified` alone does not make a function available
for unrestricted unfolding as a `pure` definition.

## 12.6 Compositional verified calls

For a call `g(t)` within a supported verified body, the compiler MUST resolve
the callee through Clang, instantiate its contract at the actual arguments,
and generate a separate obligation for its precondition, when present. The
caller may use its own precondition and postconditions of previously justified
calls. The current call's postcondition MUST NOT justify its own precondition.

After that obligation and the callee's contract have been kernel-proven, the
callee's postcondition is evidence about the call's result. Caller reasoning
uses a fresh logical result and this postcondition, without inspecting the
callee's implementation. Even a call whose result the caller's postcondition
ignores MUST discharge its precondition. Calls without preconditions still
require a proven callee contract before their postconditions become available.

The compiler MUST connect this abstract reasoning to the actual lowered body
using kernel-checked evidence. Existing universal elimination, implication
elimination, and equality substitution suffice. Conditional postconditions are
not axioms; there are zero additional kernel rules or logical assumptions.

Nested calls are processed from arguments to enclosing calls. Since supported
expressions are pure, this logical dependency order introduces no runtime
evaluation-order claim. Erasure leaves every runtime call and argument unchanged
and adds no runtime checks, variables, parameters, or wrappers.

The initial composition fragment requires definitions in the same translation
unit, including included headers, and an acyclic verified-call dependency graph.
Recursion, declaration-only contracts, calls hidden behind unsupported pure
dependencies, and calls with neither an admitted pure definition nor a verified
contract fail closed. Specification
expressions retain the existing pure-definition model. Equality and unsigned
addition are the original composition fragment; comparisons extend it under
section 12.7, and unsigned subtraction and multiplication under section 7.1.1.
A precondition that follows from the caller's facts only by order reasoning is
established under section 7.5.

## 12.7 Path-sensitive verification

Verified bodies MAY contain ordinary `if`/`else`, nested blocks, and returns.
Clang MUST resolve each condition and operand type. Every modeled path MUST end
in a return; an omitted `else` continues with the following statements. Locals
and assignments are specified in section 12.8, and loops, with their `break`
and `continue`, in section 24.3. Switches, other jumps, exceptions, side
effects outside section 12.9, implicit type conversions, and trailing unreachable
statements remain unsupported.

For each return term R, the compiler MUST generate and kernel-prove:

```text
forall parameters. expects -> condition_1 -> ... -> condition_n -> Q[R/result]
```

The true arm supposes its condition; the false arm supposes its negation.
No path condition is an axiom. Each call precondition MUST be established using
only conditions encountered before the call and previously justified call
summaries on that path. In particular, a call within a condition cannot use
that condition to justify itself. All paths require proof, even when their
conditions appear contradictory: such a path is proven from the contradiction
itself under section 7.5, never skipped as unreachable. A contract is available
to callers only after every path and required call precondition is proven and
their evidence is linked to the complete body.

A condition is not a value position. `&&`, `||` and `!` state propositions, and
a proposition is not a value: the core computes no boolean from one. In a
verified condition they are therefore elaborated into the routes they select
between, rather than lowered as values:

```text
if (A && B) T else F   ==>   if (A) { if (B) T else F } else F
if (A || B) T else F   ==>   if (A) T else { if (B) T else F }
if (!A)     T else F   ==>   if (A) F else T
```

Elaboration recurses, so the connectives nest to any depth. This models C++
short-circuit evaluation exactly rather than approximating it: an operand
appears only on the routes where C++ evaluates it, so no route can suppose a
fact about an operand that did not execute on it. In particular the route where
`A && B` fails is the union of `!A` and `A && !B`, represented as those routes,
and MUST NOT be represented as one route supposing both operands false.
Symmetrically, the route where `A || B` holds is a union and establishes neither
side on its own. Outside a condition, `&&` and `||` remain refused as values.

Contracts, Laws, and conditions support built-in integer `==`, `!=`, `<`, `<=`,
`>`, `>=`, and logical negation of these predicates. Operands MUST have the same
Clang-resolved modeled integer type. Identical path predicates can close goals;
existing equality rewriting remains available. Concrete integer comparisons
compute with their stated signedness and width. Comparisons are normalized under
section 7.1.1, and order consequences of path conditions, preconditions and
summaries are established under section 7.5. Signed arithmetic is not inferred.

The core represents comparisons as total boolean computations, with boolean
values encoded as unsigned one-bit integers. A declared `bool` parameter or
result is modeled as that same one-bit unsigned integer, because C++ `bool` has
exactly its two values, so a condition MAY be a `bool` value itself. Integral
promotion of `bool` and `bool` literals are not modeled and are rejected as the
conversions they are. Positive equality retains ordinary
propositional equality; negative equality denotes the equality comparison
evaluating to zero. Negation reverses the required comparison outcome.

One conditional-elimination kernel rule combines checked true and false cases
into the postcondition of a typed conditional term. The kernel independently
checks both branch premises, the proposition context, and its substitution at
the actual condition and return terms. This adds zero logical assumptions.
Erasure MUST preserve each runtime condition, call, return, and control-flow
edge, and MUST insert zero runtime checks. Pure specification helpers retain
their single-return fragment.

## 12.8 Locals and assignments

A verified body MAY declare local variables and assign to them.

A declaration MUST have automatic storage, a modeled type, and an initializer
that is a single modeled expression: `T x = e;`, `T x{e};`, `T x(e);`, and the
same forms with `auto` or `const`. An uninitialized local, a `static`,
`extern`, `register` or `thread_local` declaration, a declaration of a
pointer, `volatile` or otherwise unmodeled type, an aggregate or
empty braced initializer, and a non-variable declaration are rejected. An
assignment MUST name a local of the same body, with a value of the same modeled
type. Reference and parameter writes follow section 12.9. Pointer writes and writes to
unmodeled storage are rejected. C++ decides whether a `const` local may
be assigned; C++L adds no `const` model of its own. Conversions in an
initializer or an assigned value are rejected exactly as elsewhere: a
difference in qualification alone is not a conversion, because the value read
is the same.

As a statement, `x += e`, `x -= e` and `x *= e` denote the assignment
`x = x op e`, and `++x`, `x++`, `--x` and `x--` denote `x = x + 1` and
`x = x - 1`, with `1` of the local's type. C++ gives them exactly that meaning
when the local's type is not promoted before arithmetic, so a local narrower
than `int` is rejected, as is an operand of another type. The arithmetic is
then modeled or rejected like any other (§7.1.1, §29.2): signed updates are
rejected until their overflow obligations exist. Other compound assignments,
and an update used as a value rather than as a statement, are rejected.

Each write gives the local its next logical **version**. A version belongs to
the declaration Clang resolved, not to a spelling, so shadowing and nested
scopes follow C++ name lookup and never C++L's own. A read denotes the version
current where the read stands, and the value a version denotes is the modeled
expression that established it. Outside a loop a version is never an unknown:
nothing is assumed about a local. The one exception is a loop head (§24.3),
where a local the loop writes denotes a value of which only the loop's
invariants and condition are known. A value MUST be modeled where the version is
established, whether or not any later expression reads it, because C++
evaluates it there: an unread signed overflow is still undefined behavior.
C++ scoping forbids a read before the declaration but
puts a local in scope within its own initializer; a read there, or anywhere
else no version of the local is current, MUST be rejected.

What follows a branch is verified once per arm, under the versions that arm
established, so a local's value after a branch is path-sensitive by
construction. This requires no merge operation and no additional kernel rule.

A local bound to a conditional expression is path-sensitive in the same way. A
conditional states one `select` term, of which neither arm's facts are known, so
the route splits on its condition exactly as it does for a conditional in tail
position: the local denotes the arm the route takes, and owes any refinement
predicate under what that route supposes (§17.2).

Which conditional a route splits on is decided by what the bound value
**denotes**, not by how it is written. A read denotes the value its version was
given, so resolution follows reads transitively, to any depth, and a conditional
reached through intervening locals splits exactly as a directly written one
does. Resolution is bounded without a fixed hop limit: a version's value reads
only versions established before it, so following reads strictly decreases the
version and terminates. Resolution never crosses a version boundary — it takes
the version current at the read, so a version established by a later write is
never confused with the one before it — and a read whose version the route does
not establish, such as a loop head version or a parameter, resolves to itself
and stays opaque.

A route's conditions correspond to the `select` nesting of the body's lowered
value, because that nesting is what the proof is composed over: each `select` is
discharged by conditional elimination (§12.7), and a leaf is proven under
exactly the conditions standing above it there. Splitting MUST keep the two in
step. Where it cannot, the body is refused rather than proven.

A call in an initializer or an assigned value is evaluated where the body
evaluates it. Its precondition MUST be proven using only the path conditions
and summaries established **before** that statement, and its postcondition
becomes available only from that statement onwards. A call bound to a local
MUST be proven on every path that reaches its statement, including paths that
never read the local.

Every return MUST discharge the postcondition from the versions visible on its
path; as in section 12.7, no path is exempted as unreachable. Verification
models locals this way; the runtime program is not rewritten. Erasure MUST
preserve every declaration, initializer, assignment, call, branch, and return
as written.

A read denotes its version's whole value, so the stated terms can grow faster
than the body. An implementation MAY bound the statements on one path and the
size of the terms it states, and MUST reject a body beyond those bounds rather
than approximate it.

## 12.9 Reference storage and normal post-state

Verified scalar parameters MAY be passed by value, `T&`, `const T&`, or `T&&`.
Clang resolves binding, reference collapsing, qualification and access legality.
A reference denotes existing storage; it introduces no independent object or
refinement fact. A local reference MAY also bind a modeled parameter's storage.
Writes through it MUST use the same crossing and version mechanism as direct
writes. By-value parameters have independent local storage; their contract
parameter still denotes the caller's input value, as in section 12.5.

A mutable reference parameter MAY alias any other reference parameter with the
same modeled value type, including a const reference. No distinctness is inferred
from parameter position. An exact write creates a new value version for its
referent and fresh unconstrained versions for other possible referents. The
written value MUST satisfy every refinement of storage it may target. A fact
about an earlier version MUST NOT constrain a fresh version. Refinement spelling
alone MUST NOT re-establish membership after invalidation.

In `ensures`, a reference parameter denotes its value on normal return. Each
return path supplies its current reference values to the postcondition; a refined
reference parameter also owes its declared predicate at that boundary. Entry
preconditions and copied local snapshots continue to name entry values.

A verified call MUST prove its preconditions before introducing its result or
post-state. A potentially mutating call replaces the actual reference arguments'
versions, invalidates other possible aliases, and states the proven callee's
postcondition over the new versions. Repeated actual arguments share one new
version. Every refined actual storage owes membership on its new value, even
when the formal parameter is an unrefined reference. Such a requirement may use
the callee's proven postcondition; it MUST depend on successful verification of
that callee. No call summary is an axiom. Calls through references currently
require resolved tracked actual storage at an effectful boundary.

`verified void` functions MUST verify an explicit postcondition on every normal
exit, including `return;`, a returned void call, and fallthrough. There is no
source `result` binding for void. Void aliases are resolved by Clang. A private
logical completion token is used only to reuse contract bookkeeping; erasure
introduces no runtime return value, object or check.

Reference mutation and calls participate in the ordinary branch and loop rules.
Each possible mutation is included in a loop's carried state; fresh loop-head
values receive only invariant facts. Calls in a standalone statement, an
initializer, an assignment's right operand, or a return are sequenced before the
continuation. Nested effectful expressions whose ordering is not represented are
rejected. Unsupported lifetime or exceptional-state behavior remains rejected.
These stateful contracts use the partial-correctness obligation path, including
for loop-free bodies; they do not introduce total core definitions.

Dereferencing a pointer is rejected, in every form: `*p` as a read, `*p = e` as
a write, `p->m`, and `p[i]`. This is not a representational limitation. A valid
dereference requires liveness, initialization for reads, sufficient bounds,
provenance and access permission, and `p != nullptr` establishes none of them: it
is necessary and insufficient. A pointer's state model states `null` and
`non_null` and MUST NOT supply the difference (section 20.5). Dereference
therefore requires separate storage and capability obligations, defined in
section 12.10 and specified by RFC 0014. Until that model is implemented for a
given access form, no dereference is modeled, and no implementation may admit one
on the strength of a non-null precondition. Pointer values, their comparisons,
and proof-side case analysis over `null` and `non_null` are unaffected.

Pure conditional expressions use Clang's resolved result type and the existing
conditional term and branch rules. Boolean literals denote the two Boolean
values. These additions do not model numeric promotions or signed overflow.

---

## 12.10 Storage and access

This section is the normative boundary for the storage model specified by RFC 0014. It is generic: refinement types consume it and MUST NOT define it.

A **place** designates storage. A place is a logical construct; it is never an
address and never a runtime value. Places are a local's storage, a by-reference
parameter's referent, a data member of a place, an element of an array-like
place, the pointee a pointer value designates, or a materialized temporary. A
member and an element are projections naming storage within a place; a pointee
place is the only form whose construction requires a capability.

Reading a place yields a value; a place itself MUST NOT be a term the proof
kernel receives. Places are identified structurally after Clang resolution, so
two distinct data members of one complete object are distinct places.

A **region** is the object a place belongs to, and carries extent, liveness and
provenance. A member or element shares the region of the place it projects from.

A **capability** is what the current state permits at a place: `readable`,
`writable` or `initialized`. `initialized` entails `readable`. `writable` does
NOT entail `readable`. Neither `p != nullptr` nor a pointer's decomposition
state entails any capability, and no capability entails `p != nullptr`. A read
requires `initialized`; a write requires `writable`.

Two places MAY alias unless disjointness is proved. Distinct locals are
disjoint, distinct members of one object are disjoint, and distinct proved
indices into one array are disjoint. Any two pointee places MAY alias.
Type-based disjointness MUST NOT be used, because it depends on
undefined-behavior freedom the program has not been shown to have.

A write to a place MUST prove the place writable, MUST prove the written value
satisfies every refinement of that storage before the write is bound, MUST
establish a new version of the place, and MUST give every place that may alias
it a fresh unconstrained version. A fresh version of refined storage owes its
predicate again; refinement spelling alone MUST NOT re-establish membership.

A call MAY change storage. A by-value parameter has no effect on caller storage,
and neither does a parameter of const reference or const pointer type. An
unverified callee MUST NOT be assumed pure: it may write every region reachable
through its non-const reference and pointer parameters and every region whose
address may have escaped. A verified callee has exactly its stated effects, and
only after its contract and the call's entry obligations are proven. A fact
invalidated by an effect is re-established only by a proven postcondition.

An access whose capability cannot be established MUST be rejected. Where a
capability originates outside the verified world, an explicit `trusted` boundary
MAY introduce it as a recorded trust event, which MUST name the capability, the
place, the source location and the mechanism in the trust report. A failed
capability obligation MUST NOT be silently downgraded to an assumption.

Storage, regions, capabilities and versions are proof-only and erase completely.
They introduce no runtime check, tag, metadata, wrapper or layout change.

---

# 13. `pure`

`pure` declares that a function is referentially transparent for the formal semantics in which it is used.

Example:

```cpp
pure int square(int x) {
    return x * x;
}
```

---

## 13.1 Purity requirements

A pure function MUST NOT perform observable side effects.

In particular, unless explicitly modeled as immutable formal input, a pure function MUST NOT:

```text
write externally observable state
perform I/O
mutate globals
perform volatile access
perform observable atomic mutation
depend on hidden mutable state
call an impure function
```

---

## 13.2 Reading memory

A pure function MAY read data reachable from its explicit inputs only when those reads are formally stable for the duration and meaning of the call.

A raw pointer value alone does not prove referential transparency.

---

## 13.3 Purity is checked

`pure` is not merely documentation.

A definition marked `pure` MUST satisfy the purity rules before its purity may be relied upon by formal reasoning.

An external declaration whose purity cannot be checked MUST use an explicit trusted specification if purity is to be assumed.

---

## 13.4 Purity does not imply termination

A pure function may still diverge.

Therefore:

```text
pure
```

does not by itself mean:

```text
total
```

If the function participates in proof normalization or other logic requiring totality, termination MUST also be established.

---

# 14. Specification expressions

Expressions used in:

```text
law
expects
ensures
where
invariant
decreases
proof propositions
```

are specification expressions.

---

## 14.1 Side effects

Specification expressions MUST be side-effect-free.

---

## 14.2 Defined behavior

A specification expression MUST itself have defined semantics.

A property cannot be established by evaluating undefined behavior.

---

## 14.3 Calls

A specification expression may call only functions whose formal behavior is sufficiently known for the proposition being expressed.

A function used as a mathematical function in specifications MUST satisfy the required purity and termination properties.

---

# 15. Proof declarations

A `proof` declaration provides evidence for a proposition.

Basic form:

```text
proof name(parameters...)
    proves (proposition)
{
    proof_body
}
```

Example:

```cpp
proof identity_reflexive(int x)
    proves (Eq<int>(x, x))
{
    refl;
}
```

---

## 15.1 Proof parameters

Proof parameters are universally quantified.

Conceptually:

```cpp
proof P(T x)
    proves (Q(x))
```

means construction of:

```text
∀ x : T, Proof<Q(x)>
```

---

## 15.2 `proves`

The clause:

```cpp
proves (P)
```

declares the proposition that the proof body must establish.

---

## 15.3 Proof-body semantics

A proof body exists only to construct formal evidence.

Proof-body operations have no ordinary runtime effects.

The semantics of a proof are determined by the proof term it elaborates to, not by tactic implementation details.

---

## 15.4 Proof automation

An implementation MAY provide tactics, simplifiers, theorem search, decision procedures, or other proof automation.

Such automation does not change the proposition being proved.

A failed search is not proof of falsehood.

Successful search is meaningful only if it produces valid proof evidence according to the formal rules.

---

# 16. Reflexivity

C++L provides a primitive reflexivity proof equivalent to:

```text
refl : Proof<Eq<T>(x, x)>
```

Surface proof syntax may use:

```cpp
refl;
```

Reflexivity MUST NOT establish:

```text
Eq<T>(a, b)
```

unless `a` and `b` are definitionally equal.

---

# 17. Refinement types

A refinement type restricts values of an underlying type with a proposition.

Basic form:

```cpp
type Percentage = int where (self >= 0 && self <= 100);
```

Conceptually:

```text
Percentage =
{ x : int | 0 <= x <= 100 }
```

---

## 17.1 `self`

Inside a refinement predicate:

```text
self
```

denotes the candidate value.

`self` is contextual and has no special meaning outside that refinement predicate.

---

## 17.2 Construction

A value MUST NOT enter a refinement type through an unchecked conversion from its base type.

Construction requires one of:

```text
static proof of the predicate
successful runtime validation
explicit trusted boundary
```

---

## 17.3 Elimination

A value of refinement type may be used as its base value while retaining the refinement proposition as known evidence within verified reasoning.

### 17.3.1 Current refinement fragment

The implementation accepts refinement declarations at namespace scope, over modeled
built-in integer and Boolean base types, with or without indices. The base type and
the predicate are resolved by Clang; `self` is an ordinary parameter of the base
type, so it is a name Clang binds rather than one C++L invents.

Membership is an obligation, never an assumption. Every flow of a value into a
refinement type inside a verified function states its predicate where the value
enters, under whatever the path supposes there, so a branch fact can discharge it.
The flows modeled are a local declaration, an assignment or update to a local, an
argument of a call to a verified function, and a return. A refined parameter's
predicate is supposed inside the body, and the author does not restate it as an
`expects` clause. A refined result is stated with the postcondition and proven on
every path that returns. Using a refined value as its base value requires nothing
further.

A verified definition with a refined result MAY omit `ensures`; its effective
postcondition is then the result's full refinement predicate. Explicit `ensures`
clauses conjoin with that predicate. A definition with neither a refined result
nor an explicit postcondition is rejected. An ordinary or merely `pure` function
return declaration cannot establish refinement evidence and is rejected unless
the same Clang-resolved callable has a verified definition in the translation
unit. Declaration-only verified contracts and trusted/unsafe refinement-return
boundaries remain unavailable; their spellings do not establish evidence.

A function containing a loop, or calling a function verified by loop conditions,
MUST enforce the same refinement crossings as a function without loops. Every
local initialization and write is checked, including a value never subsequently
read. A loop head knows only the invariant and path facts about its fresh logical
versions; a declared refinement is not an additional unchecked loop invariant.
Unresolved refinement identity or index substitution MUST reject obligation
generation rather than omit a predicate.

A refinement whose base type is another refinement states both predicates: the one
written and every one it inherits (17.5). An indexed refinement states its
predicate at the values its indices were applied at.

Ordinary `using` and `typedef` aliases preserve refinement metadata, including
constant indices. Refinements are identified by the Clang-resolved alias
declaration, not by an unqualified name or a presumed source location. An
unrelated ordinary alias with the same spelling introduces no predicate.

A reference local that binds a tracked local object is an alias of that object's
storage, not a value of its own. A read through it denotes the storage's current
logical version, and a write through it is a write to that storage: it owes the
predicates of the reference's own refinement and of the referent's declared type
together. A refinement fact therefore cannot outlive a write through any alias of
the storage it describes, because there is no separate fact to go stale. Binding
itself is a crossing and states the reference's predicate at the referent's
current version. Modeled parameters also have tracked storage (12.9). A reference
to an unmodeled temporary, subobject, or reference-returning call is refused.

Not yet modeled, and refused rather than approximated: refined returns of an
unverified function, refined members, pointer dereferences and pointee mutation,
and refinements in templated contexts. Scalar reference parameters, void returns,
and their call effects are modeled under 12.9. A refinement over a base type outside the
modeled fragment is refused where it is declared.

Explicit refined storage outside a modeled verified body, including fields and
namespace-scope arrays, is rejected because its construction and mutation have
no generated obligations. This is a verification limitation, not a change to the
ordinary C++ representation or layout of the alias.

A verified body tracks an aggregate local as one place per data member (section
12.10) and checks the value every construction and write puts there. That covers
the body-side paths; it is necessary and not sufficient, because ordinary code
constructs records without generating any obligation. The declared refinement of
a member therefore remains refused until the unverified construction boundary is
checked as well.

A refined member is sound only if every way of establishing or changing that
member is checked against its refinement predicate:

```text
aggregate initialization
default/value initialization
constructor member initialization
copy construction
move construction
copy assignment
move assignment
direct member assignment
compound member update
mutation through aliases/references/pointers
unverified construction boundaries
```

Supplying a component's predicate on a member _read_ is not sufficient and MUST
NOT be implemented before those obligations exist. A record enters a verified
body as a parameter, so its construction happens in unverified code: admitting a
refined field and stating its predicate on read would let an ordinary
`S{-5}` establish `self > 0`, which no rule of this specification proves. The
required order is construction and write obligations first, then member
projection and membership reasoning.

---

### 17.3.2 Refinement implication

A value already of a refinement type carries that type's predicate into any further
flow, so crossing between two refinements of one base type is the implication
between their predicates and nothing else:

```text
{ self : T | P(self) }  <:  { self : T | Q(self) }    needs  forall self : T, P(self) -> Q(self)
```

The obligation this states is `P(v) -> Q(v)` at the value that crosses, under the
path conditions where it crosses. The looser direction is therefore discharged from
the predicate the value already has, and the stricter direction owes the part that
does not follow. Nothing is checked at run time in either direction: the crossing
has no runtime representation to check, because both types erase to `T` (17.4).

Because both directions are decided by implication, two refinements that erase to
one C++ type are also one C++ signature. Two overloads distinguished only by which
refinement they name are the same function, and that is reported at the declaration
the author wrote.

---

## 17.4 Runtime representation

Unless explicitly specified otherwise, a refinement type has the runtime representation of its base type.

Its refinement proof is erased.

A refinement declaration therefore lowers to the alias it means, and that alias is
what the program keeps:

```text
type R = T where (P);        ->  using R = T;
type R(I i) = T where (P);   ->  template <I i> using R = T;
```

The lowering MUST be deterministic and derived from the declaration alone. It MUST
NOT introduce a wrapper type, a constructor, a hidden field, a runtime predicate, a
runtime check, an RTTI distinction, ABI-visible state, or a different object
layout. Verification-level identity is separate from this representation: two
refinements of one base type erase to the same C++ type and remain distinct
refinement types (`TRUST.md` 10.1).

For example:

```text
Percentage
```

may have the same runtime representation as:

```text
int
```

while carrying additional compile-time proof information.

---

## 17.5 Refinement composition

A refinement whose base type is another refinement states the conjunction of the
applicable predicates. The inner predicate MUST NOT be discarded:

```cpp
type NonNegative = int where (self >= 0);
type Percentage = NonNegative where (self <= 100);
```

A value entering `Percentage` owes `self >= 0 && self <= 100`. Erasure still
reaches the ultimate ordinary C++ base representation, `int`.

---

# 18. Dependent types

C++L supports types whose formal meaning depends on values.

For example:

```cpp
type Index(std::size_t n) =
    std::size_t where (self < n);
```

`Index(n)` is a family of types indexed by `n`.

---

## 18.1 Dependent value stability

A value used as a type index MUST have sufficiently stable formal meaning.

Mutable arbitrary runtime state cannot silently become a compile-time type index without an explicit formal boundary.

---

## 18.2 Dependent function meaning

A result type or proposition MAY depend on function parameters.

Conceptually:

```text
Π (x : A), B(x)
```

describes a dependent function type.

---

## 18.3 Proof-only indices

An index that exists solely for proof purposes MUST be erasable if it has no runtime role.

Proof-only indices MUST NOT change runtime ABI merely by existing in the formal type.

---

# 19. Reasoning over C++ types

C++L reasons directly over C++ types.

The types a verified program uses are the types its C++ source declares, as resolved by Clang:

```text
struct
class
enum
std::variant and other library types
pointers
references
arrays
integers
templates
functions
```

C++L does not introduce general-purpose algebraic data types or runtime pattern matching.

A program MUST NOT be required to restate a C++ type in a second, logical type language before properties of its values can be proven.

C++L MAY provide proof-only mathematical domains (§19.1) and proof constructs such as exhaustive case analysis (§20) and induction (§21). All such constructs are erased and have no runtime representation.

The rationale is recorded in `docs/rfcs/0005-cxx-types-case-analysis-induction.md`.

---

## 19.1 Proof-only mathematical domains

Specifications and proofs MAY use these mathematical domains:

```text
@N           natural numbers: 0, 1, 2, ...
@Z           mathematical integers: ..., -1, 0, 1, ...
@Seq<T>      finite sequences of T
@Set<T>      sets of T
@Map<K, V>   finite maps from K to V
```

The set is closed. `@` does not introduce a general identifier namespace. Any other `@` spelling MUST be rejected unless a later specification adds user-defined mathematical domains.

`T`, `K` and `V` are verification types. A verification type is either a C++ type or another mathematical domain, as in `@Seq<int>` or `@Map<@N, @Z>`.

A mathematical domain MUST be accepted only where a verification type is expected:

```text
Law and proof parameters
quantifier binders
ghost declarations
arguments of another mathematical domain
```

Mathematical domains are verification-only. They have no object representation, storage, ABI, lifetime, address, `sizeof`, alignment, constructor, or destructor. Uses such as:

```cpp
@Z runtime_value;
sizeof(@Z);
new @Seq<int>();
```

MUST be rejected.

Each spelling `@N`, `@Z`, `@Seq`, `@Set` and `@Map` is a single token written without internal whitespace (§3.2). Objective-C++ also uses `@`-prefixed constructs. Objective-C++ compatibility lies outside the core C++L grammar and may require a separate frontend mode.

A C++ value is never silently a mathematical value. `int` is a machine integer; `@Z` is not (§29.1). Relating a C++ value to a mathematical one requires an explicit, defined mapping whose side conditions are proof obligations. The spelling of that mapping is not yet specified.

Explanatory material may also write ℕ, ℤ, Seq⟨T⟩, Set⟨T⟩ and Map⟨K,V⟩ as metanotation for the same domains.

---

## 19.2 Abstract models

A specification MAY relate a C++ object to an abstract mathematical value, such as a `std::vector<int>` to an `@Seq<int>`.

The function relating them is proof-only.

What that function states about a C++ type MUST be established by proof or declared as an explicit trusted assumption (§27). It MUST NOT be inferred.

---

# 20. Case analysis

`cases` splits a proof obligation into one obligation for every case of a value.

Example:

```text
enum class State { idle, running, failed };

proof foo(State s)
    proves (...)
{
    cases s {
        State::idle => {
            ...
        }

        State::running => {
            ...
        }

        State::failed => {
            ...
        }

        unnamed(value) => {
            ...
        }
    }
}
```

`cases` is a proof statement (§15). It is not runtime control flow and generates no runtime code.

---

## 20.1 Case sets

The cases of a value are determined by its C++ type under the selected C++ semantics.

They cover the type's complete semantic state space. This includes states that have no ordinary named alternative, which are called residual cases:

```text
enumeration      each enumerator
                 + unnamed(value), if its values exceed its enumerators
std::variant     each alternative(value)
                 + valueless
std::optional    engaged(value)
                 + empty
pointer          null
                 + nonnull(p)
```

Enumerator labels are written qualified, as in `State::idle`. Enumerators with the same value name the same case.

Residual labels are defined by the verifier. They have meaning only as arm labels of the corresponding construct and are not reserved identifiers. Because enumerator labels are qualified, an enumerator named `unnamed` cannot collide with the residual label.

`unnamed(value)` binds the underlying integer value, which equals the value of no enumerator.

`nonnull(p)` establishes only that `p` is not null. It establishes nothing about the lifetime of the object `p` points to (§32).

Case analysis over a type whose cases the verifier does not model MUST be rejected.

---

## 20.2 Exhaustiveness

Case analysis MUST be exhaustive over the type's complete state space, not merely over its declared names.

Every case MUST either have an arm or be proven impossible from the proof context (§20.4). Otherwise the proof is rejected.

There is no wildcard arm. A catch-all label such as `_` MUST be rejected. A proof written before an enumerator was added therefore stops checking when it is added, instead of silently covering it.

For example, a Law can rule out the residual case of a variant:

```text
using PaymentResult = std::variant<Receipt, Error>;

law settles(PaymentResult r)
    expects (!r.valueless_by_exception())
    proves (...);

proof settles_holds(PaymentResult r)
    proves (settles(r))
{
    assume intact : !r.valueless_by_exception();

    cases r {
        Receipt(receipt) => {
            ...
        }

        Error(error) => {
            ...
        }
    }
}
```

The `valueless` case has no arm because `intact` proves it impossible.

---

## 20.3 Arm binders and hypotheses

An arm label MAY be followed by binders that name the structural components of its case, such as the value a variant alternative holds.

Binders name values only, never proof evidence. The number and meaning of an arm's binders are defined by its case.

Within an arm, the fact that the value belongs to that arm's case is a premise. `assume` MAY name it. The kernel MUST reject an `assume` whose proposition does not exactly match a premise the arm received.

Binders and premises are available only inside their arm and MUST NOT escape it.

Every arm must establish the enclosing goal.

---

## 20.4 Impossible cases

A case may be closed by evidence that it cannot occur.

Impossibility MUST be established formally rather than guessed from control-flow heuristics.

---

## 20.5 Proof decomposition

`cases` is representation-independent. What states a value has is supplied by a
**decomposition provider** for its resolved C++ representation; everything else
is the **generic case engine** and is shared by every representation.

```text
representation provider
    knows the sound logical state model of one C++ representation family

generic case engine
    performs arm matching, binder handling, exhaustiveness validation,
    proof-state splitting, evidence construction, dependency checking,
    diagnostics, erasure and kernel lowering
```

**Supporting a new C++ representation for proof-side case reasoning requires a
sound decomposition provider for that representation. Arm parsing, binder
handling, exhaustiveness validation, proof-state splitting, evidence
construction, dependency checking, diagnostics, erasure and kernel lowering are
representation-independent and MUST NOT be reimplemented per type family.**

**A representation provider models ordinary C++ states for verification
purposes. It does not introduce a new C++L runtime type, runtime pattern
matching, runtime destructuring, or runtime control flow.**

### 20.5.1 Decompositions

A provider answers for a resolved type with one of:

```text
unsupported            no provider models this representation
SumDecomposition       alternative states
ProductDecomposition   constituent components
```

Provider selection is by Clang-resolved semantic identity, never by spelling, so
aliases, qualified names and template specializations that resolve to one
declaration select one provider.

A `SumDecomposition` lists its cases in a fixed order. Each case carries a
**discriminator**: an ordinary modeled Boolean condition on the subject that
holds in exactly that case. A case may also carry bindings. Exhaustiveness is
one of:

```text
ResidualRequired   the named discriminators do not cover the state space, so a
                   named residual case completes the partition and MUST be
                   written
Complete           the named discriminators are jointly exhaustive, so the last
                   case is the negation of the others
```

A provider MUST NOT supply the residual discriminator: it is derived as "no
named discriminator holds". No implicit wildcard exists, and no wildcard arm is
admitted (§20.3). A representation that gains a state therefore makes a proof
that wrote no arm for it non-exhaustive, and the verifier reports the missing
case rather than absorbing it.

### 20.5.2 Arms

A case label is either a name the representation **reserves** for a state with
no C++ expression, or a **qualified** C++ expression that Clang resolves and the
provider maps to a case. An unqualified label that is not reserved is refused,
which keeps a reserved label distinct from an enumerator of the same spelling.

An arm names exactly as many binders as its case supplies bindings. A binding
denotes a value the C++ object model already provides; it creates no object,
copy, conversion or temporary. Binder names must not duplicate an enclosing
value parameter or binder. Binders and names bound inside an arm stay local to
it.

Arms nest and may use `refl`, `assume`, `exact`, `apply`, and `rewrite`. Proof
dependencies inside arms are checked exactly like top-level steps, so recursion
cannot hide in an arm. `assume` may name a fact the case supplies; it may not
introduce a new one. At most 64 arms and 32 nested case statements are
recognized, subject also to the existing proof-resource limits.

The subject must denote one stable value for the whole statement. Any proof
expression that denotes such a value is admitted, including a parameter of
reference type; the subject is projected once, so an expression that would be
evaluated more than once, or that would require an invented temporary, is
refused with a diagnostic rather than stabilized silently.

Case-derived facts are flow-sensitive in general. `cases` and `decompose` are
available only in proof bodies, which contain proof statements alone: no
assignment, call, construction or destruction can appear in one. No case fact
can therefore go stale within the statement that derives it. A fact cannot
escape one either: a subject that another object can write has reference type,
and a reference type has no formal meaning, so no law or contract can state a
proposition about it. Mutation and aliasing are thus excluded structurally
rather than by analysis.

Where a future revision admits `cases` over values that can change, its facts
MUST participate in the same mutation and alias invalidation framework as every
other proof fact — a provider MUST NOT be given an invalidation mechanism of its
own.

### 20.5.3 Evidence

The engine splits the enclosing goal on the discriminators in order, using the
existing conditional-elimination rule. Each arm proves the original goal under
its case's fact. The remaining branch, in which every discriminator is known
false, receives their left-associated conjunction, built with conjunction
introduction from the facts the branch already carries.

Case splitting therefore produces compositions of the existing
conditional-elimination, implication, conjunction and equality rules. There is
no case rule, no per-representation kernel rule, no axiom, no runtime check and
no runtime representation. Exhaustiveness is a property of evidence the kernel
rechecks: a provider that described the wrong partition can only fail to produce
a proof, never forge one.

### Abstract values and typed observations

The core additionally admits nominal abstract value sorts `V(identity; T0, ...,
Tn)` and total logical projections `project<i>(v) : Ti`. A projection is
well-typed only if its subject has exactly its declared domain and signature
and its index belongs to that signature. Signatures are finite, acyclic and
resource-bounded. They are part of semantic identity and obligation hashes.
Abstract values are not integers and admit no machine arithmetic.

Projection normalization only normalizes its subject. No state, payload value,
constructor identity, injectivity, surjectivity, or product extensionality is
assumed. Equality and substitution use the existing rules. Providers supply the
correspondence between C++ observations and these logical functions; a logical
projection never performs a runtime operation. A payload observation may be
exposed to source only in the state where the C++ payload exists.

### 20.5.4 Implemented providers

| Representation                                                  | Model                                         | Residual state                |
| --------------------------------------------------------------- | --------------------------------------------- | ----------------------------- |
| scoped enumeration                                              | one case per distinct enumerator value        | `unnamed`                     |
| `std::variant`                                                  | `alternative<i>(value)` per alternative index | `valueless`                   |
| `std::optional`                                                 | `some(value)`                                 | `none`                        |
| `std::expected`                                                 | `value(payload)`                              | `error(reason)`               |
| pointer                                                         | `null`                                        | `non_null`, binding nothing   |
| record, `std::pair`, `std::tuple`, `std::array`, built-in array | one `components(...)` arm                     | none; a product has one state |

A representation is recognized by its Clang-resolved canonical identity after
substitution, never by spelling. A standard type is identified through its
specialized template declaration in the canonical `std` namespace, skipping
inline namespaces, so a user type spelled like a standard one is not that type,
and a standard type reached through an alias, a template parameter or a
dependent name is.

Alternatives are identified by index, so repeated and aliased alternative types
are distinct states. `valueless` is a state of every variant and MUST NOT be
omitted. A pointer's `non_null` state binds nothing: it does not state that a
live, initialized or in-bounds object exists, and MUST NOT be read as stating
anything about lifetime, provenance, dereferenceability, bounds, ownership,
uniqueness or dynamic type. A standard type is modeled by its public semantics
only; no implementation's layout is read.

A product has exactly one state and so is not a case analysis. It is written
with `decompose` (§20.5.1) and generates no discriminator. Each binding is a
logical projection onto the existing subobject: no structured binding, copy,
move, conversion or temporary is created. Component order, types, access and
array extents come from Clang, and a component Clang reports as inaccessible is
refused by name.

`std::expected` requires the C++23 library. Where it is unavailable the provider
is simply not exercised.

A scoped enumeration (`enum class`, `enum struct`) with a visible definition and
a modeled, non-Boolean underlying integer type decomposes into one case per
distinct enumerator value, in declaration order, plus the residual case
`unnamed`. Clang supplies the enum identity, underlying width and signedness,
and the constant enumerator values. The logical value ranges over the **entire
underlying integer type**, because that is the C++ value set of a scoped enum;
it is not a finite domain inferred from the listed names.

Enumerators that share a value are aliases naming one case, reachable by either
name; writing both is a duplicate. Named cases bind nothing. The residual case
binds one value: the subject at its exact underlying type, as an alias.

Enum values, constants, comparisons, and explicit `static_cast` from a scoped
enum to its **exact underlying type** are modeled. Other enum casts and implicit
conversions remain refused. The existing integer-literal representation cannot
express unsigned enumerators above `INT64_MAX`; enums containing them are
refused.

### 20.5.5 Representations without a provider

A representation no provider models is refused at the provider boundary, naming
the resolved C++ type. It is never reinterpreted as a sum because a proof used
arm syntax on it, and its states are never guessed.

The following are refused, each naming its reason rather than being decomposed
on an assumption: an incomplete type; a union, which requires an independently
justified active-member model; a base subobject, which requires an explicit
accessible projection; a component of unmodeled type, including a reference
member, whose referent another object can write; and an array extent or template
argument list that is unresolved or exceeds the proof resource limits.

Omission based on impossible-case evidence (§20.4) is not implemented; the
compiler reports an omitted arm rather than supplying an assumption.

See RFC 0013 and `TRUST.md` 41.2 for correspondence responsibilities.

---

# 21. Induction

`induction` proves a proposition for every value of a domain by applying that domain's induction principle.

Example:

```text
proof add_zero(unsigned x)
    proves (add(x, 0u) == x)
{
    induction x {
        zero => {
            refl;
        }

        successor(pred) => {
            assume below : pred < UINT_MAX;
            assume ih    : add(pred, 0u) == pred;
            ...
        }
    }
}
```

Each arm establishes one case of the principle. Arm binders name the case's structural components (§20.3).

A step case receives its premises in its proof context: any range condition, and one induction hypothesis per recursive component. `assume` names them, and the kernel MUST reject an `assume` whose proposition does not exactly match a supplied premise. Given a well-founded tree principle (§21.3):

```text
induction tree {
    empty => {
        ...
    }

    node(value, left, right) => {
        assume left_ih  : P(left);
        assume right_ih : P(right);
        ...
    }
}
```

Induction hypotheses come from the principle. A proof does not obtain one by invoking itself.

The short form:

```cpp
proof add_zero(unsigned x)
    proves (add(x, 0u) == x)
{
    induction x;
}
```

leaves every case to proof automation (§15.4). Automation that closes a case MUST produce valid proof evidence. A case it cannot close leaves the proof unproven.

`induction` is a proof statement (§15). It is not runtime control flow and generates no runtime code.

---

## 21.1 Induction principles

Induction MUST follow the induction principle associated with the value's domain.

An induction principle MUST be well founded and MUST correspond to the runtime semantics of that domain.

Induction over a domain without a defined principle MUST be rejected.

An implementation MAY provide convenient tactic syntax.

Convenience syntax does not alter the induction rule.

---

## 21.2 Machine integers

For an unsigned integer type `T` whose maximum value is `max`, the principle has the form:

```text
P(0)

∀ n : T,
    n < max → P(n) → P(n + 1)

therefore:

∀ n : T,
    P(n)
```

Its cases are `zero` and `successor(pred)`. The `successor` arm receives the premises `pred < max` and `P(pred)`.

The successor step applies only below `max`. It never wraps.

Principles for other integer types MUST likewise respect their machine range and defined behavior (§29).

---

## 21.3 Pointer-linked structures

A pointer type has no induction principle by type alone.

A value of type `Node*` may be null, cyclic, dangling, or shared.

Induction over a pointer-linked structure MUST be justified by an explicit well-founded premise, such as finite acyclic reachability under the memory model (§32).

Without such a premise it MUST be rejected.

The labels and premises of such a principle follow from the premise that justifies it. Their exact form is not yet specified.

---

## 21.4 Mathematical domains

For `@N` the cases are `zero` and `successor(pred)`, with no range condition. The principle has the conceptual form:

```text
P(0)

∀ n,
    P(n) → P(n + 1)

therefore:

∀ n,
    P(n)
```

---

# 22. Termination

Proof-producing computation that participates in logical reduction MUST terminate.

C++L MUST NOT permit divergence to manufacture arbitrary proof evidence.

---

## 22.1 Runtime divergence

Ordinary runtime C++ functions may diverge.

C++L does not globally require all C++ programs to terminate.

---

## 22.2 Proof-relevant computation

A function used in:

```text
proof normalization
definitional equality
inductive proof computation
total formal functions
```

MUST have termination established.

---

## 22.3 `decreases`

A termination measure may be expressed using:

```cpp
decreases (expression)
```

Example:

```cpp
pure unsigned gcd(unsigned a, unsigned b)
    decreases (b)
{
    return b == 0u ? a : gcd(b, a % b);
}
```

For recursive calls, the declared measure MUST decrease according to a well-founded ordering.

---

## 22.4 Lexicographic measures

A C++L implementation MAY support tuples of measures interpreted lexicographically.

Every accepted termination ordering MUST be well-founded.

---

# 23. Partial and total correctness

C++L distinguishes partial correctness from total correctness.

A verified postcondition ordinarily means:

```text
if the function begins in a state satisfying its preconditions
and returns normally,
then its postconditions hold
```

This does not by itself prove termination.

A total-correctness claim additionally requires termination.

A verified function whose body contains a loop, or calls a verified function
whose contract is partial, has a **partial-correctness contract**. Its body is
not a total formal function, so it MUST NOT be admitted as a definition the
formal core may unfold, and no Law or specification expression may mention it.
Its contract is established from verification conditions (§24.3), each of which
is an ordinary proposition requiring kernel-checked evidence. A verified caller
uses such a contract only as it uses any other: the call's result is a fresh
value of which the postcondition is supposed, after the precondition has been
proven, and the caller's own contract is then partial as well. Reports MUST
distinguish partial-correctness contracts from total ones.

---

# 24. Loop invariants

Imperative loops may carry formal invariants.

Example:

```text
while (condition)
    invariant (P)
{
    ...
}
```

---

## 24.1 Invariant obligations

An invariant is not automatically assumed.

Verification MUST establish:

```text
the invariant before the first iteration

and

preservation of the invariant by every iteration
```

---

## 24.2 Loop termination

When termination is part of the required property, a loop MAY use:

```cpp
decreases (measure)
```

The measure MUST strictly decrease on each continuing iteration under a well-founded ordering.

---

## 24.3 Verified loops

A verified body MAY contain `while (c) invariant (I) { body }`
and `for (init; c; step) invariant (I) { body }`, with at most one
invariant clause and a block body. The invariants are specification
expressions resolved by Clang in the scope of the loop head, and conjoin.

Every local that the loop's condition, step or body writes is **carried**. At
the head, each carried local denotes a fresh value; every other local keeps the
version it had. The following conditions MUST each be proven, under everything
the path supposes where they stand:

```text
entry:         for each invariant Ij, Ij holds of the carried locals' values
               where the loop is entered
preservation:  for each Ij and each way an iteration can end - the end of the
               body followed by the step, or `continue` followed by the step -
               Ij holds of the values the carried locals then hold, supposing
               every invariant and the condition at the head of that iteration
exit:          what follows the loop is verified supposing every invariant and
               the negated condition of the fresh head values
```

A `break` continues with what follows the loop under the versions current at
the `break`, supposing what the path supposes there and nothing more; a
`return` in the body is a return path under the same suppositions. Calls in the
condition, the step and the body are verified calls under section 12.6, proven
where the loop makes them. Multiple invariants are proven one by one and
supposed one by one, which is their conjunction; no conjunction connective is
required.

Loops establish partial correctness only (§23): nothing here proves that a loop
terminates, and the function containing it has a partial-correctness contract.
`decreases` on a loop is rejected until termination is verified rather than
accepted unchecked. `do`/`while`, range-based `for`, a `for` without a
condition, a condition that declares a variable, an invariant clause not
followed by a block, and loop invariants outside a verified function are
rejected. The generated conditions add no kernel rule and no logical
assumption; the loop rule that generates them is part of the correspondence
layer (`TRUST.md` 41.2). Erasure removes the invariant clauses and preserves
every loop, condition, step, body statement, `break` and `continue` as written.

---

# 25. Ghost state

`ghost` declares proof-only state.

Example:

```cpp
ghost auto initial_balance = account.balance;
```

Ghost state may record symbolic information useful for proof. The declaration form is a `ghost`-prefixed local simple declaration in a verification-enabled block. Ghost parameters, members and globals are not part of this grammar.

---

## 25.1 Runtime erasure

Ghost locals MUST be erased before runtime execution. They MUST NOT be converted into runtime data or used to affect runtime behavior.

---

## 25.2 No runtime influence

Ghost state MUST NOT:

```text
control runtime branching
change runtime return values
perform runtime I/O
change runtime object layout
change runtime ABI
change destruction behavior
escape through runtime FFI
```

---

## 25.3 Runtime values in ghost reasoning

Ghost state MAY refer symbolically to runtime values.

Doing so does not require a runtime copy when the same information can be represented formally.

---

# 26. `unsafe`

`unsafe` marks a boundary where C++L's strongest verification guarantees are not claimed.

Example:

```cpp
unsafe {
    platform_specific_operation();
}
```

An ordinary function declaration may also carry contextual `unsafe` after its ordinary prefix specifiers. There is no unsafe expression form; unsafe does not combine with `verified` or `pure` to waive obligations.

---

## 26.1 Unsafe is not trusted

`unsafe` means:

```text
not proven here
```

It does not mean:

```text
assumed correct
```

---

## 26.2 Unsafe cannot manufacture proofs

Unsafe runtime code MUST NOT directly create valid proof evidence.

An unsafe result may enter verified reasoning only through an explicit mechanism such as:

```text
runtime validation
trusted contract
independently established proof
```

---

## 26.3 Unsafe dependencies

If a verified proposition depends on an unmodeled fact produced only by unsafe code, the proposition remains unresolved unless that boundary is otherwise justified.

---

## 26.4 Runtime behavior

Unsafe code retains ordinary C++ runtime semantics.

`unsafe` does not create an alternate execution model.

---

# 27. `trusted`

`trusted` introduces an explicit assumption.

Example:

```text
trusted law operating_system_contract(...)
    proves (...);
```

A trusted proposition is accepted as a premise without requiring proof inside C++L.

---

## 27.1 Trusted is not proven

A trusted proposition has status:

```text
TRUSTED
```

not:

```text
PROVEN
```

---

## 27.2 Derived proofs

A theorem derived correctly from trusted assumptions may have valid proof evidence relative to those assumptions.

Its trusted dependency closure remains semantically relevant.

---

## 27.3 No implicit trust

An implementation MUST NOT silently convert:

```text
unsupported
unknown
timeout
unverified
unsafe
proof failure
```

into:

```text
trusted
```

Trust must be explicit.

---

# 28. Runtime validation

Some values cannot be known until runtime.

C++L therefore distinguishes:

```text
static proof
```

from:

```text
runtime validation
```

Example:

```text
network integer
    ↓
runtime predicate check
    ↓
Percentage
```

---

## 28.1 Successful validation

On a successful runtime-validation branch, the validated property may be used as a known fact for that concrete value.

---

## 28.2 Failed validation

A failed validation MUST NOT construct the refined or validated value.

---

## 28.3 Runtime-check status

A property established by executing a runtime check has status conceptually equivalent to:

```text
RUNTIME-CHECKED
```

It is not a universal compile-time theorem.

---

## 28.4 Validation survives erasure

Runtime checks required to establish properties of dynamic input MUST NOT be erased.

---

# 29. Machine arithmetic

C++ runtime numeric types retain their selected C++ machine semantics.

For example:

```cpp
int
unsigned int
std::uint32_t
```

are not silently replaced with mathematical integers.

---

## 29.1 Mathematical and machine numbers are distinct

A proof about mathematical integers MUST NOT silently be applied to bounded machine integers when overflow or other machine behavior can differ.

---

## 29.2 Overflow

Verification involving arithmetic MUST respect the selected C++ semantics.

For example, proof must distinguish cases such as:

```text
mathematical addition
unsigned modular arithmetic
signed arithmetic with undefined overflow
checked arithmetic
```

C++ defines `+`, `-` and `*` on two unsigned operands of one type, after the
usual arithmetic conversions, as the result reduced modulo `2^w`. That is
exactly the core's wrapping arithmetic (section 7.1.1), so those operators MAY
be modeled by it. Operands narrower than `int` are promoted to `int` first, and
the promoted operation is signed: it MUST NOT be modeled as unsigned wrapping.
Signed `+`, `-` and `*` MUST NOT be modeled by wrapping arithmetic at all;
until their no-overflow obligations are generated and discharged (section 31),
a verified body using them MUST be rejected.

---

## 29.3 Division and remainder

Operations such as division MUST satisfy all C++ conditions required for defined behavior.

For example:

```text
division by zero
```

cannot occur on a verified reachable path.

---

## 29.4 Shifts

Shift amounts and operand conditions MUST satisfy the selected C++ defined-behavior rules.

---

# 30. Floating-point semantics

C++ floating-point values are not mathematical real numbers.

Verification over floating-point types MUST account for relevant runtime semantics, including where applicable:

```text
rounding
precision
NaN
infinity
signed zero
exceptional values
target behavior
```

A verifier MUST NOT silently reason about:

```cpp
double
```

as exact:

```text
ℝ
```

unless an explicit abstraction justifies that correspondence.

---

# 31. Undefined behavior

A fully verified execution path MUST NOT rely on undefined behavior.

Verification MUST establish the preconditions required for every modeled potentially undefined operation on reachable verified paths.

Examples include:

```text
signed overflow
invalid shifts
division by zero
out-of-bounds access
invalid dereference
use-after-lifetime
invalid references
uninitialized reads
invalid pointer arithmetic
invalid casts
data races
```

---

## 31.1 Ordinary unverified C++

This rule does not cause all ordinary existing C++ containing potential undefined behavior to stop compiling.

The rule applies to the guarantees claimed for verified code.

Ordinary unverified C++ retains ordinary C++ semantics.

---

# 32. Object lifetime and memory

C++L does not replace the C++ object model.

Verified reasoning involving memory MUST respect relevant C++ semantics.

These include:

```text
object lifetime
storage duration
references
pointer validity
alignment
bounds
provenance
aliasing
moves
destruction
mutation
```

---

## 32.1 Pointers are not integers

C++L MUST NOT generally treat:

```text
pointer
```

as equivalent to:

```text
integer address
```

for proof purposes.

---

## 32.2 References

A reference carries the semantic requirements of the selected C++ object model.

It is not merely an integer or an arbitrary non-null pointer.

---

## 32.3 Moves

A move is not assumed to be semantically identical to a copy.

Post-move state must follow the semantics of the relevant C++ type.

---

## 32.4 Destruction

Destruction and RAII are observable runtime semantics where they have observable effects.

Proof erasure MUST NOT change required destruction behavior.

---

# 33. Exceptions

C++L preserves C++ exception semantics.

Unless otherwise specified:

```text
ensures
```

describes normal return.

If a proof requires:

```text
this call cannot throw
```

that property MUST itself be established.

An implementation MUST NOT silently assume exception freedom.

---

# 34. Concurrency

C++L does not replace the C++ concurrency and memory model.

Claims involving:

```text
threads
atomics
interleavings
happens-before
data-race freedom
lock invariants
```

require formal semantics sufficient for the claim being made.

A verifier MUST NOT establish concurrency properties using purely sequential reasoning when concurrent behavior can invalidate that reasoning.

Ordinary concurrent C++ remains ordinary C++ when no such verification claim is made.

---

# 35. Foreign code

C++L may interoperate with ordinary C, C++, platform APIs, assembly, and other foreign systems.

Foreign code is not automatically formally verified.

A verified caller may rely on foreign behavior only through an explicit boundary such as:

```text
verified specification
trusted contract
runtime validation
unsafe boundary
```

---

## 35.1 Foreign code and proofs

Runtime foreign code MUST NOT directly manufacture valid proof-domain values.

External proof artifacts may be accepted only if they satisfy the normal C++L proof-validation semantics.

---

# 36. Erasure

C++L proof-only constructs are erased before ordinary runtime execution.

The following are proof/specification-only unless otherwise stated:

```text
law
proof
proves
ghost
expects
ensures
invariant
decreases
proof-only type indices
proof evidence
```

---

## 36.1 Erasure must preserve runtime behavior

Erasure MUST preserve the observable runtime semantics of the executable program.

---

## 36.2 No automatic runtime assertion substitution

A specification clause MUST NOT become a runtime assertion merely because its proof failed.

Verification failure and runtime checking are separate mechanisms.

---

## 36.3 Refinement erasure

Unless otherwise specified:

```text
refinement value
```

erases to the runtime representation of its base value.

The proof of the refinement predicate is erased.

---

## 36.4 Ghost erasure

Ghost values and ghost operations have no runtime identity.

---

## 36.5 Runtime validation is not erased

Checks required for runtime validation remain runtime behavior.

---

## 36.6 No mandatory theorem runtime

A conforming C++L program MUST NOT require a theorem VM, proof interpreter, proof garbage collector, or equivalent runtime merely because compile-time proofs were used.

C++L runtime execution remains ordinary native C++ execution unless the program itself explicitly depends on another runtime library.

---

# 37. ABI semantics

The following constructs MUST NOT by themselves change an existing C++ function's native ABI:

```text
verified
pure
expects
ensures
law associations
proof declarations
case analysis and induction
proof-only mathematical domains
ghost state
trusted metadata
unsafe metadata
```

Proof-only values are absent from runtime calling conventions.

Refinement types use their base runtime representation unless explicitly specified otherwise.

Case analysis, induction, and proof-only mathematical domains have no runtime representation (§19–§21).

---

# 38. Verification statuses

C++L distinguishes different semantic assurance states.

They MUST NOT be collapsed into one generic `verified` result.

---

## 38.1 `PROVEN`

`PROVEN` means valid proof evidence exists for the proposition under its explicit premises.

A proven proposition may still have an explicit dependency on trusted premises.

That dependency is not erased by the word `PROVEN`.

---

## 38.2 `TRUSTED`

`TRUSTED` means the proposition is accepted explicitly as an assumption without an internal proof.

---

## 38.3 `RUNTIME-CHECKED`

`RUNTIME-CHECKED` means a property of a concrete runtime value is established through runtime validation.

---

## 38.4 `UNSAFE`

`UNSAFE` means execution crosses an explicit region for which C++L is not making its strongest formal guarantee.

---

## 38.5 `UNVERIFIED`

`UNVERIFIED` means no proof claim has been established for the relevant code/property.

Ordinary C++ may remain unverified and still compile.

---

## 38.6 `UNRESOLVED`

`UNRESOLVED` means a formal proof obligation exists but has not been successfully discharged.

---

# 39. No implicit status promotion

The following conversions are forbidden unless justified by the formal rules:

```text
UNRESOLVED → PROVEN
UNVERIFIED → PROVEN
UNSAFE     → PROVEN
TRUSTED    → PROVEN
```

A trusted premise may participate in the derivation of a proven theorem, but the premise itself remains trusted.

---

# 40. Ordinary C++ and verification boundaries

Ordinary C++ is not required to become verified merely because it is linked with or called by verified C++L code.

Example:

```text
verified C++L
    ↓
explicit boundary
    ↓
ordinary existing C++
```

is valid.

The formal guarantee stops or changes at the boundary unless an explicit specification bridges it.

---

# 41. Calls from verified code

When verified code calls another function, one of the following must provide the facts required by the caller's proof:

```text
verified contract
proven Law
trusted contract
runtime-validated result
explicitly irrelevant behavior
```

An unknown implementation MUST NOT silently contribute arbitrary formal facts.

---

# 42. Templates

C++ templates retain their normal C++ semantics.

C++L declarations MAY be parameterized by C++ templates where grammatically valid.

Proof obligations depending on concrete template values or types are obligations over the relevant instantiation unless established generically.

A proof for one template instantiation MUST NOT silently be reused for a semantically different instantiation.

---

# 43. Namespaces and scopes

C++L declarations participate in lexical scopes.

C++ entities referenced by C++L constructs follow ordinary C++ name lookup unless this specification defines a distinct formal-name rule.

Laws and proofs may be placed inside namespaces.

Formal names MUST resolve unambiguously.

---

# 44. Headers

C++L constructs may appear in supported C++ headers.

Existing:

```text
.h
.hpp
.hh
```

files do not require a separate C++L header format.

A Law or formal declaration placed in a header does not by itself change runtime ABI.

---

# 45. Modules

Where the selected C++ mode supports modules, ordinary module semantics remain C++ semantics.

A C++L implementation may expose formal declarations through module interfaces.

Imported formal declarations MUST preserve their proposition and trust meaning.

---

# 46. File extensions

C++L does not require existing projects to rename C++ source files.

Supported ordinary extensions may include:

```text
.cpp
.cc
.cxx
.h
.hpp
.hh
```

An implementation MAY additionally support a dedicated extension such as:

```text
.cppl
```

A dedicated extension MUST NOT be required merely to compile ordinary supported C++ as C++L.

---

# 47. `law` and implementation independence

A Law describes required behavior, not a specific algorithm.

For example:

```text
law sorted_output_preserves_count(...)
    proves (...);
```

may remain valid across different implementations.

Changing implementation while retaining the same Law is permitted only when the new implementation also satisfies that Law.

---

# 48. Tests and proofs

Passing tests do not establish a universal Law.

For example:

```cpp
assert(square(2) == 4);
assert(square(3) == 9);
```

does not prove:

```text
∀ x, square(x) >= 0
```

Tests remain valuable for software validation.

They are not interchangeable with formal proof.

---

# 49. Assertions and proofs

A runtime assertion:

```cpp
assert(P);
```

does not construct:

```text
Proof<P>
```

merely because execution has not yet failed.

Runtime assertions and formal proof are separate mechanisms.

---

# 50. Counterexamples

A valid counterexample may establish that a universal proposition is false.

Failure to find a counterexample does not establish that the proposition is true.

C++L MUST NOT equate:

```text
no counterexample found
```

with:

```text
PROVEN
```

---

# 51. Impossible states

C++L encourages C++ types that encode mutually exclusive states directly.

For example:

```cpp
using PaymentResult = std::variant<Receipt, Error>;
```

holds one alternative at a time. After an exception during assignment it may hold none (`valueless_by_exception()`).

Such a type may make states structurally impossible that would otherwise require Boolean invariants.

Proofs may rely on that exclusivity through case analysis (§20), which must also account for every other state C++ permits.

---

# 52. Formal assumptions and implementation limitations

A language implementation may lack support for proving some semantics defined by this specification.

Lack of implementation support MUST NOT change the meaning of the language rule.

The implementation may report:

```text
UNRESOLVED
UNVERIFIED
unsupported
```

but MUST NOT invent a weaker theorem.

Current implementation support is documented in `STATUS.md`.

---

# 53. Proof failure

When proof is mandatory for a declaration, proof failure is a compile-time verification failure.

It MUST NOT automatically become:

```text
warning-only success
runtime assertion
trusted assumption
unsafe block
ignored obligation
```

The programmer must explicitly choose a different semantic boundary if that is genuinely intended.

---

# 54. Specification failure

A malformed or semantically invalid specification is a language error.

Examples include:

```text
side effects in a specification expression
invalid use of result
invalid use of self
non-well-founded proof recursion
construction of refinement without justification
ghost value escaping into runtime
```

---

# 55. Proof irrelevance at runtime

Proof evidence has no runtime observational identity.

Runtime code MUST NOT:

```text
compare proof values
branch on proof object identity
serialize proof objects as ordinary values
take runtime addresses of erased proof values
depend on proof object layout
```

Proof information may affect whether compilation succeeds.

It does not otherwise become runtime state.

---

# 56. Trusted and unsafe are orthogonal

These concepts MUST remain distinct.

```text
unsafe
    no formal guarantee is claimed for the operation

trusted
    a formal proposition is accepted explicitly as an assumption
```

Unsafe code does not automatically create trusted facts.

Trusted facts do not imply the underlying runtime implementation is verified.

---

# 57. Verification and runtime checking are orthogonal

These concepts MUST also remain distinct.

```text
PROVEN
    property established statically through proof

RUNTIME-CHECKED
    property established dynamically for a concrete execution/value
```

A runtime check may provide a fact after the successful branch.

It does not retroactively become a universal static theorem.

---

# 58. Formal soundness requirement

No language construct may permit arbitrary propositions to be established without either:

```text
valid proof
```

or:

```text
explicit trust
```

In particular:

```text
divergence
undefined behavior
unsafe memory
solver failure
runtime assertion
compiler crash
unverified foreign code
```

MUST NOT manufacture formal proof evidence.

---

# 59. Language-level verification principle

For a proposition `P`, C++L distinguishes:

```text
P declared
P assumed
P checked at runtime
P proven
```

These are different semantic states.

The language MUST preserve that distinction.

---

# 60. Source compatibility principle

The presence of C++L in a toolchain MUST NOT require ordinary supported C++ code to contain:

```text
law
proof
verified
pure
ghost
trusted
unsafe
refinement types
```

merely to retain its previous runtime behavior.

Verification features are opt-in and additive.

---

# 61. Conformance requirements

A conforming C++L implementation MUST:

1. preserve valid supported C++ source compatibility;
2. preserve ordinary C++ runtime semantics when no C++L feature changes them;
3. treat C++L words contextually rather than globally reserving them;
4. preserve the distinction between runtime, specification, and proof domains;
5. preserve the distinction between proof and trust;
6. preserve the distinction between static proof and runtime validation;
7. reject invalid proof evidence;
8. prevent proof-only state from affecting runtime behavior;
9. preserve selected C++ machine arithmetic semantics;
10. prevent verified reasoning from silently relying on undefined behavior;
11. preserve explicit trusted and unsafe boundaries;
12. erase proof-only constructs without changing required runtime behavior;
13. avoid automatic runtime assertion substitution for failed proofs;
14. preserve the meaning of verification statuses;
15. fail rather than falsely report `PROVEN` when a required proof cannot be established.

---

# 62. Non-goals of the language semantics

C++L does not semantically require:

```text
a new operating system runtime
a garbage collector
a theorem VM
a replacement linker
a replacement native ABI
a mandatory runtime proof engine
a rewrite of existing C++ dependencies
whole-program verification before any code can compile
general-purpose algebraic data types
runtime pattern matching
a logical restatement of the program's C++ types
```

These are not requirements of the C++L language model.

---

# 63. Core examples

## 63.1 Ordinary C++

```cpp
int add(int a, int b) {
    return a + b;
}
```

This remains ordinary C++.

No proof claim is implied.

---

## 63.2 Verified identity

```cpp
verified int identity(int x)
    ensures (result == x)
{
    return x;
}
```

Required proposition:

```text
∀ x : int,
    identity(x) == x
```

for normal return.

---

## 63.3 Preconditions

```cpp
verified int divide(int x, int y)
    expects (y != 0)
{
    return x / y;
}
```

The verified call domain excludes:

```text
y == 0
```

unless another control-flow branch handles it before the division.

---

## 63.4 Standalone Law

```cpp
law identity_returns_input(int x)
    proves (identity(x) == x);
```

The Law is a proposition.

Its declaration alone does not prove it.

---

## 63.5 Explicit proof

```cpp
proof integer_reflexivity(int x)
    proves (Eq<int>(x, x))
{
    refl;
}
```

---

## 63.6 Refinement

```cpp
type Percentage = int where (self >= 0 && self <= 100);
```

An arbitrary `int` cannot silently become `Percentage`.

---

## 63.7 Ghost state

```cpp
ghost auto starting_balance = account.balance;
```

The ghost value may participate in proof.

It does not become runtime state.

---

## 63.8 Trusted external contract

```text
trusted law external_api_contract(...)
    proves (...);
```

The contract is explicit trust, not proof.

---

## 63.9 Unsafe boundary

```cpp
unsafe {
    platform_intrinsic();
}
```

The block retains runtime behavior.

No formal facts emerge automatically from it.

---

# 64. Fundamental semantic model

The C++L model can be summarized as:

```text
ordinary C++
    says what executes

Laws and contracts
    say what must be true

proofs
    establish why it is true

trusted declarations
    state what is assumed

runtime validation
    establishes facts about dynamic values

unsafe boundaries
    mark where proof guarantees stop

erasure
    removes proof-only information

runtime
    remains ordinary native C++
```

---

# 65. Fundamental rule

For every proposition claimed as `PROVEN`, the language semantics require:

```text
explicit proposition
+
valid derivation
+
explicit premises
```

Never:

```text
because a test passed
because an assertion did not fail
because automation said so
because unsupported behavior was ignored
because the implementation needed the statement to be true
```

**C++L is C++ with Laws: existing C++ continues to execute as C++, while formal intent may be stated and mechanically established without turning proof machinery into runtime behavior.**

## Canonical surface conformance

[RFC 0015](./rfcs/0015-canonical-language-surface.md) reconciles historical
surface examples. [GRAMMAR.md](./GRAMMAR.md) is the single concrete grammar;
[DEVELOPER_GUIDE.md](./DEVELOPER_GUIDE.md) is the practical usage guide. Implementation
fragment notes constrain available verification power, never authorize an
alternate surface spelling or unchecked acceptance.
