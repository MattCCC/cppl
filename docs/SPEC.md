# C++L Language Specification

**C++L - C++ with Laws**

Status: Normative target specification

This document is the primary normative definition of C++L language semantics.
It specifies the language that a complete conforming implementation MUST provide.
It is intentionally independent of repository progress, implementation staging
and engineering limitations.

Repository source, tests and `STATUS.md` describe implementation state; they do
not weaken, narrow or redefine this specification.

This document defines what C++L programs mean. It does **not** define:

- compiler architecture or implementation component boundaries;
- proof-kernel implementation technique;
- solver implementation technique;
- caching or incremental-build strategy;
- editor integration;
- release planning;
- repository implementation status.

Those concerns belong in `ARCHITECTURE.md`, `DESIGN.md`, `FOUNDATIONS.md`,
`TRUST.md`, `COMPATIBILITY.md` and `STATUS.md` as appropriate.

A complete implementation MUST implement every non-optional language construct
and semantic obligation defined here and in the normative grammar. A missing
implementation feature is an implementation defect or incomplete conformance; it
MUST NOT be reinterpreted as permission to change the language semantics.

When documents disagree:

1. this specification is authoritative for language meaning and semantic
   obligations;
2. `GRAMMAR.md` is authoritative for concrete parsing forms consistent with this
   specification;
3. `FOUNDATIONS.md` may formalize the proof calculus without changing the source
   language meaning stated here;
4. `TRUST.md` defines the trusted-computing-base and correspondence obligations;
5. `COMPATIBILITY.md` defines supported C++ language/library modes and platform
   compatibility;
6. `STATUS.md`, roadmaps, tests and implementation notes are non-normative with
   respect to language meaning.

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
readable
writable
size
at
contains
```

`readable` and `writable` are the memory propositions of §12.10. `size`, `at`
and `contains` have proof-intrinsic meaning only for the mathematical domains
defined in §19.1. Outside those formal contexts, identically spelled names remain
ordinary C++ identifiers.

The following words have special meaning only as proof statements inside a proof body (§15):

```text
refl
exact
apply
assume
rewrite
cases
decompose
induction
```

C++L does not define `data` or `match`. It introduces no algebraic data types and no runtime pattern matching (§19).

The mathematical-domain spellings `@N`, `@Z`, `@Seq`, `@Set` and `@Map` are C++L tokens (§19.1). No valid C++ program contains them outside literals and comments.

Proof-arm labels such as `unnamed`, `alternative`, `valueless`, `some`, `none`,
`value`, `error`, `null`, `non_null`, `components`, `zero` and `successor` have
C++L meaning only in the corresponding `cases`, `decompose` or `induction` arm
position. They are not globally reserved identifiers.

Existing C++ keywords retain their existing C++ meaning.

C++L MUST NOT redefine an existing C++ keyword for unrelated C++L semantics.

In particular:

```text
requires
```

belongs to C++ and is not a C++L contract keyword.

---

## 3.1 C++-first disambiguation

Outside a grammatical C++L construct, when a token sequence is valid ordinary
C++ in the current context, the ordinary C++ interpretation takes precedence.

Once the parser has entered a C++L specification or proof context, syntax that
this specification assigns a formal meaning takes precedence within that
context. This includes, where grammatically applicable:

```text
Eq<T>(...)
forall (...)
exists (...)
->
<->
&&
||
```

Only the operands or subexpressions that the C++L grammar designates as ordinary
C++ expressions are then resolved with ordinary C++ meaning by Clang.

For example:

```cpp
int law = 1;

void proof();

struct ghost {};
```

remains ordinary C++ because no C++L grammatical context has been entered.

Conversely, an `Eq<T>(a, b)` appearing where the specification grammar expects a
formal proposition denotes C++L propositional equality even if an ordinary C++
entity named `Eq` is visible. Likewise, top-level `->` in the implication
position defined by the specification grammar denotes implication rather than
member access.

C++L MUST NOT globally reinterpret contextual identifiers or operators outside
their defined grammatical contexts.

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

When the specification grammar consumes tokens such as `&&`, `||`, `->`, `<->`
or `Eq<T>(...)` as formal syntax, those tokens are not first interpreted as one
complete ordinary C++ Boolean expression. Their C++L logical semantics apply,
and the ordinary C++ subexpressions forming their operands remain subject to
Clang resolution and defined-behavior requirements.

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

Definitional normalization of machine integers MUST respect the exact semantics
of the selected C++ integer type. For an unsigned type of width `w`, addition,
subtraction and multiplication are interpreted modulo `2^w`. For signed integer
operations, normalization is valid only on paths where the corresponding C++
operation has defined behavior; signed overflow MUST NOT be modeled as wrapping.

Normalization MAY use algebraic canonicalization only for identities that are
valid for every value of the modeled machine type under those semantics. It MUST
NOT use mathematical-integer rewrites that fail for bounded machine arithmetic.

Comparison normalization MAY use logically equivalent rewrites, but MUST preserve
the signedness, width, promotions and defined-behavior requirements selected by
C++.

Definitional normalization is deterministic. If a verifier cannot normalize a
term soundly, it MUST leave the term opaque or reject the attempted proof; it
MUST NOT approximate it with a stronger proposition.

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

## 7.5 Arithmetic reasoning over machine integers

Arithmetic proof rules MUST reason about the exact machine semantics of the
resolved C++ types. A consequence such as:

```text
i < n -> i + 1 <= n
```

requires evidence that includes every side condition needed for the C++ operation
to be defined and for the implication to hold at the relevant width and
signedness.

A conforming proof checker MAY use normalization, decision procedures, SMT,
Presburger arithmetic, certificate checking or other automation, but acceptance
requires sound evidence according to the formal proof rules. Solver success alone
is not proof.

Contradictory established premises may prove any proposition according to ordinary
logic, but contradiction itself must be established from valid premises. Machine
integer overflow, conversion, comparison and promotion rules MUST NOT be silently
replaced by unbounded-integer reasoning.

---

## 7.6 Conjunction

In specification and proof context:

```text
P && Q
```

denotes logical conjunction. Evidence for the conjunction requires evidence for
both `P` and `Q`; checked evidence for the conjunction permits elimination of
either side.

Specification conjunction is proof-domain composition, not runtime C++
short-circuit execution. Each operand must therefore be a well-formed proposition
with defined specification semantics under the proof context in which the
conjunction is formed. Runtime `&&` appearing in executable C++ retains ordinary
C++ short-circuit semantics (§12.7).

Conjunction associates according to the normative grammar. It binds no runtime
storage and erases completely.

---

## 7.7 Logical equivalence

`P <-> Q` denotes `(P -> Q) && (Q -> P)` as specified in GRAMMAR.md 30.
Both directions require explicit kernel-checked evidence. It adds no kernel
rule or assumption. Equivalence is looser than implication and conjunction;
repeated equivalence associates to the left. It composes with explicit equality,
quantifiers, implications and conjunctions wherever a proposition is accepted.

---

## 7.8 Disjunction

In specification and proof context:

```text
P || Q
```

denotes logical disjunction. Evidence for the disjunction identifies and proves
at least one side. Elimination of a disjunction requires establishing the target
conclusion from each possible side.

`P || !P` is not an implicit axiom. Classical principles may be used only when
specified by the formal foundation and represented by valid evidence.

Specification disjunction is proof-domain composition, not runtime C++
short-circuit execution. Each operand must be a well-formed proposition with
defined specification semantics. Runtime `||` retains ordinary C++ short-circuit
semantics (§12.7).

Disjunction binds no runtime storage and erases completely.

---

# 8. Universal quantification

Parameters of a `law` or `proof` are universally quantified unless a more local
binder shadows them.

For example:

```cpp
law nonnegative_identity(unsigned x)
    proves (x == x);
```

denotes conceptually:

```text
forall x : unsigned, x == x
```

Explicit universal quantification is written:

```cpp
forall (T x) {
    proposition
}
```

Multiple binders are permitted:

```cpp
forall (T x, U y) {
    proposition
}
```

A binder type is a verification type: an ordinary C++ type whose values have a
formal model, a refinement of such a type, or a proof-only mathematical domain.
The binder ranges over the complete value domain of that type. In particular,
`forall (unsigned x)` ranges over the complete machine-`unsigned` value set,
whereas `forall (@N x)` ranges over mathematical natural numbers.

Quantifier binders introduce proof-domain variables only. They allocate no
runtime storage and generate no runtime loop.

## 8.1 Universal introduction and elimination

To prove `forall (T x) { P(x) }`, the proof must establish `P(x)` for an arbitrary
fresh `x : T` without assuming any property of `x` beyond facts supplied by its
type and surrounding premises.

Checked evidence for `forall (T x) { P(x) }` may be instantiated at any
well-typed term `t : T` to produce evidence for `P(t)`. Instantiation performs
capture-avoiding substitution and preserves all refinement and definedness
obligations of `t`.

Nested universal quantifiers follow the same rule from outermost to innermost.

## 8.2 Implication

In specification and proof context:

```text
P -> Q
```

denotes logical implication and is right-associative according to `GRAMMAR.md`.
It claims `Q` under the premise `P`; it does not assert `P`.

A Law:

```cpp
law L(T x)
    expects (P(x))
    proves (Q(x));
