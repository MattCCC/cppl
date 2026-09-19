# C++L Language Specification

**C++L — C++ with Laws**

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
ensures(x >= 0)
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

# 8. Universal quantification

Parameters of a `law` are universally quantified unless explicitly stated otherwise.

For example:

```cpp
law nonnegative_square(int x)
    ensures(square(x) >= 0);
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

The basic form is:

```cpp
law name(parameters...)
    expects(precondition)
    ensures(postcondition);
```

A Law MUST contain at least one proposition to establish.

Multiple `expects` clauses are conjoined.

Multiple `ensures` clauses are conjoined.

---

## 10.2 Law semantics

For:

```cpp
law L(T x)
    expects(P(x))
    ensures(Q(x));
```

the meaning is conceptually:

```text
∀ x : T,
    P(x) → Q(x)
```

With no `expects` clause:

```cpp
law L(T x)
    ensures(Q(x));
```

means:

```text
∀ x : T,
    Q(x)
```

---

## 10.3 Laws are not axioms

Declaring:

```cpp
law L(...)
    ensures(...);
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
expects(condition)
```

Example:

```cpp
int divide(int x, int y)
    expects(y != 0)
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
ensures(condition)
```

Example:

```cpp
int abs_value(int x)
    ensures(result >= 0)
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
    ensures(result == x)
{
    return x;
}
```

`result` is not globally reserved.

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
    expects(amount >= 0)
    expects(amount <= account.balance)
    ensures(account.balance == old(account.balance) - amount)
{
    account.balance -= amount;
}
```

`old(expression)` is a formal snapshot.

It does not imply that a runtime copy must be created.

---

## 11.5 Multiple clauses

Multiple preconditions are conjoined:

```cpp
expects(A)
expects(B)
```

means:

```text
A ∧ B
```

Multiple postconditions are also conjoined.

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
expects(P)
```

requires proof that `P` holds at the call site.

An ordinary unverified caller is not automatically rejected merely because it cannot statically prove the precondition.

This preserves incremental adoption.

---

## 11.8 Contracts are compile-time specifications

`expects` and `ensures` do not automatically generate runtime checks.

Failure to prove a required contract MUST NOT silently be transformed into:

```cpp
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
    ensures(result == x)
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
    expects(P)
    ensures(Q)
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
Substitution MUST avoid variable capture. `result` is a specification binding
of type `T`; it MUST NOT introduce a runtime variable, parameter, or computation.
The return expression MUST be checked even when `Q` does not mention `result`.

The body MUST NOT be replaced by an assumed summary or a `body_semantics` axiom.
Every generated obligation requires evidence accepted by the existing kernel.
This fragment adds zero kernel rules and zero logical assumptions.

The initial implementation accepts namespace-scope functions with an explicit
built-in integer return type, integer value parameters, one `ensures` equality,
and at most one `expects` equality. Bodies contain exactly one return of a
modeled pure expression: parameters, integer literals, unsigned addition,
subtraction and multiplication, or calls to admitted pure definitions or
verified functions under section 12.6. Unsigned `+`, `-` and `*` denote the
core's wrapping operations (section 7.1.1), which C++ defines them to be for
unsigned operands of one type (section 29.2). Clang remains authoritative for
overloads, integer widths, and conversions; unsupported conversions, signed
arithmetic, division, remainder, shifts and bitwise operators are rejected. Type
aliases are resolved by Clang.

Branches and multiple returns extend this fragment under section 12.7, and
straight-line locals and assignments under section 12.8.
Loops, references, pointers, floating point,
exceptions, side effects, recursion, and unsupported declarators are rejected.
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
and assignments are specified in section 12.8. Loops, switches, jumps,
exceptions, side effects, implicit type conversions, and trailing unreachable
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
reference, pointer, `volatile` or otherwise unmodeled type, an aggregate or
empty braced initializer, and a non-variable declaration are rejected. An
assignment MUST name a local of the same body, with a value of the same modeled
type. Assignment through a reference or pointer, to a parameter, and to
anything outside the body are rejected. C++ decides whether a `const` local may
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
expression that established it. A version is never an unknown: nothing is
assumed about a local. That expression MUST be modeled where the version is
established, whether or not any later expression reads it, because C++
evaluates it there: an unread signed overflow is still undefined behavior.
C++ scoping forbids a read before the declaration but
puts a local in scope within its own initializer; a read there, or anywhere
else no version of the local is current, MUST be rejected.

What follows a branch is verified once per arm, under the versions that arm
established, so a local's value after a branch is path-sensitive by
construction. This requires no merge operation and no additional kernel rule.

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

```cpp
proof name(parameters...)
    proves(proposition)
{
    proof_body
}
```

Example:

```cpp
proof identity_reflexive(int x)
    proves(Eq<int>(x, x))
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
    proves(Q(x))
```

means construction of:

```text
∀ x : T, Proof<Q(x)>
```

---

## 15.2 `proves`

The clause:

```cpp
proves(P)
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
type Percentage =
    int where(self >= 0 && self <= 100);
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

---

## 17.4 Runtime representation

Unless explicitly specified otherwise, a refinement type has the runtime representation of its base type.

Its refinement proof is erased.

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

# 18. Dependent types

C++L supports types whose formal meaning depends on values.

For example:

```cpp
type Index(std::size_t n) =
    std::size_t where(self < n);
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

```cpp
enum class State { idle, running, failed };

proof foo(State s)
    proves(...)
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

```cpp
using PaymentResult = std::variant<Receipt, Error>;

law settles(PaymentResult r)
    expects(!r.valueless_by_exception())
    ensures(...);

proof settles_holds(PaymentResult r)
    proves(settles(r))
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

# 21. Induction

`induction` proves a proposition for every value of a domain by applying that domain's induction principle.

Example:

```cpp
proof add_zero(unsigned x)
    proves(add(x, 0u) == x)
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

```cpp
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
    proves(add(x, 0u) == x)
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
decreases(expression)
```

Example:

```cpp
pure unsigned gcd(unsigned a, unsigned b)
    decreases(b)
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

---

# 24. Loop invariants

Imperative loops may carry formal invariants.

Example:

```cpp
while (condition)
    invariant(P)
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
decreases(measure)
```

The measure MUST strictly decrease on each continuing iteration under a well-founded ordering.

---

# 25. Ghost state

`ghost` declares proof-only state.

Example:

```cpp
ghost auto initial_balance = account.balance;
```

Ghost state may record symbolic information useful for proof.

---

## 25.1 Runtime erasure

Ghost state MUST be erased before runtime execution unless explicitly converted into ordinary runtime data.

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

A declaration MAY also be explicitly unsafe where defined by the grammar.

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

```cpp
trusted law operating_system_contract(...)
    ensures(...);
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

```cpp
law sorted_output_preserves_count(...)
    ensures(...);
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
    ensures(result == x)
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
    expects(y != 0)
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
    ensures(identity(x) == x);
```

The Law is a proposition.

Its declaration alone does not prove it.

---

## 63.5 Explicit proof

```cpp
proof integer_reflexivity(int x)
    proves(Eq<int>(x, x))
{
    refl;
}
```

---

## 63.6 Refinement

```cpp
type Percentage =
    int where(self >= 0 && self <= 100);
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

```cpp
trusted law external_api_contract(...)
    ensures(...);
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