```

therefore denotes the universally quantified implication:

```text
forall x : T, P(x) -> Q(x)
```

Implication introduction adds its premise to the proof context and requires proof
of its conclusion. Implication elimination requires evidence for both the
implication and its premise.

Within specification/proof grammar, the implication token takes its C++L meaning.
Ordinary C++ pointer member access `p->member` remains ordinary C++ when parsed as
an ordinary C++ subexpression. Parentheses may be used to make that boundary
explicit. The grammar MUST disambiguate the two without changing runtime C++
semantics.

---

# 9. Existential quantification

C++L supports existential propositions:

```cpp
exists (T x) {
    proposition
}
```

Conceptually:

```text
exists x : T, P(x)
```

A binder type follows the same verification-type rules as §8.

Proof of an existential proposition requires both:

```text
a witness w : T
+
proof evidence for P(w)
```

Conceptually, existential introduction is:

```text
w : T
p : Proof<P(w)>
-----------------
Proof<exists (T x) { P(x) }>
```

Existential elimination may use a checked existential only by introducing a fresh
witness and its property locally; neither may escape a scope in a way that would
make the result depend on the hidden witness.

The witness is proof-domain evidence and has no runtime identity merely because it
witnesses an existential proposition.

The source proof language has no standalone `witness` statement. An existential
goal is closed either by `exact` evidence already establishing the existential or
by proof automation that constructs a concrete witness together with
kernel-checkable existential-introduction evidence. Automation MUST expose enough
proof evidence for independent checking; failure to find a witness proves
nothing.

`exists` produces no runtime search or allocation and erases completely.

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

## 10.6 Scope and member Laws

A Law may appear at namespace or class scope wherever permitted by the normative
grammar. Namespace lookup follows ordinary C++ scope rules for referenced C++
entities.

A class-scope Law may refer to the implicit object through ordinary C++ member
lookup and `this` where that expression is valid. It does not create a runtime
member function. Its proposition is quantified over every explicit parameter and
over every implicit object state required by the Law's C++ member context.

A Law declared in an unnamed namespace has translation-unit-local formal identity.
A Law intended for use across translation units must be available through the
verification interface seen by its users.

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
verified unsigned divide(unsigned x, unsigned y)
    expects (y != 0u)
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

```cpp
verified int abs_value(int x)
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
verified int identity(int x)
    ensures (result == x)
{
    return x;
}
```

`result` is not globally reserved. Its return-value meaning is invalid in a Law,
a void postcondition, a precondition or a refinement predicate; ordinary C++ names
retain their ordinary meaning outside the special context.

It has special meaning only within a relevant postcondition and introduces no
runtime variable, parameter, storage or computation.

---

## 11.4 `old`

Inside a postcondition:

```cpp
old(expression)
```

denotes the semantic value of `expression` in the function pre-state.

Example:

```cpp
verified void withdraw(Account& account, int amount)
    expects (amount >= 0 && amount <= account.balance)
    ensures (account.balance == old(account.balance) - amount)
{
    account.balance -= amount;
}
```

`old(expression)` is legal as a snapshot only in a function postcondition. Its
expression is resolved in the function entry state, must be well-defined there,
and cannot use `result` or nested `old`. Elsewhere `old` is an ordinary C++ name.

`old(expression)` is a formal snapshot.

It does not imply that a runtime copy must be created.

---

## 11.5 Clause cardinality, ordering and layout

Specification predicates and measures MUST be parenthesized. A construct has at
most one clause of each kind. Function clauses are `expects`, `ensures`,
`decreases`; Law clauses are `expects`, `proves`; loop clauses are `invariant`,
`decreases`, in those orders. Conjoined predicates belong in a single `&&`
expression. A measure list is lexicographic and MUST NOT be merged as conjunction.

Whitespace between a clause word and its opening parenthesis is not semantically
significant. A conforming parser MUST therefore accept both:

```cpp
ensures(result == x)
```

and:

```cpp
ensures (result == x)
```

as the same clause.

Canonical presentation puts one space before each clause's opening parenthesis and
puts clauses on continuation lines. Refinement `where (P)` stays attached to the
declaration. Ordinary C++ prefix specifiers precede `verified pure`. Canonical presentation is defined by the normative grammar and formatting rules;
whitespace normalization does not alter semantics.

---

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

`ensures` applies to normal function return. C++L defines no separate exceptional-postcondition clause.

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

## 11.9 Member functions and the implicit object

A member-function contract uses ordinary C++ member lookup and `this`. The
identifier `self` is reserved for refinement predicates and is not an alternate
name for the implicit object.

For a non-static member function, preconditions observe the entry-state object.
Postconditions observe the normal-return post-state object unless an occurrence is
inside `old(...)`.

The cv/ref qualifiers of the member function retain their ordinary C++ meaning.
They do not by themselves prove purity, alias exclusivity or global immutability.

## 11.10 Constructors and destructors

A constructor has no `result` binding. Its `expects` clause is evaluated before
object initialization using only values that are valid in that entry state.
Its `ensures` clause describes the fully initialized object after successful
construction.

`old(member)` is invalid in a constructor when that member had no live initialized
entry-state value. Constructor initializer lists, delegating construction, base
construction and member initialization retain ordinary C++ order and lifetime
semantics and must be modeled accordingly.

A destructor may have an entry precondition. On normal completion, the object
lifetime has ended, so a destructor postcondition MUST NOT read dead members or
otherwise treat the destroyed object as live. It may refer to valid external
state and to legal `old(...)` snapshots captured from destructor entry.

Construction and destruction effects, including RAII effects during unwinding,
are runtime C++ behavior and MUST NOT be erased or reordered by verification.

## 11.11 Virtual functions and overriding contracts

Virtual dispatch remains ordinary C++. A call type-checked against a base virtual
function is verified from the base contract; the caller does not depend on which
override executes.

Every verified override MUST be substitutable for the overridden verified
contract. For a base precondition `P_base`, override precondition `P_over`, base
normal postcondition `Q_base`, and override normal postcondition `Q_over`, the
override must establish:

```text
P_base -> P_over

and, on normal return under P_base,

Q_over -> Q_base
```

Thus an override may weaken a precondition and strengthen a postcondition, but
must not strengthen the base precondition or weaken the base guarantee.

An override MUST NOT have a broader externally observable effect set than the base
contract permits. A base function relied upon as `pure` may be overridden only by
a function that satisfies the same purity guarantee. If the base contract is
required to be total, each override reachable through that virtual interface must
also satisfy the required termination guarantee.

Ordinary C++ rules for `virtual`, `override`, `final`, covariance, access and
`noexcept` continue to apply independently of these verification obligations.

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

## 12.5 Function verification semantics

For a verified function entity `f`, verification begins from its complete
Clang-resolved C++ declaration, contract, refinement information and function
body.

At function entry, the proof context contains:

- the function's `expects` proposition, when present;
- refinement predicates of refined parameters and of any refined implicit object
  state that the contract is entitled to assume;
- valid C++ type, lifetime and binding facts established by the language semantics;
- no additional facts merely because the implementation would benefit from them.

Verification MUST establish for every reachable execution path covered by the
claim:

- defined behavior for every modeled runtime operation;
- every callee precondition before the call;
- every refinement introduction or write obligation;
- every loop invariant and requested termination obligation;
- the declared purity obligation when `pure` is present;
- the function's `ensures` proposition on every normal return;
- the refinement predicate of a refined return type on every normal return;
- any required total-correctness property under §§22–24.

For a normal return of expression `R` from a non-void function, `result` denotes
the Clang-resolved value returned by that path. The postcondition is checked after
capture-avoiding substitution of that logical result and after applying the
post-state semantics of every visible storage location.

For a `void` function, there is no `result`; the postcondition is checked against
the normal-return post-state.

A verified body is not replaced by an assumed summary. The summary becomes usable
by callers only after evidence tying the body to that summary has been accepted,
or after an explicit trusted proposition provides the required fact.

All ordinary C++ syntax remains ordinary C++ syntax. Verification may reject a
program when its required semantics or proof obligations cannot be established,
but it MUST NOT reinterpret the runtime operation as a different C++ operation.

---

## 12.6 Compositional calls and summaries

For a call to a verified function, the verifier MUST resolve the callee through
ordinary C++ overload resolution and template instantiation, instantiate its
formal contract at the actual arguments and prove its complete entry obligation
before using any callee guarantee.

After the entry obligation is proven, the caller may use the callee's checked
normal-return postcondition, return-type refinement, purity/termination properties
and verified effect summary on the corresponding path. A call's own postcondition
MUST NOT be used to prove its precondition.

The logical result of a non-void call is fresh. The caller reasons from the
callee's checked summary rather than by assuming an arbitrary implementation.
Inlining or unfolding is permitted only when the callee is eligible for the
formal use in question, including purity and termination requirements.

Verification metadata required for compositional checking includes, as
applicable:

```text
contract propositions
refinement identities and predicates
Law/proof identities and evidence dependencies
purity
termination status and measures
effect summary
trust dependency closure
```

That metadata MUST be associated with the C++ entity across translation units,
headers, modules and explicit template instantiations. Native ABI symbols alone
are not sufficient proof metadata.

A call to ordinary unverified C++ remains executable C++. Such a call contributes
no unstated formal facts. Its return is an unconstrained value of the resolved C++
type except for facts guaranteed by ordinary C++ semantics, and every storage
location it may affect is invalidated according to §12.10. A later runtime check,
verified wrapper or explicit trusted Law may establish new facts; the unverified
call itself does not.

---

## 12.7 Path-sensitive control flow

Verification follows ordinary C++ control flow and evaluation order. Each runtime
branch creates proof contexts corresponding to the paths C++ can execute.

For `if`, conditional expressions, `switch`, loop conditions and other Boolean
runtime control flow, a true path may suppose the condition and a false path may
suppose its logical negation when the condition has a sound formal model. These
path facts are evidence scoped to the path on which they hold.

Runtime `&&` and `||` retain C++ short-circuit evaluation. Verification MUST NOT
reason about an operand on a runtime path on which C++ does not evaluate that
operand. `!` reverses the path proposition. This runtime rule is distinct from
proof-domain conjunction and disjunction in §§7.6 and 7.8.

Every normal return, throw, `break`, `continue`, `goto`, switch edge and exceptional
edge retains its ordinary C++ control-flow meaning. A verification engine may use
an equivalent control-flow representation, but the resulting obligations MUST
cover every runtime path relevant to the claimed property.

A path may be discharged as impossible only from checked contradiction evidence.
Syntactic unreachability heuristics, solver timeout or failure to enumerate a path
MUST NOT be treated as proof of impossibility.

A call, write or operation may use only facts established before that operation on
the same path and facts that remain valid under intervening effects and aliasing.

---

## 12.8 Locals, assignments and logical versions

Ordinary local variables retain ordinary C++ storage, lifetime, initialization,
shadowing and destruction semantics.

For verification, each successful write establishes a new logical version of the
written place. A read denotes the version current at that program point. Logical
versions are proof bookkeeping only and introduce no runtime object or copy.

Initializers, assignments, compound assignments, increments/decrements,
constructor calls and other writes MUST be verified according to the actual C++
operation selected by Clang. Any conversion, arithmetic definedness, lifetime,
refinement or capability obligation created by that operation must be discharged.

After a branch, reasoning is path-sensitive. Any representation of merged control
flow MUST preserve the exact path-dependent values and facts; merge bookkeeping
may not manufacture equality between values established on different paths.

A fact about an earlier version does not automatically constrain a later version.
Mutation through any alias that may designate the same place invalidates facts as
required by §12.10.

Automatic object destruction at scope exit is part of the runtime path and its
effects participate in verification. Erasure MUST NOT remove, duplicate or reorder
ordinary local construction/destruction.

---

## 12.9 References, aliasing and normal post-state

References retain ordinary C++ binding, collapsing, cv-qualification, lifetime
and aliasing semantics. A reference denotes existing storage; it does not create
independent storage merely for verification.

A write through a reference is a write to its referent and establishes a new
logical version of that place. Any other place that may alias it is invalidated or
updated according to the proven alias relation. A `const` reference restricts
writes through that access path but does not prove that the underlying object is
immutable through every alias.

For a function contract, value parameters denote their entry values. Reference
and pointer observations in `ensures` denote the normal-return post-state unless
inside `old(...)`. Repeated actual arguments that alias the same storage refer to
one underlying post-state, not independent copies.

A verified call applies its checked effect summary before its postcondition is
made available to the caller. Facts invalidated by that effect may be recovered
only from the postcondition, refinement guarantees or other independently checked
evidence.

Reference binding itself MUST NOT manufacture lifetime, uniqueness, initialization
or refinement evidence beyond what ordinary C++ and the current proof context
establish.

---

## 12.10 Storage, memory capabilities and effects

C++L uses a single storage model for locals, members, array elements, references,
pointers, temporaries and dynamically allocated objects. Refinements consume this
model; they do not create a separate storage semantics.

A **place** is a proof-level designation of C++ storage. A **region** is the live
C++ object or array allocation to which a place belongs. Places and regions have
no runtime representation of their own.

For pointer-based access, the verifier tracks the C++ facts needed to justify the
operation, including as applicable:

```text
object lifetime
provenance
bounds / array extent
alignment
initialization
read permission
write permission
cv/access restrictions
```

The specification-domain predicates:

```text
readable(p)
readable(p, n)
writable(p)
writable(p, n)
```

are built-in C++L memory propositions when `p` is a pointer to `T` and `n` is an
integral element count. They are not calls to user C++ functions and have no
runtime behavior.

`readable(p, n)` means that, under the selected C++ object model, the range of `n`
`T` objects beginning at `p` may be read for the proof path: the required objects
are live, initialized, within the relevant object/array bounds, provenance and
alignment are valid, and the access is permitted. `readable(p)` abbreviates one
object.

`writable(p, n)` means that the corresponding range may be written by the modeled
operation with valid lifetime, provenance, bounds, alignment and access rights.
It does not by itself assert the previous stored values. `writable(p)` abbreviates
one object.

A successful ordinary C++ operation may establish or consume these capabilities
according to C++ semantics. Examples include address-of a live object, array
construction, successful allocation, reference binding, object construction and
validated library abstractions. A mere `p != nullptr` proves only non-nullness; it
proves neither `readable` nor `writable`.

A pointer read `*p` requires `readable(p)`. A pointer write through `*p` requires
the write to be permitted by `writable(p)` and all C++ lifetime/type rules; after
a successful write, facts about the new stored value are established from the
write itself. Array subscripting and pointer arithmetic additionally require the
bounds/provenance obligations imposed by C++ including one-past rules.

Two places MAY alias unless C++ semantics and checked evidence establish
otherwise. Distinct complete local objects are disjoint while their lifetimes do
not overlap. Distinct non-overlapping subobjects are disjoint only when the C++
object model establishes that fact; unions, potentially-overlapping subobjects,
`[[no_unique_address]]`, base subobjects and implementation-defined layout MUST
NOT be treated as disjoint merely because they have different member names.

A write invalidates facts about every place that may alias the target. A call
invalidates facts about every mutable region in its effect set. Pointer values
passed by value may still provide access to caller storage; by-value parameter
passing proves only that the parameter object's own storage is distinct from the
caller argument object. `const` on a parameter or access path is not a global
frame condition.

Every verified function has a semantic **effect summary** derived from checked
body semantics. It records the externally observable storage the function may
read or write and other proof-relevant effects needed for composition. C++L adds
no required `reads` or `modifies` source clause: the summary is verification
metadata. A summary used across translation units MUST be transported and tied to
the checked function entity.

An unverified or foreign call with no checked effect summary is conservatively
assumed capable of modifying every mutable region it can access through its
arguments, reachable objects, globals/statics, escaped aliases, callbacks,
virtual dispatch and other C++-permitted mechanisms. A verifier may preserve a
fact only when it proves that the call cannot affect the place on which the fact
depends.

If the verifier cannot establish the capability, lifetime, alias or effect facts
needed for a verified operation, the verification claim fails closed. It MUST NOT
invent a capability or preserve a stale fact.

A `trusted law` may explicitly admit a memory proposition such as `readable(...)`
or `writable(...)`; doing so creates a normal trust dependency under §27. The
predicate remains proof-only and does not perform a runtime memory check.

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

An external declaration whose purity cannot be checked MUST NOT be treated as
pure merely from an unchecked declaration. C++L defines no `trusted pure` or
trusted-function-contract modifier. A function whose purity is not established by
checked semantics is unavailable for reasoning that requires purity.

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

Expressions used in Laws, contracts, refinements, invariants, termination
measures and proof propositions are specification expressions.

A specification expression may combine:

- pure, defined ordinary C++ expressions whose formal meaning is available;
- formal propositions and proof-only mathematical values;
- the logical operators and quantifiers defined by this specification;
- built-in specification predicates such as the memory predicates of §12.10.

## 14.1 Side effects

Specification expressions MUST be side-effect-free. They MUST NOT perform runtime
mutation, I/O, volatile access, observable atomic effects, allocation/deallocation
or any other runtime side effect merely because the specification is checked.

## 14.2 Defined behavior

Every ordinary C++ subexpression used in a specification MUST have defined C++
semantics under the proof context in which its value is required. Undefined
behavior cannot establish a proposition.

Logical `&&` and `||` in specification context do not hide an undefined operand by
runtime short-circuiting. Runtime short-circuit semantics apply only to executable
C++ control flow (§12.7).

## 14.3 Calls

A runtime function may be used as a mathematical function in a specification only
when its checked semantics are sufficient for that use. In particular, any
unfolded or definitionally reduced call must be pure and total for the relevant
inputs. A verified normal-return contract may be referenced propositionally
without granting unrestricted definitional unfolding.

An unverified function declaration, an unchecked `pure` claim or a function name
by itself supplies no formal semantics.

## 14.4 Contextual formal operators

Inside specification/proof grammar, `Eq`, `forall`, `exists`, `->`, `<->`, `&&`,
`||`, the memory predicates and other forms explicitly defined by C++L have their
formal meanings. Outside those grammatical contexts, identically spelled names
and operators retain ordinary C++ meaning.

The grammar MUST make every boundary between formal syntax and embedded ordinary
C++ expressions deterministic.

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

## 15.5 Law and proof distinction

A `law` names a theorem.

A `proof` names explicit reusable evidence for a proposition.

Conceptually:

```text
law
    = theorem / proposition

proof
    = named proof evidence
```

For example:

```cpp
law reflexivity(int x)
    proves (Eq<int>(x, x));
```

states a theorem.

A named proof may construct evidence for the same proposition:

```cpp
proof reflexivity_evidence(int x)
    proves (Eq<int>(x, x))
{
    refl;
}
```

A Law may also contain its explicit proof directly:

```cpp
law reflexivity(int x)
    proves (Eq<int>(x, x))
{
    refl;
}
```

A separate `proof` declaration is therefore useful when the evidence itself
requires a reusable name, acts as a proof helper, or should remain distinct from
the theorem declaration.

Both `law` and `proof` are proof-domain constructs. Neither has ordinary runtime
callable identity. Neither produces a runtime function merely because its syntax
contains parameters and a body. Both erase before native execution.

---

## 15.6 Proof statement semantics

A proof body is checked against a proof state consisting conceptually of:

```text
Γ    available premises and named evidence
G    current goal proposition
```

A proof statement MUST transform that state only through a sound proof rule and
MUST elaborate to kernel-checkable evidence.

### 15.6.1 `refl`

`refl;` closes the current goal only when the goal is an equality whose two sides
are definitionally equal under §7.1 and §16.

### 15.6.2 `exact`

```cpp
exact evidence;
```

requires `evidence` to elaborate, after valid instantiation and definitional
conversion, to `Proof<G>`. It closes the current goal. `exact` does not coerce an
unproven Boolean value, runtime assertion or ordinary object into proof evidence.

### 15.6.3 `apply`

```cpp
apply evidence;
```

requires `evidence` to establish a proposition whose conclusion can be
instantiated to the current goal. If its proposition is conceptually:

```text
P1 -> P2 -> ... -> G
```

then `apply` replaces the current goal with the ordered subgoals `P1`, `P2`, ... .
Every generated subgoal requires evidence. If the evidence has no conclusion
matching the current goal, `apply` is rejected.

### 15.6.4 `assume`

```cpp
assume h : P;
```

MUST NOT manufacture `P`.

It is legal in either of two cases:

1. `P` is already an available, as-yet-unnamed premise in `Γ`, such as a Law
   `expects` premise, a case discriminator premise, or an induction premise; or
2. the current goal is definitionally an implication `P -> Q`, in which case the
   statement performs implication introduction: it adds `P` to `Γ`, names that
   premise `h`, and changes the current goal to `Q`.

A Law application denotes the proposition of that Law instance (§10.4), so if
that proposition is an implication, `assume` may introduce its premise by the
second rule above.

In all other cases `assume` MUST be rejected.

### 15.6.5 `rewrite`

```cpp
rewrite h;
```

requires `h` to be checked evidence of an equality applicable to the current
proof state. Rewriting MUST be implemented through equality elimination,
substitution or an equivalent kernel-checked rule. It MUST preserve binding and
avoid capture. If no sound rewrite is available, the statement is rejected.

### 15.6.6 Structural proof statements

`cases`, `decompose` and `induction` transform the proof state only according to
the decomposition and induction rules in §§20–21. Their generated premises are
available to `assume`; they do not become axioms.

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

```cpp
type Percentage = int where (self >= 0 && self <= 100);
```

Conceptually:

```text
Percentage = { x : int | 0 <= x && x <= 100 }
```

A refinement has verification-level type identity while using the runtime
representation of its ultimate ordinary C++ base type.

## 17.1 `self`

Inside `where (P)`, `self` denotes the candidate value of the refinement's base
type. `self` is contextual and has no special meaning outside that predicate.

## 17.2 Introduction and construction

A value enters a refinement only when the complete refinement predicate is
established for that value in the current proof context, or when the required
fact is admitted explicitly through trust.

A refinement obligation is created at every semantic crossing that establishes or
changes refined storage or a refined value, including as applicable:

```text
local initialization
parameter entry into verified reasoning
function argument binding
return
assignment and compound update
member initialization and member write
array/element write
construction, copy and move
verified call post-state
```

Runtime path facts may discharge the obligation. No hidden runtime validation is
generated.

For example:

```cpp
type Positive = int where (self > 0);

verified Positive positive_or_one(int x)
{
    if (x > 0) {
        return x;
    }
    return 1;
}
```

The first return uses the branch fact; the second uses the literal value.

A refined parameter supplies its predicate as an entry premise of the verified
function. This is a formal precondition of the verified claim, not an ABI check.
An unverified external caller can physically pass a representation-equivalent
value that violates the refinement; in that execution the verified precondition
was not met and no C++L guarantee that depends on it applies.

## 17.3 Elimination and flow

A refined value may be used as its base value without an additional proof. Its
predicate remains available while the value/version to which it applies remains
unchanged.

A refined return type creates its own membership obligation on every normal
return; a duplicate `ensures` is unnecessary. An explicit `ensures` may add
additional postconditions.

A write to refined storage creates a new logical version and MUST establish the
refinement predicate for the new value. Mutation through a possible alias
invalidates facts about an earlier version according to §12.10.

## 17.4 Refinement implication and conversion

For refinements over the same ultimate base type:

```text
{ self : T | P(self) } <: { self : T | Q(self) }
```

requires evidence that the actual crossing value satisfies `Q`. A general
stronger-to-weaker conversion is justified by proof of `P -> Q`; a weaker-to-
stronger conversion requires the additional stronger fact at the crossing.

No refinement conversion inserts a runtime test.

Two overloads whose C++ signatures differ only by refinement identity erase to
the same native signature and therefore do not form distinct C++ overloads. Such
declarations are conflicting redeclarations unless ordinary C++ distinguishes
them independently of the refinement.

## 17.5 Nested refinements

If the base of a refinement is itself refined, all inherited predicates remain
part of membership:

```cpp
type NonNegative = int where (self >= 0);
type Percentage = NonNegative where (self <= 100);
```

A `Percentage` value must establish both predicates.

## 17.6 Refined members and elements

A refined data member or element is sound only if every way of establishing or
changing that storage proves the refinement, including aggregate/value/default
initialization where applicable, constructor initialization, copy/move
construction, copy/move assignment, direct and compound mutation, and mutation
through aliases.

Reading a refined member MUST NOT manufacture its predicate if some construction
or mutation path capable of producing the stored value escaped those obligations.
The common storage model of §12.10 applies.

## 17.7 Indexed refinements

A refinement family declares typed indices with parentheses and applies them with
angle brackets:

```cpp
type Index(unsigned n) = unsigned where (self < n);

Index<4u>
```

The index binder is in scope in the predicate. Each application substitutes the
actual index capture-avoidingly and creates a distinct verification-level
refinement identity as required by the formal type system.

Indices used as compile-time type arguments must have the stability and C++
template-argument properties required by §18. Proof-only indices may be erased
when they have no runtime role.

## 17.8 Runtime representation and erasure

A refinement declaration lowers canonically to the underlying C++ representation.
Conceptually:

```text
type R = T where (P);        ->  using R = T;
type R(I i) = T where (P);   ->  template <I i> using R = T;
```

Erasure MUST NOT introduce a wrapper, hidden tag, constructor, validation flag,
runtime predicate, RTTI distinction, hidden field, changed layout or changed
calling convention solely because a value is refined.

Runtime validation, when required for dynamic external input, is ordinary C++
control flow under §28 and remains runtime code.

---

# 18. Dependent and indexed formal types

C++L permits verification-level type meaning to depend on values through indexed
refinements and formal propositions.

```cpp
type Index(std::size_t n) = std::size_t where (self < n);
```

`Index<4>` applies that family at the value `4`.

## 18.1 Index stability

A value used in a C++L type identity must be stable for the lifetime of that type
identity. A C++ constant template argument, a proof-only binder in a purely formal
type, or another value whose identity is fixed by the formal context may be an
index. Arbitrary mutable runtime state MUST NOT silently become a stable type
index.

## 18.2 Dependent function meaning

A formal result type or proposition may depend on function parameters when the
index is valid in that formal context. Conceptually this is a dependent function
relationship:

```text
Pi (x : A), B(x)
```

This notation is explanatory; ordinary source function syntax and indexed type
applications are the source surface. Dependent meaning MUST NOT alter ordinary
runtime calling convention merely because the formal type carries an index.

## 18.3 Proof-only indices

An index that exists only for proof has no runtime storage, lifetime, address,
layout or ABI position. Erasure removes it unless the same source value also has
an independent ordinary C++ runtime role.

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

C++L provides the proof-only mathematical domains of §19.1 and the proof constructs of §§20–21. All such constructs are erased and have no runtime representation.

---

## 19.1 Proof-only mathematical domains

The core proof-only mathematical domains are:

```text
@N           natural numbers: 0, 1, 2, ...
@Z           mathematical integers: ..., -1, 0, 1, ...
@Seq<T>      finite sequences of T
@Set<T>      finite sets of T
@Map<K, V>   finite maps from K to V
```

The set of `@` domain constructors is closed by this specification. Other `@Name`
spellings are not C++L mathematical domains.

`T`, `K` and `V` are verification types and may themselves be ordinary modeled
C++ types or proof-only mathematical domains.

Mathematical domains are legal only in proof/specification positions such as Law
or proof parameters, quantifier binders, ghost proof state and type arguments of
other mathematical domains. They have no runtime object representation, address,
storage duration, ABI, `sizeof`, alignment, constructor or destructor.

Equality, universal/existential quantification and definitional identity apply to
all mathematical-domain values. `@N` and `@Z` additionally support exact
mathematical `+`, `-`, `*` and order comparisons; `@N` subtraction requires proof
that the result remains a natural number. Division/remainder are defined only
when their mathematical divisor is nonzero. Integer literals in a context that
requires `@N` or `@Z` denote the corresponding exact mathematical value; a
negative value is not a valid `@N` literal.

For `@Seq<T>`, the proof intrinsics `size(s)` and `at(s, i)` are defined, with
`size(s) : @N`; `at(s, i)` requires `i < size(s)`. For `@Set<T>`,
`contains(set, value)` is the core membership proposition. For `@Map<K,V>`,
`contains(map, key)` states key membership and `at(map, key)` requires that
membership and yields the mapped value. These names have their proof-intrinsic
meaning only when their arguments select these mathematical domains.

There is no implicit conversion between a machine integer and `@N`/`@Z`.
Specification-only casts use explicit functional type conversion syntax:

```text
@Z(x)
@N(x)
```

for a modeled integral C++ value `x`. `@Z(x)` denotes the exact mathematical value
represented by `x`. `@N(x)` additionally requires proof that the represented
mathematical value is nonnegative. These conversions are proof-only and erase.

No implicit conversion from an unbounded mathematical result back to a machine
integer exists. Relating such a result to a machine value requires proving that
the machine value represents the mathematical result under the selected C++
semantics.

The tokens `@N`, `@Z`, `@Seq`, `@Set` and `@Map` are lexically distinct C++L tokens
and do not invoke macros named `N`, `Z`, `Seq`, `Set` or `Map`.

---

## 19.2 Abstract models

A specification MAY relate a C++ object to an abstract mathematical value, such as a `std::vector<int>` to an `@Seq<int>`.

The function relating them is proof-only.

What that function states about a C++ type MUST be established by proof or declared as an explicit trusted assumption (§27). It MUST NOT be inferred.

---

# 20. Proof-side case analysis and decomposition

`cases` splits a proof obligation according to the complete logical state
partition of a modeled C++ value. `decompose` exposes product components. Both are
proof statements; neither creates runtime control flow or runtime pattern
matching.

Arms use one canonical form:

```text
Label(bindings) => {
    proof statements
}
```

For a label with no bindings, parentheses are omitted.

## 20.1 Sum case sets

The core case partitions are:

| C++ representation    | Cases                                                                 |
| --------------------- | --------------------------------------------------------------------- |
| scoped enumeration    | one case per distinct enumerator value, plus `unnamed(value)`         |
| `std::variant<Ts...>` | `alternative<i>(value)` for every alternative index, plus `valueless` |
| `std::optional<T>`    | `some(value)`, `none`                                                 |
| `std::expected<T,E>`  | `value(payload)`, `error(reason)`                                     |
| pointer `T*`          | `null`, `non_null`                                                    |

The standard-library cases apply when the selected C++ language/library mode
provides the corresponding standard type as defined by `COMPATIBILITY.md`.

A scoped enumeration ranges over the complete value set permitted by its
underlying integer type. `unnamed(value)` therefore represents every value equal
to no named enumerator and binds the exact underlying integer value. Enumerators
with equal values name the same logical case.

`std::variant` alternatives are identified by index, not merely by type, so
repeated types remain distinct. `valueless` represents
`valueless_by_exception()`.

Pointer `non_null` binds nothing. It proves only that the pointer is not null and
provides no lifetime, provenance, bounds, initialization, ownership, readability
or writability fact.

A type not listed above has no core sum decomposition unless another normative
section explicitly defines one. Using `cases` on a type without a case partition
is ill-formed C++L proof syntax for that subject.

## 20.2 Exhaustiveness and impossible cases

A `cases` statement MUST account for every semantic case. A case is accounted for
when either:

1. an arm is present for it; or
2. the current proof context establishes that the case discriminator is
   impossible.

There is no wildcard arm. `_` is not a C++L proof catch-all. Adding a new semantic
state therefore makes an older proof non-exhaustive unless that state is
independently proved impossible.

Omission by impossibility requires checked contradiction evidence from the
current proof context; it MUST NOT be inferred from heuristics or assumed by the
case partition itself.

## 20.3 Arm binders and premises

Arm binders denote existing logical values exposed by the modeled C++ state. They
create no runtime copy, object, conversion or temporary. Binder scope is the arm
only.

Each arm receives its discriminator as an available premise. `assume` may name
that premise only according to §15.6.4. Every arm must establish the enclosing
goal.

Nested `cases`, `decompose` and `induction` are permitted; their binders and
premises obey lexical proof scope and cannot escape.

## 20.4 Product decomposition

`decompose` is defined for these core product forms:

```text
complete non-union record with accessible modeled non-static data members
std::pair
std::tuple
std::array
built-in array
```

It has exactly one `components(...)` arm whose binders correspond in semantic
order to the exposed components:

```cpp
decompose point {
    components(x, y) => {
        ...
    }
}
```

Bindings are logical projections of the existing subobjects. No structured
binding, copy, move, construction or destruction is generated at runtime.

For records, base subobjects, inaccessible members, unions, potentially
ambiguous layout or components without a formal value model are not silently
invented as product components. If the required decomposition cannot be defined
from ordinary C++ semantics, the `decompose` statement is ill-formed for that
subject.

## 20.5 Stability and mutation

A proof-side subject denotes one logical value/version for the duration of the
structural proof step. Case or component facts apply only to that version.

If the surrounding proof system permits reasoning about mutable runtime storage,
subsequent mutation or a call that may mutate the subject invalidates those facts
through the normal storage/effect rules of §12.10. Case analysis does not receive
a separate aliasing exception.

## 20.6 Evidence and erasure

Case/decomposition evidence MUST be reducible to ordinary checked logical rules:
discriminator reasoning, conjunction/disjunction/implication, equality,
substitution and the formal state partition defined above. The correspondence
between each C++ representation and its logical partition is a trust-sensitive
language correspondence described by `TRUST.md`; a bug in that correspondence can
be a soundness bug and MUST NOT be treated as harmless merely because the
resulting internal proof term is locally well-typed.

`cases`, `decompose`, their binders and their proof branches erase completely.

---

# 21. Induction

`induction` is proof-only reasoning over a domain with a C++L-defined well-founded
induction principle. It is distinct from `cases`: induction supplies induction
hypotheses for structurally smaller values.

The short form:

```cpp
induction value;
```

requests proof automation for every case. The block form exposes the cases and
premises explicitly.

## 21.1 Mathematical naturals

For `@N`, the principle is:

```text
P(0)
forall n : @N, P(n) -> P(n + 1)
--------------------------------
forall n : @N, P(n)
```

The source cases are:

```text
zero
successor(pred)
```

The successor arm receives `pred : @N` and the induction premise `P(pred)`.

## 21.2 Unsigned machine integers

For an unsigned machine integer type `T` with maximum `max(T)`, the principle is:

```text
P(0)
forall n : T, n < max(T) -> P(n) -> P(n + 1)
------------------------------------------------
forall n : T, P(n)
```

The source cases are `zero` and `successor(pred)`. The successor arm receives both
the range premise `pred < max(T)` and induction hypothesis `P(pred)`, so the
successor step never relies on wraparound.

Signed machine integers do not use this zero/successor principle because their
value domain is not generated from zero by defined successor alone. `induction`
on a signed machine integer is ill-formed unless another induction principle is
explicitly defined by this specification.

## 21.3 Other C++ values

Raw pointers, arbitrary classes, graphs and recursive object structures do not
acquire an induction principle merely from their C++ type. A pointer may be null,
cyclic, dangling or shared, so pointer shape alone cannot justify structural
induction.

The core language defines no generic user-declared induction-principle syntax.
Therefore `induction` is well-formed only for domains for which this specification
(or another normative C++L standard section) defines the principle. Other
subjects are rejected rather than supplied an assumed well-founded relation.

## 21.4 Scope and evidence

Arm binders name structural values; induction hypotheses and range conditions are
premises supplied by the induction principle and may be named using `assume`.
A proof cannot obtain an induction hypothesis by recursively invoking itself.

Every case must prove the enclosing goal. Automation must produce evidence for
every case. `induction` and all induction evidence erase completely.

---

# 22. Termination

Proof-producing computation and any runtime computation relied upon as total in
formal reasoning MUST terminate. Divergence MUST NOT manufacture proof evidence.

## 22.1 Runtime divergence

Ordinary runtime C++ may diverge. A normal-return postcondition alone is a partial-
correctness claim and does not prohibit divergence.

## 22.2 Proof-relevant computation

Any computation unfolded during definitional equality, proof normalization or
other proof-relevant evaluation must be total for the evaluated inputs. A pure
function is not automatically total.

A verified runtime function used only through a normal-return contract may remain
partial unless total correctness is requested or required by its proof role.

## 22.3 `decreases`

A function or loop may request a termination proof with one clause:

```cpp
decreases (measure)
```

or one lexicographic list:

```cpp
decreases (outer, inner)
```

Each measure is a specification expression over a well-founded ordered domain.
For every recursive call or continuing loop iteration, the resulting measure
tuple must be strictly smaller lexicographically than at the source point, and
all expressions used in the comparison must be defined.

Writing `decreases` makes termination part of the verification claim. Failure to
prove descent is a verification failure; the clause MUST NOT be ignored.

## 22.4 Recursion and mutual recursion

For direct recursion, every recursive call must satisfy the declared descent.
For mutually recursive functions in one recursion strongly connected component,
the verifier must establish a common well-founded ranking sufficient for every
recursive edge. This may be represented by compatible declared measure tuples or
an equivalent formally checked ranking derived from them.

Recursive proof declarations and any compile-time proof computation are subject
to the same no-divergence principle even when no runtime code exists.

## 22.5 Well-founded domains

`@N` with `<` and finite unsigned machine domains with their natural non-wrapping
order are well-founded for termination measures. Lexicographic products of
well-founded orders are well-founded. A custom order may be used only when its
well-foundedness is itself part of the formal environment; no arbitrary C++
overload of `<` is assumed well-founded.

---

# 23. Partial and total correctness

C++L distinguishes partial correctness from total correctness.

A verified postcondition ordinarily means:

```text
if the function begins in a state satisfying its preconditions
and returns normally,
then its postconditions hold
```

This is partial correctness. It does not by itself prove termination.

A function has total correctness only when termination of every execution path
covered by the verification claim is also established.

C++L introduces no separate `total` keyword. Termination becomes
a required obligation in either of these ways:

1. the program explicitly writes `decreases (...)` on a recursive function or
   loop, thereby requesting a termination proof for that construct; or
2. the function is used in a proof-relevant context that requires a total formal
   function, including definitional reduction, proof normalization or a
   specification/Law position that depends on evaluating it (§22.2).

Straight-line finite control flow requires no `decreases` clause merely to show
termination. Recursion requires a sound well-founded argument, normally expressed
by function-level `decreases`. A loop contributes total correctness only when its
termination is established under §24.2. A call contributes total correctness only
when the callee's relevant contract is total.

Accordingly, the presence of a loop does not by itself permanently force a
function to be partial: a function containing loops may have a total-correctness
contract when every loop and called dependency required for termination has been
proved terminating. Conversely, any loop or call whose termination is not
established makes the containing contract partial with respect to termination.

A partial-correctness function body MUST NOT be admitted as a total formal
definition for unfolding in proof. A verified caller MAY still use its normal-
return contract, but the caller is itself partial if its own termination depends
on that partial call.

Reports MUST distinguish partial-correctness contracts from total-correctness
contracts.

---

# 24. Loop invariants

Imperative loops in verified code may carry one `invariant` clause and one
`decreases` clause in canonical order.

For `while` and traditional `for`:

```cpp
while (condition)
    invariant (I)
    decreases (M)
{
    body
}

for (init; condition; step)
    invariant (I)
    decreases (M)
{
    body
}
```

For a range-based `for`, the clauses follow the range-for header. For `do`/`while`,
they follow `do` and precede the body:

```cpp
do
    invariant (I)
    decreases (M)
{
    body
} while (condition);
```

Either clause may be omitted independently. A `for` with no condition is treated
as having the constant condition `true` for verification.

## 24.1 Invariant obligations

An invariant is not assumed merely because it is written. Verification must prove:

```text
entry:
    I holds before the first body execution

preservation:
    after every path that continues to another iteration,
    the next loop head satisfies I

normal exit:
    code after a condition-controlled loop may use I together with
    the fact that the loop condition is false
```

`continue` is a continuing path and must re-establish the invariant at the proper
next-iteration point after any `for` step semantics. `break` exits under the facts
established on its own path; it does not automatically acquire the negated loop
condition. `return` and `throw` leave the loop according to ordinary C++ control
flow.

For a `do` loop, entry means immediately before the first body execution. The body
therefore must satisfy the declared invariant even on the first iteration.

For a range-based `for`, verification follows the semantic C++ expansion of range
initialization, begin/end acquisition, iterator comparison, element binding,
increment and destruction while preserving the source-level invariant meaning.
The invariant holds at each iteration head after the range machinery required to
reach that head has executed and before the user body.

## 24.2 Loop-carried state

Every place that may be modified by the loop condition, body, step, called
functions or aliases is loop-carried state. At a generic loop head, facts about a
carried value are available only when established by the invariant, stable
external facts or other sound loop reasoning. The verifier MUST NOT reuse a
pre-loop version as though it were unchanged.

Writes, aliases and call effects inside loops follow §12.10.

## 24.3 Loop termination

A loop with `decreases (M)` requests totality for that loop. The verifier must
prove that `M` is in a well-founded domain and strictly decreases on every path
that continues to another iteration, including `continue` paths and the
traditional `for` step.

A loop with no established termination proof may still satisfy partial
correctness. If total correctness of the containing function is required, every
reachable loop on the relevant paths must have termination established.

## 24.4 Erasure

`invariant` and `decreases` clauses erase completely. The executable loop,
condition, range machinery, step, body, `break`, `continue`, `return`, exception
behavior, construction and destruction remain ordinary C++ runtime behavior.

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

Ghost locals MUST be erased before runtime execution. They MUST NOT be converted
into runtime data or used to affect runtime behavior. Because the entire ghost
declaration erases, its initializer and any proof-side destruction semantics MUST
be free of observable runtime effects. A ghost declaration that would require an
I/O operation, mutation, volatile/atomic effect, observable constructor or
destructor effect, or any other runtime side effect if executed MUST be rejected.

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

An ordinary function declaration may carry contextual `unsafe` after ordinary C++ prefix specifiers and before the return type, for example:

```cpp
unsafe unsigned read_device();
```

An `unsafe` declaration states that calls cross an unsafe runtime boundary. There
is no unsafe expression form. `unsafe` MUST NOT be combined with `verified` or
`pure` to waive their obligations.

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
`trusted law` evidence
independently established proof
```

---

## 26.3 Unsafe dependencies

If a verified proposition depends on a fact produced only by unsafe code and no checked or trusted evidence establishes that fact, the proposition remains unresolved.

---

## 26.4 Runtime behavior

Unsafe code retains ordinary C++ runtime semantics.

`unsafe` does not create an alternate execution model.

---

# 27. `trusted`

`trusted` explicitly admits a proposition without proving it. The production
trusted surface is a `trusted law` declaration with no proof body:

```cpp
trusted law external_assumption(T x)
    expects (P(x))
    proves (Q(x));
```

The proposition is accepted as an assumption relative to its declared premise.
Its status is `TRUSTED`, not `PROVEN`.

`trusted` is not a generic block, expression, cast, function modifier, purity
modifier or escape hatch. Other spellings have no C++L meaning.

## 27.1 Trust dependency

Any proof derived from a trusted Law is valid only relative to that assumption.
The complete transitive trust dependency closure MUST remain attached to the
resulting evidence and reportable by tooling even though the trusted declaration
erases from runtime code.

A trusted Law may state ordinary formal propositions, refinement relations and
built-in specification propositions such as `readable(...)` or `writable(...)`.
Such a Law admits the proposition; it does not perform runtime validation or
change memory.

## 27.2 No implicit trust

Unsupported semantics, unknown facts, solver failure, timeout, unsafe code,
unverified code and proof failure MUST NOT be silently converted into trust.
Only an explicit `trusted law` introduces a trusted premise.

## 27.3 Trusted Law restrictions

A `trusted law` MUST end with a semicolon and MUST NOT have a proof body. Its
proposition must be well-formed and side-effect-free even though it is not proved.
Its parameters and `expects` premise follow ordinary Law semantics; trust does not
change quantification or scope.

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

Runtime validation is performed using ordinary C++ execution. C++L does not
require a special `validate<T>()` language construct or standard runtime validator.
C++L ships no required runtime support library and injects no verification runtime
into the executable.

For example:

```cpp
type Percentage = int where (self >= 0 && self <= 100);

verified void accept_percentage_input(int raw)
{
    if (raw >= 0 && raw <= 100) {
        Percentage percentage = raw;
    }
}
```

The runtime `if` performs the actual validation. The refinement introduction is
checked because it occurs in a verified body. On the successful branch, C++L
may use the path facts:

```text
raw >= 0
raw <= 100
```

to establish that `raw` satisfies the refinement predicate for `Percentage`.

No hidden runtime check is generated by the refinement declaration, and
refinement introduction is not an implicit conversion unless the current proof
context already establishes the predicate.

---

## 28.1 Successful validation

On a successful ordinary C++ runtime-validation path, a property established by
that path MAY be used as evidence about the concrete runtime value.

The evidence applies only where the corresponding path fact remains valid.

For example:

```cpp
if (raw > 0) {
    Positive value = raw;
}
```

The refinement crossing is valid only on the branch where `raw > 0` has been
established.

A checked helper function MAY also establish a path fact through its verified
contract:

```cpp
verified bool is_percentage(int value)
    ensures (result <-> (value >= 0 && value <= 100))
{
    return value >= 0 && value <= 100;
}
```

Then:

```cpp
if (is_percentage(raw)) {
    Percentage percentage = raw;
}
```

may use the checked postcondition of `is_percentage` to justify the refinement
introduction. No special validation API is required.

---

## 28.2 Failed validation

A failed validation path MUST NOT construct or expose a refined value whose
predicate has not been established.

Failure handling remains ordinary C++ runtime behavior.

---

## 28.3 Runtime-check status

A property established by executing a runtime check has status conceptually equivalent to:

```text
RUNTIME-CHECKED
```

It is not a universal compile-time theorem.

---

## 28.4 Validation survives erasure

Ordinary C++ runtime checks written by the programmer remain ordinary runtime
behavior after C++L erasure.

Only the verification interpretation of a successful validation path is erased.
C++L itself MUST NOT add a runtime check that the programmer did not request
through ordinary executable C++.

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

# 32. Object lifetime, pointers and the C++ object model

C++L does not replace the C++ object model. Verification of memory operations must
respect object lifetime, storage duration, effective/dynamic type where relevant,
alignment, provenance, bounds, aliasing, initialization, cv/access rules,
construction, destruction and moves.

The detailed proof-level storage and capability rules are in §12.10.

## 32.1 Pointers are not integer addresses

A pointer is not generally equivalent to an integer address. Converting or
comparing pointer representations MUST preserve the guarantees and limitations of
the selected C++ semantics. Numeric address equality alone does not establish
provenance, lifetime or dereferenceability.

## 32.2 Pointer access

`p != nullptr` establishes only non-nullness. A dereference additionally requires
the capability and object-model obligations of §12.10. Pointer arithmetic and
subscript operations must remain within the C++-permitted object/array domain,
including one-past semantics.

## 32.3 References

A reference carries ordinary C++ lifetime, binding and aliasing semantics. It is
not merely an integer or an automatically valid non-null pointer. A reference does
not establish uniqueness, global immutability or a refinement fact not otherwise
proved.

## 32.4 Moves

A move is not assumed to be a copy. The moved-to and moved-from states follow the
actual C++ type's semantics. Facts about the pre-move object may be retained only
when justified by the type's checked contract/model.

## 32.5 Construction and destruction

Construction begins object lifetime only according to C++ rules. Destruction ends
lifetime in ordinary C++ order. RAII side effects, base/member destruction,
temporary destruction and unwinding are observable runtime semantics where C++
makes them observable; proof erasure MUST NOT change them.

## 32.6 Potentially overlapping storage

Verification MUST account for unions, base subobjects, bit-fields,
`[[no_unique_address]]`, placement construction and other C++ cases in which
source-level member distinction does not imply non-overlapping storage. Disjoint
places may be assumed only from C++ semantics or checked evidence.

---

# 33. Exceptions

C++L preserves ordinary C++ exception semantics.

`ensures` describes normal return only. A thrown exception does not have to satisfy
the normal postcondition unless another specification mechanism explicitly says
so; this specification defines no separate exceptional-postcondition clause.

Verified reasoning MUST nevertheless model the effects of throwing expressions,
stack unwinding and destructors whenever they can affect a claimed property. A
proof that a call cannot throw requires checked evidence from ordinary C++
`noexcept` semantics and/or verified body semantics; exception freedom is never
silently assumed.

A `noexcept` function retains the ordinary C++ consequence that an escaping
exception causes termination. Verification MUST NOT reinterpret that behavior.

Total correctness and exception behavior are distinct: termination may include
completion by a C++ exception, while a normal-return postcondition still applies
only to normal return.

---

# 34. Concurrency

C++L preserves the C++ concurrency and memory model. Ordinary threads, atomics,
locks and synchronization primitives keep their C++ runtime semantics.

A verification claim involving shared mutable state must account for all
interleavings permitted by the selected C++ model, including happens-before,
memory ordering, synchronization and data-race rules. Sequential reasoning MUST
NOT be applied to a value that another thread may change unless synchronization
or another proof establishes the required stability.

`const` does not imply cross-thread immutability. `pure` does not by itself imply
thread safety. A non-atomic read does not become stable merely because the same
thread has not written the object.

C++L introduces no separate runtime concurrency system. A verified concurrent
program succeeds only when the verifier can establish the required C++ memory-
model properties from the program's ordinary concurrency operations and checked
formal facts. Otherwise the verification claim fails closed.

---

# 35. Foreign and unverified code

C++L interoperates with ordinary C, C++, Objective-C++, assembly, platform APIs
and other foreign systems through ordinary runtime ABI mechanisms.

Foreign code is not automatically verified. A verified caller may obtain formal
facts about a foreign interaction only from:

- a checked verified wrapper whose own obligations are established;
- explicit `trusted law` propositions;
- ordinary runtime validation whose successful path establishes the fact; or
- facts already guaranteed by ordinary C++ semantics independently of the
  foreign implementation.

An `unsafe` boundary permits execution but supplies no formal facts by itself.

Runtime foreign code MUST NOT manufacture proof objects. External proof artifacts
may be consumed only when they are translated into the normal C++L evidence model
and independently checked according to the same proof rules.

Foreign pointer or buffer contracts that rely on trusted memory facts may use the
built-in memory propositions of §12.10 in an explicit `trusted law`; that trust
remains reportable and does not generate runtime checks.

---

# 36. Erasure

C++L separates runtime C++ from verification-only language constructs.

Verification-only constructs are erased before ordinary runtime execution.

These include, where applicable:

```text
law
proof
proves
expects
ensures
forall
exists
refl
exact
apply
assume
rewrite
cases
decompose
induction
ghost
invariant
decreases
refinement predicates
proof-only refinement indices
proof-only mathematical values
proof-only mathematical-domain intrinsics
memory capability propositions
proof evidence
trusted verification metadata
```

The contextual specification meanings of `result`, `old` and `self` exist only
during verification and introduce no runtime state of their own.

`verified` and `pure` are verification modifiers. Their markers erase while the
ordinary C++ function body remains. `unsafe` is a verification marker; the marker
erases while the ordinary C++ runtime operations inside the unsafe boundary remain.

A refinement declaration may require canonical lowering to its underlying C++
representation rather than simple token deletion.

---

## 36.1 Erasure must preserve runtime behavior

For every accepted C++L program `p`, erasure MUST preserve observable runtime
semantics:

```text
Sem_runtime(p) = Sem_runtime(erase(p))
```

C++L verification may affect whether a program is accepted. It MUST NOT otherwise
change what an accepted program does at runtime.

Verification-only constructs MUST NOT introduce hidden runtime assertions, proof
interpreters, proof tables, theorem dispatch, proof-only branches or loops,
refinement tags, validation flags, hidden verification fields, verification-only
constructors, observable verification-only temporaries or changed calling conventions.

---

## 36.2 No automatic runtime assertion substitution

A specification clause MUST NOT become a runtime assertion merely because its proof failed.

Verification failure and runtime checking are separate mechanisms.

---

## 36.3 Refinement erasure

Unless otherwise specified, a refinement has the runtime representation of its
base type and its proof of membership is erased.

For example:

```cpp
type Percentage = int where (self >= 0 && self <= 100);
```

lowers conceptually to an ordinary C++ representation equivalent to:

```cpp
using Percentage = int;
```

Refinement erasure MUST NOT introduce a wrapper, hidden tag, validation flag,
additional field, automatic runtime check or different calling convention.

---

## 36.4 Ghost erasure

Ghost declarations, values and operations have no runtime identity. A ghost
declaration erases completely, including its verification-only initialization and
destruction semantics. Runtime computation MUST NOT depend on erased ghost state.

---

## 36.5 Runtime validation is not erased

Runtime validation written as ordinary C++ remains ordinary runtime behavior. The
proof facts derived from a validation branch erase; the branch itself does not.

---

## 36.6 No C++L runtime requirement

C++L ships no required runtime support library for verification and injects no
verification runtime into the executable.

A conforming C++L program MUST NOT require a theorem VM, proof interpreter, proof
garbage collector, proof runtime, refinement runtime, contract runtime or hidden
validator runtime merely because C++L verification was used.

Runtime execution remains ordinary native C++ execution unless the program itself
explicitly depends on another runtime library or ordinary runtime support code.

---

# 37. ABI semantics

Verification-only C++L constructs MUST NOT by themselves change an existing C++
function's native ABI.

This includes:

```text
verified
pure
expects
ensures
proves
Law associations
proof declarations
forall / exists
proof commands
cases
decompose
induction
ghost state
loop invariants
termination measures
proof-only mathematical domains
proof-only mathematical-domain intrinsics
memory capability propositions
refinement predicates
proof-only refinement indices
trusted verification metadata
unsafe verification metadata
```

Proof-only values are absent from runtime calling conventions.

Refinement types use the runtime representation and ABI of their underlying C++
base type unless this specification explicitly defines otherwise.

Consequently, two declarations that differ only by refinement identity MUST NOT
silently become distinct native overloads when their erased C++ signatures are
the same.

Proof metadata needed for verification across translation units is not native ABI
state. A conforming implementation MUST transport or reconstruct that metadata by
a mechanism that preserves its semantic identity and trust dependencies; it MUST
NOT repair missing metadata by changing the native calling convention or by
manufacturing evidence.

Native ABI compatibility and verification metadata availability are separate
properties. If required verification metadata is unavailable, verification MUST
fail closed.

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
trusted Law supplying the required proposition
runtime-validated result
explicitly irrelevant behavior
```

An unknown implementation MUST NOT silently contribute arbitrary formal facts.

---

# 42. Templates

C++ templates retain ordinary C++ parsing, lookup, substitution, constraints,
overload resolution, instantiation and specialization semantics.

A C++L contract, refinement, Law or proof associated with a template is itself
parameterized by the template's semantic parameters. Proof obligations are
checked for the actual specialization unless they were already established by a
valid generic proof.

A C++ constraint such as `requires` controls C++ template viability; it is not by
itself a C++L theorem. Conversely, C++L proof evidence does not alter C++ overload
resolution unless ordinary C++ source semantics independently do so.

At an instantiation point, the verifier must have the formal metadata required by
the specialization: contracts, refinement definitions, Laws/proofs, purity,
termination/effect summaries and trust dependencies. Header definitions, modules,
explicit-instantiation metadata or another semantically equivalent transport may
provide it.

Evidence for one specialization MUST NOT be reused for another unless a checked
generic derivation justifies that reuse.

---

# 43. Namespaces, classes and formal scope

C++L declarations participate in lexical scope while referenced C++ entities use
ordinary C++ name lookup unless a formal binder explicitly shadows a name.

Laws and proofs may appear at namespace scope. Class-scope Laws follow §10.6.
Proof binders, quantifier binders, case binders and induction binders have lexical
proof scope and MUST NOT escape it.

Formal declaration identity is semantic rather than spelling-only: aliases,
qualified names, overload resolution and template specialization are resolved
through the corresponding C++ entity model before C++L attaches verification
metadata.

An unnamed namespace gives Laws/proofs translation-unit-local identity just as it
does for ordinary C++ entities.

---

# 44. Declarations, headers and translation units

C++L uses ordinary supported C++ source/header organization. Public verification
interfaces normally place contracts, shared refinements and reusable Laws/proofs
where callers can see them.

A public verified declaration may carry the contract while its matching
out-of-line definition carries only the ordinary C++ function body:

```cpp
// account.hpp
verified unsigned withdraw(unsigned balance, unsigned amount)
    expects (amount <= balance)
    ensures (result == balance - amount);

// account.cpp
unsigned withdraw(unsigned balance, unsigned amount)
{
    return balance - amount;
}
```

The declaration and definition are one Clang-resolved function entity. The
contract belongs to that entity. Repeated contracts, when present, must be
semantically identical after parameter renaming and normal resolution;
conflicting contracts are ill-formed.

A declaration alone does not prove its implementation. A caller may rely on its
verified summary only when checked implementation evidence or an explicit trusted
proposition establishes the required summary.

Across translation units, the verification interface must preserve contracts,
refinement identities/predicates, Law/proof evidence, purity, totality, effect
summaries and trust dependencies needed by callers. This metadata is independent
of native ABI symbols.

---

# 45. Modules

When the selected C++ mode supports modules, ordinary module parsing, ownership,
visibility and import semantics remain C++ semantics.

C++L verification metadata associated with exported declarations must be
available to importing verification contexts with the same meaning it has in the
defining module. Import does not weaken contracts, erase trust dependencies or
create proof evidence that was absent from the exported verification interface.

C++L introduces no separate module syntax solely for proof metadata.

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

merely because execution reaches that point without failure.

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

# 52. Complete-conformance and fail-closed rule

This specification defines the target language independently of implementation
progress.

A complete conforming implementation MUST implement every required construct and
semantic rule defined by this specification and the normative grammar. It may use
any sound internal architecture, solver or proof automation consistent with those
semantics.

A particular build or tool may be incomplete during development, but such
incompleteness is status, not language meaning. Missing support MUST NOT:

```text
weaken a Law or contract
change canonical syntax
invent proof evidence
silently introduce trust
silently introduce runtime checks
change erasure or ABI rules
reinterpret a required feature as optional
```

For a program outside the semantics expressly modeled by the language, or for a
proof obligation that cannot be established, verification fails closed. Failure
to implement a feature required by the language is incomplete conformance with
this specification.

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

A complete conforming C++L implementation MUST:

1. preserve valid supported C++ source compatibility when no C++L semantics are requested;
2. preserve ordinary C++ runtime semantics and the selected C++ object/memory model;
3. implement the contextual C++L grammar without globally reserving contextual words;
4. implement Laws, `proof`, `proves`, proof statements and kernel-checkable evidence;
5. implement universal and existential propositions with sound introduction and elimination;
6. implement definitional/propositional equality, logical connectives and substitution soundly;
7. implement `verified` contracts, path-sensitive reasoning, normal-return post-state and call composition;
8. implement refinement and indexed/dependent refinement obligations at every defined crossing;
9. implement purity checking and keep purity distinct from termination and `const`;
10. implement proof-side `cases`, `decompose` and `induction` with exhaustive, sound state partitions;
11. implement loop invariants and `decreases` termination obligations for the loop forms defined here;
12. implement partial/total correctness distinctions and recursion termination semantics;
13. implement the storage, alias, lifetime, pointer-capability and effect-summary rules needed by verified memory operations;
14. implement member, constructor, destructor and virtual-override contract semantics;
15. preserve template, namespace, class, translation-unit and module verification identity across composition;
16. preserve selected C++ machine arithmetic, floating-point and undefined-behavior semantics;
17. handle exceptions and concurrency without unsound sequential or no-throw assumptions;
18. preserve explicit `trusted law` and `unsafe` boundaries and their distinct meanings;
19. preserve the distinction between static proof, trust, runtime validation, unsafe and unverified code;
20. reject invalid proof evidence and fail closed when required facts cannot be established;
21. prevent proof-only and ghost state from affecting runtime behavior;
22. erase verification-only constructs without changing required runtime behavior;
23. preserve native ABI where this specification promises erasure-level ABI identity;
24. avoid automatic runtime assertion/validation substitution for failed proofs;
25. transport sufficient verification metadata for sound cross-translation-unit and module reasoning;
26. preserve trust dependency closure in derived proof evidence and reports; and
27. never weaken a Law, contract, refinement, memory obligation or termination obligation to make a program verify.

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
a required C++L verification runtime library
a required C++L runtime-validation library
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
verified unsigned divide(unsigned x, unsigned y)
    expects (y != 0u)
{
    return x / y;
}
```

The verified call domain excludes:

```text
y == 0uu
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

`GRAMMAR.md` defines the concrete grammar corresponding to this specification.
`DEVELOPER_GUIDE.md` is explanatory usage material and MUST remain consistent with
this specification. `FOUNDATIONS.md` formalizes the proof model; `TRUST.md`
defines the trusted-computing-base and correspondence obligations.

Examples, tutorials, implementation code and status documents MUST NOT establish
an alternate language dialect. When they disagree with this specification, they
must be corrected rather than used to weaken the normative target.
