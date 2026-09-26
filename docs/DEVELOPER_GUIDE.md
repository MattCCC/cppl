# Writing C++L

C++L adds checked contracts, Laws, proofs and refinements to ordinary C++.

**Functions ensure. Laws prove.** Runtime bodies remain C++; specifications and
proofs are checked before erasure and native compilation.

[SPEC.md](./SPEC.md) defines meaning and [the grammar](./GRAMMAR.md) defines
syntax. This guide uses their canonical spelling. [STATUS.md](./STATUS.md) records
verification coverage; a compiler that cannot check a construct must reject it,
not accept its proposition as an assumption. Verification limitations do not
create alternate syntax.

## 1. What runs and what erases

| Construct                                     | Runtime behavior                  | Erasure                                                                    |
| --------------------------------------------- | --------------------------------- | -------------------------------------------------------------------------- |
| Ordinary C++ body                             | Executes normally                 | Preserved                                                                  |
| `verified`                                    | Function body executes            | Verification modifier/metadata removed                                     |
| `pure`                                        | Function body executes            | Verification modifier/metadata removed                                     |
| `expects`                                     | None                              | Removed                                                                    |
| `ensures`                                     | None                              | Removed                                                                    |
| `proves`                                      | None                              | Removed                                                                    |
| `law`                                         | None                              | Entire declaration removed                                                 |
| `proof`                                       | None                              | Entire declaration removed                                                 |
| `refl`                                        | None                              | Removed with proof                                                         |
| `exact`                                       | None                              | Removed with proof                                                         |
| `apply`                                       | None                              | Removed with proof                                                         |
| `assume`                                      | None                              | Removed with proof                                                         |
| `rewrite`                                     | None                              | Removed with proof                                                         |
| `contradiction`                               | None                              | Removed with proof                                                         |
| `contradiction` in a verified body            | Empty statement                   | Words removed; the `;` stays, so control flow is unchanged                 |
| `omit ... by contradiction ...`               | None                              | Removed with the `cases` statement                                         |
| `forall`                                      | None                              | Removed with specification/proof                                           |
| `exists`                                      | None                              | Removed with specification/proof                                           |
| `cases`                                       | None                              | Removed with proof                                                         |
| `decompose`                                   | None                              | Removed with proof                                                         |
| `cases`/`decompose` in a verified body        | Empty statement                   | Statement removed, arms included; a `;` stays where its `}` was            |
| `induction`                                   | None                              | Removed with proof                                                         |
| `ghost` local                                 | None                              | Removed                                                                    |
| `type ... where (...)` refinement declaration | Base C++ representation only      | Predicate and refinement identity removed/lowered                          |
| Refinement indices                            | None                              | Removed                                                                    |
| `self` in refinement predicates               | None                              | Removed with predicate                                                     |
| `result` in postconditions                    | None                              | Removed with postcondition                                                 |
| `old(...)` in postconditions                  | None                              | Removed with postcondition                                                 |
| `invariant`                                   | None                              | Clause removed; runtime loop retained                                      |
| `decreases`                                   | None                              | Clause removed; runtime function/loop retained                             |
| `trusted law`                                 | None                              | Assumption retained in verification/trust metadata; erased from executable |
| `unsafe` function/block marker                | Marked runtime operation executes | Marker removed; runtime operations retained                                |
| Explicit runtime validation                   | Executes                          | Preserved                                                                  |

A contract is not a hidden runtime assertion. A failed proof is a compilation
error. Tests, solver output and AI suggestions cannot replace kernel-checked
evidence. `TRUSTED`, `UNSAFE`, `RUNTIME-CHECKED` and `PROVEN` are distinct statuses.

A useful mental model is:

```text
ordinary C++ body
    -> runtime

verified contract
    -> compile-time obligation

law / proof
    -> compile-time theorem/evidence only

forall / exists
    -> compile-time logical quantification only

refl / exact / apply / assume / rewrite
    -> compile-time proof commands only

cases / decompose / induction
    -> compile-time proof structure only

ghost
    -> verification-only state

refinement
    -> base C++ runtime representation + erased verification predicate

runtime validation
    -> runtime check that may establish evidence for verified code
```

## 2. Verified functions

Put a contract after the complete C++ declarator. Each clause has parentheses,
one space before `(` in canonical formatting, and its own continuation line.
The order is `expects`, `ensures`, `decreases`, with at most one of each.

Whitespace between a clause keyword and `(` is not semantically significant.
The formatter canonicalizes both `ensures(...)` and `ensures (...)` to:

```cpp
ensures (...)
```

<!-- cppl-example: verify -->

```cpp
verified int identity(int x)
    ensures (result == x)
{
    return x;
}
```

`expects` states the caller's precondition; `ensures` states the normal-return
postcondition. A function that returns a value states it with `ensures`; a
`void` function may state only `expects`, and still has to satisfy its body's
obligations. Here `advance` owes the precondition of the call it makes, and its
own precondition supplies it:

<!-- cppl-example: verify -->

```cpp
verified unsigned next(unsigned x)
    expects (x < 100u)
    ensures (result == x + 1u)
{
    return x + 1u;
}

verified void advance(unsigned& counter)
    expects (counter < 100u)
{
    counter = next(counter);
}

verified unsigned withdraw(unsigned balance, unsigned amount)
    expects (amount <= balance)
    ensures (result == balance - amount)
{
    return balance - amount;
}
```

A contract applies to every normal return, including early returns:

<!-- cppl-example: verify -->

```cpp
verified unsigned bounded(unsigned x)
    ensures (result <= 10u)
{
    if (x > 10u) {
        return 10u;
    }

    return x;
}
```

A private helper can carry its contract on the definition:

<!-- cppl-example: verify -->

```cpp
static verified int normalize(int x)
    expects (x >= 0)
    ensures (result >= 0)
{
    return x;
}
```

Ordinary C++ prefix specifiers come first, followed by `verified pure`, then the
return type. `inline verified pure`, `constexpr verified pure`,
`consteval verified pure`, and `virtual verified` follow that rule wherever
C++ permits the corresponding declaration. Attributes, trailing return types,
`const`, `noexcept`, reference qualifiers, `override` and `final` retain their
C++ placement. `ghost`, `trusted` and `unsafe` are separate constructs, not
interchangeable flags on a verified function.

### 2.1. Calling verified functions

Verified functions compose through their contracts.

A caller must establish every callee `expects`. After a successful call, the
callee's checked `ensures` becomes available as evidence about the returned
value and post-state.

<!-- cppl-example: verify -->

```cpp
verified unsigned bump_zero(unsigned x)
    expects (x == 0u)
    ensures (result == 1u)
{
    return x + 1u;
}

verified unsigned bump_one(unsigned x)
    expects (x == 1u)
    ensures (result == 2u)
{
    return x + 1u;
}

verified unsigned two_from_zero(unsigned x)
    expects (x == 0u)
    ensures (result == 2u)
{
    return bump_one(bump_zero(x));
}
```

The reasoning is:

```text
x == 0
    |
    v
bump_zero(x)
    ensures result == 1
    |
    v
bump_one(1)
    expects input == 1
    |
    v
result == 2
```

The contracts disappear before runtime compilation. Both function calls remain
ordinary runtime calls.

A caller that cannot prove a precondition is rejected:

```cpp
verified unsigned invalid_call(unsigned x)
{
    return bump_zero(x);
}
```

Nothing establishes `x == 0u`, so the call obligation cannot be discharged.

This is one of the most common C++L patterns:

```text
caller facts
    -> callee expects
    -> call
    -> callee ensures
    -> new caller facts
```

### 2.2. Calling verified code from ordinary C++

`verified` does not insert runtime precondition checks.

Consider:

```cpp
verified unsigned withdraw(unsigned balance, unsigned amount)
    expects (amount <= balance)
    ensures (result == balance - amount)
{
    return balance - amount;
}
```

A verified caller must prove:

```text
amount <= balance
```

before making the call.

Ordinary unverified C++ can still call the erased runtime function. C++L does not
silently insert:

```cpp
assert(amount <= balance);
```

or any equivalent check.

Therefore:

```text
verified caller
    -> must prove `expects`

ordinary C++ caller
    -> no compile-time C++L proof unless that caller is also verified

runtime caller outside C++L verification
    -> no hidden contract enforcement
```

If a public API must reject invalid runtime input, add explicit runtime validation
at the boundary.

For example, conceptually:

```cpp
bool try_withdraw(
    unsigned balance,
    unsigned amount,
    unsigned& result)
{
    if (amount > balance) {
        return false;
    }

    result = withdraw(balance, amount);
    return true;
}
```

The validation executes at runtime. The verified `withdraw` contract does not.

This distinction is fundamental:

```text
expects (...)
    = proof obligation

runtime validation
    = executable check
```

Do not use a compile-time contract where the application actually requires a
runtime security, protocol or input-validation check.

### 2.3. Calling ordinary or unverified functions from verified code

A verified function may not invent semantics for an ordinary function call.

Suppose:

```cpp
unsigned external_value();
```

and:

```cpp
verified unsigned use_external()
    ensures (result <= 100u)
{
    return external_value();
}
```

The declaration of `external_value` alone does not prove that its result is at
most `100u`.

Verified code can rely on an external call only through information that C++L can
soundly justify, such as:

```text
a checked verified contract
a checked pure/formal model
explicit runtime validation
an explicit trusted boundary
```

If a call may mutate storage, the verifier must also account for those effects.

An unmodeled call must never become an implicit theorem merely because ordinary
C++ permits the call.

The general rule is:

```text
ordinary C++ says:
    "this call is well-typed"

C++L additionally asks:
    "what may this call return or mutate, and what evidence justifies that?"
```

If the verifier cannot answer the second question soundly, verification fails or
the relevant facts are conservatively invalidated.

### 2.4. Contracts and exceptional exits

`ensures (...)` describes a normal return.

For example:

```cpp
verified int f()
    ensures (result > 0)
{
    ...
}
```

means that every successfully completed normal return must satisfy:

```text
result > 0
```

It does not automatically describe an exception path.

After a potentially throwing call, a postcondition is available only on the path
where that call completed normally.

`noexcept` retains its ordinary C++ meaning. C++L does not reinterpret it as a
proof annotation.

If exception behavior cannot be modeled soundly by the verifier, verification of
that path must be rejected rather than treating the operation as non-throwing.

## 3. `result`, `old` and normal post-state

`result` denotes the returned value only in a non-void function postcondition.
A Law has no result. A void function describes state changes instead:

<!-- cppl-example: verify -->

```cpp
verified void clear(unsigned& value)
    ensures (value == 0u)
{
    value = 0u;
}
```

`old(expression)` denotes the value at function entry. It is a specification
snapshot, not a runtime copy:

```cpp
verified void increment(unsigned& value)
    expects (value < 100u)
    ensures (value == old(value) + 1u)
{
    ++value;
}
```

Snapshot syntax is legal only in function postconditions. The expression must
be well-defined in the entry state; it cannot use `result` or another `old`.

Normal parameter/member observations in the postcondition describe the normal
post-state. Preconditions refer to entry state. An exceptional exit is not a
normal return and does not acquire an invented exception guarantee.

| Identifier        | Special meaning and scope             | Outside that scope     |
| ----------------- | ------------------------------------- | ---------------------- |
| `result`          | Returned value in non-void `ensures`  | Ordinary C++ name      |
| `old(expression)` | Entry value in function `ensures`     | Ordinary C++ call/name |
| `self`            | Candidate value in refinement `where` | Ordinary C++ name      |

Members use C++ `this` and ordinary member lookup; `self` is not a second spelling
for the implicit object. Ordinary C++ remains valid:

<!-- cppl-example: verify -->

```cpp
int result = 0;
int self = 1;

int old(int value)
{
    return value;
}
```

Clause-like C++L syntax is canonically formatted with a space:

```cpp
expects (...)
ensures (...)
proves (...)
invariant (...)
decreases (...)
where (...)
```

Expression-like `old` remains function-like:

```cpp
old(value)
```

## 4. Laws

A Law is a compile-time theorem. Parameters are universally quantified, an
optional `expects` is its premise, and `proves` is its conclusion.

<!-- cppl-example: verify -->

```cpp
law addition_identity(unsigned x)
    proves (x + 0u == x);

law subtraction_cancels(unsigned balance, unsigned amount)
    expects (amount <= balance)
    proves ((balance - amount) + amount == balance);
```

The semicolon requests automatic proof. If automation cannot produce evidence
accepted by the kernel, compilation fails. A declaration does not create an
axiom.

Write a proof body when explicit evidence is useful:

<!-- cppl-example: verify -->

```cpp
law equality_is_reflexive(int x)
    proves (Eq<int>(x, x))
{
    refl;
}

law equality_reused(int x)
    proves (Eq<int>(x, x))
{
    apply equality_is_reflexive(x);
}
```

The two Law forms therefore mean:

```text
law ... proves (...);
    -> theorem whose proof is discharged automatically

law ... proves (...) {
    ...
}
    -> theorem with an explicitly authored proof body
```

Both forms are compile-time-only.

A Law can express a property without calling a runtime implementation. A Law
mentioning a function needs that function's declaration and a checked formal
model. Do not write a function name into a theorem as if spelling alone supplied
its semantics.

Common mistake:

```cpp
law wrong(int x)
    ensures (x == x);
```

This is rejected. Use `proves`, not `ensures`.

Merely changing the word cannot repair a Law that also refers to an undefined
return-value `result`.

### 4.1. Law versus proof

`law` and `proof` are both compile-time-only, but they serve different roles.

A Law names the proposition developers want to rely on:

```cpp
law addition_identity(unsigned x)
    proves (x + 0u == x);
```

A named proof constructs reusable evidence:

```cpp
proof addition_identity_evidence(unsigned x)
    proves (x + 0u == x)
{
    refl;
}
```

Think of them as:

```text
law
    = theorem / proposition API

proof
    = named evidence artifact
```

A Law may contain its proof directly:

```cpp
law reflexive(int x)
    proves (Eq<int>(x, x))
{
    refl;
}
```

In that case a second named `proof` is unnecessary unless separately named
evidence is useful elsewhere.

Use a standalone `proof` when the evidence itself deserves a reusable name,
when building proof helpers, or when keeping the theorem statement separate
from a larger proof construction.

Neither form creates a runtime function.

This:

```cpp
proof pointer_states(int* pointer)
    proves (Eq<bool>(true, true))
{
    cases pointer {
        null => {
            refl;
        }

        non_null(address) => {
            refl;
        }
    }
}
```

does not produce a callable runtime symbol.

Its purpose is to give the verifier reusable checked evidence.

### 4.2. Laws and runtime verification

Laws may help discharge obligations generated while verifying runtime code.

They do not execute at runtime.

For example:

```cpp
pure unsigned identity_value(unsigned x)
{
    return x;
}

law identity_value_returns_input(unsigned x)
    proves (identity_value(x) == x);
```

A verified function may rely on the checked model of `identity_value` and the
visible Law while proving its own contract:

```cpp
verified unsigned checked_identity(unsigned x)
    ensures (result == x)
{
    return identity_value(x);
}
```

Conceptually:

```text
runtime body
    -> generates verification obligation

visible Laws / contracts / facts
    -> provide checked evidence

kernel
    -> validates the proof

native body
    -> runs without theorem machinery
```

A Law is never called as runtime code merely because its name looks like a
function.

## 5. Explicit proofs and logical expressions

A named `proof` constructs reusable evidence without a runtime function:

<!-- cppl-example: verify -->

```cpp
proof same(int x)
    proves (Eq<int>(x, x))
{
    refl;
}

proof use_same(int x)
    proves (Eq<int>(x, x))
{
    exact same(x);
}
```

| Statement             | Meaning                                               |
| --------------------- | ----------------------------------------------------- |
| `refl;`               | Close a definitionally reflexive equality             |
| `exact same(x);`      | Close the goal with existing evidence                 |
| `apply same(x);`      | Apply evidence; discharge its premises                |
| `assume h : x == 0u;` | Name a matching context-supplied premise              |
| `rewrite h;`          | Rewrite the goal left-to-right using checked equality |

Commands are statements, not calls such as `exact(same);`.

An evidence reference may itself have arguments. It names a `proof`, a premise
named by `assume`, or a `trusted law`, which the proof then rests on (§12.2).

`assume` never asserts an arbitrary proposition:

<!-- cppl-example: verify -->

```cpp
law given_zero(unsigned x)
    expects (x == 0u)
    proves (x + 0u == 0u)
{
    assume h : x == 0u;
    rewrite h;
    refl;
}
```

`h` is available because the Law supplies the premise. Without that premise,
this `assume` is rejected. Scope and matching are checked, including nested arms.

Use ordinary Boolean predicates for modeled C++ conditions, and `Eq<T>(a, b)`
for formal equality. Definitional equality comes from specified computation;
propositional equality requires evidence. `&&`, `||`, `->`, and `<->` compose
formal propositions in specification contexts.

<!-- cppl-example: verify -->

```cpp
law every_value_equals_itself()
    proves (forall (unsigned x) { Eq<unsigned>(x, x) });
```

An existential proposition uses the same binder/block shape:

```cpp
law a_zero_exists()
    proves (exists (unsigned x) { x == 0u });
```

A proof of existence needs explicit checked witness evidence according to the
proof grammar. Absence of a counterexample is insufficient.

Quantifiers produce no runtime loops. C++ pointer member access inside a formal
proposition is parenthesized where necessary so `->` is not confused with formal
implication.

The mathematical domain names are `@N`, `@Z`, `@Seq<T>`, `@Set<T>` and
`@Map<K, V>`. They are proof-only and do not rename C++ machine integers:

```cpp
law mathematical_identity(@Z x)
    proves (Eq<@Z>(x, x));

law sequence_identity(@Seq<int> xs)
    proves (Eq<@Seq<int>>(xs, xs));

law set_identity(@Set<int> xs)
    proves (Eq<@Set<int>>(xs, xs));

law map_identity(@Map<int, int> xs)
    proves (Eq<@Map<int, int>>(xs, xs));
```

There is no implicit conversion from an unbounded mathematical result to a
machine result. Signed overflow and invalid memory operations remain proof
obligations, even when an idealized mathematical identity would hold.

### 5.1. Choosing a proof command

The commands are easiest to understand by looking at the current goal.

Use `refl` when the goal reduces definitionally to equality with itself:

```cpp
proof reflexive(unsigned x)
    proves (Eq<unsigned>(x, x))
{
    refl;
}
```

Use `exact` when you already have evidence whose proposition exactly matches
the current goal:

```cpp
proof same(unsigned x)
    proves (Eq<unsigned>(x, x))
{
    refl;
}

proof same_again(unsigned x)
    proves (Eq<unsigned>(x, x))
{
    exact same(x);
}
```

Conceptually:

```text
goal: P

available evidence:
    e : P

exact e;
    -> goal closed
```

Use `apply` when existing evidence can be used to reduce the current goal to
its remaining premises.

```text
goal: Q

theorem:
    P -> Q

apply theorem;
    -> remaining goal: P
```

Use `rewrite` when an established equality lets the verifier replace one side
inside the current goal:

```cpp
law zero_identity(unsigned x)
    expects (x == 0u)
    proves (x + 0u == 0u)
{
    assume h : x == 0u;
    rewrite h;
    refl;
}
```

Use `contradiction` when the evidence you name cannot hold together with the
premises standing where you write it. The goal is then closed whatever it says:

```cpp
pure unsigned zero() {
    return 0u;
}

law nothing_reaches_a_false_premise(unsigned x)
    expects (zero() == 1u)
    proves (x == 7u)
{
    assume impossible : zero() == 1u;
    contradiction impossible;
}
```

`x == 7u` is false for most `x`, but no `x` reaches it, because no value makes
`zero() == 1u` true. The contradiction is established from the premises alone,
before the goal is looked at, and the kernel checks it. A premise that has merely
not been proven is not a contradiction, and neither is a failure to find a state
that reaches the goal.

The goal's shape does not matter. The contradiction is evidence for `False`, and
the kernel closes any goal from that, so `Eq<Pair>(p, q)` for two arbitrary
records closes exactly as `x == 7u` does.

The same statement written in a verified function's body claims that no
execution reaches it:

```cpp
proof nothing()
    proves (zero() == 0u)
{
    refl;
}

verified unsigned below_five(unsigned x)
    expects (x < 5u)
    ensures (result < 5u)
{
    if (x >= 5u) {
        contradiction nothing;
    }
    return x;
}
```

The precondition and the branch condition cannot both hold, so the claim is
proven from them, and the path ends there: nothing written after the claim on
that path is verified or needs to be. The facts a claim is checked against are
everything established on the path to it: preconditions, branch conditions,
loop invariants and the postconditions of verified calls already made. A claim
that some execution can reach is an error, never an assumption. At runtime the
claim is an empty statement. If your translation unit uses the name
`contradiction` for anything else, the statement is ordinary C++ and the
compiler warns that it is not a claim.

Use `cases` when a proof depends on which state a value occupies.

Use `induction` when the proof depends on a recursively smaller predecessor and
needs an induction hypothesis.

A useful decision table is:

```text
goal is definitionally obvious
    -> refl

already have evidence for exactly the goal
    -> exact

have a theorem that can reduce the goal to premises
    -> apply

have an equality that should transform the goal
    -> rewrite

have evidence the premises here cannot hold together with
    -> contradiction

a path in a verified body cannot be taken
    -> contradiction, written as a statement of the body

need to split finite/logical states
    -> cases

need recursive reasoning with a smaller predecessor
    -> induction
```

### 5.2. Logical quantifiers: `forall` and `exists`

`forall` and `exists` are C++L specification constructs for quantified logical
statements. They are not C++ runtime loops and they generate no runtime code.

Use `forall` when a proposition must hold for every value in a domain:

```cpp
law every_unsigned_equals_itself()
    proves (forall (unsigned x) { Eq<unsigned>(x, x) });
```

Conceptually:

```text
forall (unsigned x) { P(x) }
```

means:

```text
for every unsigned x, P(x) holds
```

Use `exists` when the proposition requires at least one witness:

```cpp
law zero_exists()
    proves (exists (unsigned x) { x == 0u });
```

Conceptually:

```text
exists (unsigned x) { P(x) }
```

means:

```text
there is at least one x for which P(x) holds
```

Both forms are proof-only:

```text
forall
    -> compile-time logical quantifier
    -> erased before runtime

exists
    -> compile-time logical quantifier
    -> erased before runtime
```

They do not enumerate runtime values.

#### Quantifier domains are determined by their types

The binder type matters.

This:

```cpp
forall (unsigned x) {
    P(x)
}
```

quantifies over the values of the C++ `unsigned` machine type.

It does not mean mathematical natural numbers.

Likewise:

```cpp
forall (@N n) {
    P(n)
}
```

quantifies over mathematical natural numbers, while:

```cpp
forall (@Z z) {
    P(z)
}
```

quantifies over mathematical integers.

Therefore these domains are different:

```text
unsigned
    -> finite C++ machine domain

@N
    -> unbounded mathematical natural numbers

@Z
    -> unbounded mathematical integers
```

Machine arithmetic retains its C++ semantics inside machine-typed propositions.
For example, unsigned arithmetic wraps according to the C++ machine model.

Mathematical domains use their specified mathematical semantics.

Never silently transfer a theorem between a machine domain and an unbounded
mathematical domain without a checked conversion or relation.

Nested quantifiers are allowed where the logical model supports them:

```cpp
law equality_is_symmetric()
    proves (
        forall (int x) {
            forall (int y) {
                Eq<int>(x, y) -> Eq<int>(y, x)
            }
        }
    );
```

An existential proposition requires witness evidence. It is not enough to show
that no contradiction was found.

A useful mental model is:

```text
forall
    = introduce an arbitrary value and prove the property

exists
    = provide a witness and prove the property for that witness
```

Quantified variables are proof/specification binders. They do not introduce
runtime storage, allocation, lifetime or ABI-visible state.

## 6. Refinement types

A refinement restricts an existing C++ type. `self` is the value being refined:

<!-- cppl-example: verify -->

```cpp
type NonNegative = int where (self >= 0);

type Percentage = NonNegative where (self <= 100);

verified Percentage half()
    ensures (result == 50)
{
    Percentage value = 50;
    return value;
}

verified Percentage unchanged(Percentage value)
    ensures (result == value)
{
    return value;
}
```

`Percentage` has verification-level identity and the runtime representation of
`int`. Erasure produces the equivalent of `using Percentage = int;`, not a
wrapper, tag, allocation or hidden check.

Nested refinements require every predicate inherited from the base.

Every introduction needs evidence: local initialization, call argument, return,
assignment, member/element write and verified call effect.

Branch facts can establish the predicate:

<!-- cppl-example: verify -->

```cpp
type Positive = int where (self > 0);

verified Positive positive_or_one(int x)
{
    if (x > 0) {
        Positive value = x;
        return value;
    }

    return 1;
}
```

A refined return supplies its membership obligation without a duplicate
`ensures`.

A stronger refinement may be used where a weaker one is required only when
implication is proven. An arbitrary base value does not acquire a refinement by
conversion or spelling.

Rejected introduction:

```cpp
type Positive = int where (self > 0);

verified Positive unproved(int x)
{
    Positive value = x;
    return value;
}
```

The verifier needs `x > 0`; no such fact is available.

Writes create a new logical value version. Possible alias mutation invalidates
facts about the old version. A const reference does not make the aliased object
globally immutable:

```cpp
type Positive = int where (self > 0);

verified void set_one(Positive& value)
    ensures (value == 1)
{
    value = 1;
}
```

A later `value = 0` would fail the refinement crossing. Calls can restore a fact
only through a checked postcondition. Repeated actual aliases share a post-state.

A refined member likewise uses the common storage rules:

```cpp
type Positive = int where (self > 0);

struct Counter {
    Positive value;
};

verified int initial_count()
    ensures (result == 1)
{
    Counter counter{1};
    return counter.value;
}
```

Indexed refinements declare typed indices with parentheses and apply them with
angle brackets:

<!-- cppl-example: verify -->

```cpp
type Index(unsigned n) = unsigned where (self < n);

verified Index<4u> first_index()
{
    return 0u;
}
```

The index is in scope in the predicate; `self` is the base value. Index metadata
has no runtime representation. A dependent application such as `Index<N>` follows
ordinary C++ template substitution; the refined base declaration must be visible
at instantiation.

There is one declaration/application spelling, not a second C++ template system.

A trusted proposition about a value does not silently validate external input.
Only an explicit trusted boundary or a retained runtime validator can supply the
corresponding entry evidence. Such trust remains in the report.

### 6.1. Refinement lifecycle

A useful way to think about a refinement is as a checked boundary:

```text
ordinary value
    |
    | prove predicate
    v
refined value
    |
    | use in verified code
    v
mutation
    |
    | predicate must hold again
    v
new refined value version
```

For example:

```cpp
type Positive = int where (self > 0);

verified Positive normalize_positive(int value)
{
    if (value > 0) {
        return value;
    }

    return 1;
}

verified int consume_positive(Positive value)
    ensures (result > 0)
{
    return value;
}

verified int process(int raw)
    ensures (result > 0)
{
    Positive value = normalize_positive(raw);
    return consume_positive(value);
}
```

The base `int` does not become `Positive` merely because a developer wants to
pass it to `consume_positive`.

The proof must happen at the crossing.

Stronger refinements may flow into weaker refinements when implication is
established:

```cpp
type NonNegative = int where (self >= 0);
type Positive = NonNegative where (self > 0);

verified NonNegative weaken(Positive value)
{
    return value;
}
```

The reverse direction requires evidence:

```cpp
verified Positive strengthen(NonNegative value)
{
    return value;
}
```

This is rejected unless the current context also proves `value > 0`.

### 6.2. Refinements at runtime and ABI boundaries

A refinement has verification identity but no extra runtime representation.

For example:

```cpp
type Positive = int where (self > 0);
```

erases to a representation equivalent to:

```cpp
int
```

at the native ABI boundary.

There is no hidden runtime tag saying:

```text
this int is Positive
```

and there is no hidden constructor performing validation.

This has an important consequence for external and unverified callers.

Suppose a public C++L API declares:

```cpp
verified int consume(Positive value)
    ensures (result > 0);
```

The verifier can rely on the refinement when a checked C++L caller proves the
crossing.

But after erasure, the native ABI receives an `int`.

Code outside the verified C++L boundary must therefore not be assumed to have
proved:

```text
value > 0
```

merely because the source-level declaration used `Positive`.

At an FFI, plugin, network, deserialization, C API or other unverified boundary,
use:

```text
runtime validation
or
an explicit trusted boundary
```

before treating the incoming base representation as a refinement.

The rule is:

```text
refinement spelling
    != runtime validation

refinement erasure
    != runtime type check

verified crossing
    = proof that the predicate holds
```

This is also why refinements do not create distinct native overload identities.

### 6.3. Refinement mutation and alias invalidation

Refinement facts belong to logical value versions, not permanently to variable
names.

For example:

```cpp
type Positive = int where (self > 0);

verified void set_one(Positive& value)
{
    value = 1;
}
```

the new stored value must satisfy:

```text
1 > 0
```

A write such as:

```cpp
value = 0;
```

is rejected because it would establish a new value version that does not satisfy
the declared refinement.

Aliasing also matters.

Consider:

```cpp
verified void mutate(int& value);

verified int example(Positive& positive, int& alias)
{
    mutate(alias);
    return positive;
}
```

If `alias` may refer to the same storage as `positive`, a call that can mutate
`alias` may invalidate previously known facts about `positive`.

The verifier must therefore conservatively invalidate facts about any storage
that the call may modify through an alias.

A `const` reference does not make the underlying object globally immutable:

```cpp
const int& view = value;
int& writer = value;
```

Mutation through `writer` changes what `view` observes.

C++L must reason about storage identity and possible aliases, not merely variable
spelling.

The practical rule is:

```text
read
    -> may use facts about the current logical version

write
    -> creates a new logical version

possible alias write
    -> invalidates facts that may refer to that storage
```

A postcondition may establish new facts after the mutation.

## 7. Pure functions

`pure` requests checked referential transparency. Its body remains ordinary C++:

<!-- cppl-example: verify -->

```cpp
pure unsigned same_value(unsigned value)
{
    return value;
}

verified pure unsigned checked_value(unsigned value)
    ensures (result == value)
{
    return value;
}
```

The canonical combination is `verified pure`.

A pure member uses explicit input and stable object state:

```cpp
struct Number {
    unsigned value;

    pure unsigned get() const
    {
        return value;
    }
};
```

Purity forbids observable mutation, I/O and calls whose effects are not admitted.

For example, this is rejected when relied upon as pure:

```cpp
unsigned global_count = 0u;

pure unsigned bump()
{
    return ++global_count;
}
```

`const` is an ordinary C++ qualifier; it is not by itself proof of purity or
freedom from alias mutation.

Purity also does not by itself prove termination.

### 7.1. `pure` is not `constexpr`, `const` or termination

These concepts are separate.

```text
pure
    -> no admitted observable side effects

const member function
    -> ordinary C++ restriction on access through `this`

constexpr
    -> ordinary C++ constant-evaluation capability

decreases
    -> termination proof

verified
    -> checked contract/body
```

For example:

```cpp
pure unsigned f(unsigned x)
{
    return x;
}
```

is still an ordinary runtime function.

`pure` does not mean that it executes at compile time.

Likewise:

```cpp
unsigned get() const;
```

is not automatically pure. It may observe mutable shared state, perform operations
through aliases, or call impure functions unless those effects are ruled out by
the verifier.

A pure function that may recurse forever is still not automatically terminating.
Termination requires the corresponding proof obligation.

## 8. Ghost locals

`ghost` prefixes a local declaration in the body of a verified function. Its
value exists only for verification, and the whole declaration leaves the
program.

There are no ghost runtime parameters, members or globals in this grammar, and a
proof body holds proof statements only, so there is no ghost declaration there
either.

A snapshot can support an invariant:

<!-- cppl-example: verify -->

```cpp
verified unsigned keep(unsigned x)
    ensures (result == x)
{
    ghost unsigned original = x;
    unsigned i = 0u;

    while (i < 3u)
        invariant (i <= 3u && x == original)
        decreases (3u - i)
    {
        ++i;
    }

    return x;
}
```

A ghost may also be computed from other ghosts and from `pure` functions, and a
claim's evidence may take one as an argument. Its initializer is read like a
specification expression, so it may have no effect: no assignment, increment,
allocation or volatile read, and no call to anything but a `pure` function. It
is an integer or a Boolean value, declared with a value, directly in a block
and outside every unsafe block; a class, pointer, reference or array ghost could
construct, destroy or alias runtime objects, and is refused.

Runtime values may be observed symbolically; proof-only values cannot flow back
into runtime behavior.

Rejected ghost leak:

```cpp
verified unsigned leaked(unsigned x)
    ensures (result == x)
{
    ghost unsigned snapshot = x;
    return snapshot;
}
```

The return is runtime behavior and would depend on erased state, so the
compiler refuses it where the ghost is used:

```text
error [cppl-syntax]: ghost 'snapshot' is used by code that runs
```

The same rule forbids every other use by code that runs, whether or not a path
reaches it:

```text
runtime branches and loop bounds
indices
arguments, including lambda captures
initializers of runtime objects
writes, and addresses
runtime return values
```

## 9. Cases and product decomposition

`cases` is proof-only state splitting. It produces no runtime `switch`, `if` or
`std::visit`.

Every representation uses the same `Label(bindings) => { }` arms.

Bindings are aliases or logical projections of the subject, never copied values.

Scoped enums include every distinct named value and the unnamed residual:

<!-- cppl-example: verify -->

```cpp
enum class Mode { idle, active };

proof mode_identity(Mode mode)
    proves (Eq<bool>(true, true))
{
    cases mode {
        Mode::idle => {
            refl;
        }

        Mode::active => {
            refl;
        }

        unnamed(value) => {
            refl;
        }
    }
}
```

A scoped enum's underlying integer domain contains values beyond its enumerators.

`unnamed` names that real residual state. It is not a wildcard.

Adding a distinct enumerator must break a proof that omitted its named arm; a
catch-all would hide that stale proof.

`_` is not a C++L proof catch-all.

Variant alternatives use indices, including when two alternatives share a type:

<!-- cppl-example: verify -->

```cpp
#include <variant>

proof variant_identity(std::variant<int, bool> value)
    proves (Eq<bool>(true, true))
{
    cases value {
        alternative<0>(number) => {
            refl;
        }

        alternative<1>(flag) => {
            refl;
        }

        valueless => {
            refl;
        }
    }
}
```

Optional payloads and nested decomposition use the same grammar:

<!-- cppl-example: verify -->

```cpp
#include <optional>

proof nested_optional(std::optional<std::optional<bool>> value)
    proves (Eq<bool>(true, true))
{
    cases value {
        some(inner) => {
            cases inner {
                some(flag) => {
                    refl;
                }

                none => {
                    refl;
                }
            }
        }

        none => {
            refl;
        }
    }
}
```

C++23 `std::expected` has value and error alternatives:

```cpp
#include <expected>

proof expected_identity(std::expected<unsigned, int> value)
    proves (Eq<bool>(true, true))
{
    cases value {
        value(payload) => {
            refl;
        }

        error(reason) => {
            refl;
        }
    }
}
```

Pointer decomposition states only nullness:

<!-- cppl-example: verify -->

```cpp
proof pointer_states(int* pointer)
    proves (Eq<bool>(true, true))
{
    cases pointer {
        null => {
            refl;
        }

        non_null => {
            refl;
        }
    }
}
```

`non_null` binds nothing: a pointer's only state beyond null is that it is not
null. It proves no:

```text
lifetime
bounds
provenance
initialization
ownership
writability
```

Product decomposition uses the separate product operation and the same arm
body/binder grammar:

<!-- cppl-example: verify -->

```cpp
struct Point {
    int x;
    int y;
};

proof coordinates(Point point)
    proves (Eq<bool>(true, true))
{
    decompose point {
        components(x, y) => {
            refl;
        }
    }
}
```

Providers expose the complete state space. The verifier, not the provider, may
prove an omitted state impossible under the context.

For example:

```cpp
law known_null(int* pointer)
    expects (pointer == nullptr)
    proves (Eq<bool>(true, true))
{
    assume is_null : pointer == nullptr;

    cases pointer {
        null => {
            refl;
        }

        omit non_null by contradiction is_null;
    }
}
```

A case goes without an arm only through an `omit` clause naming it
(`GRAMMAR.md` 5.7). Its evidence is checked under that case's own discriminator
premise, so omitting `non_null` requires checked evidence of contradiction
between the entry premise and that state's discriminator.

An omission is a claim about the case, not about the goal. The contradiction must
hold whatever the arm would have had to prove, so writing `omit null by
contradiction is_null;` above is refused, even though `refl` would close that
arm's goal: `pointer == nullptr` agrees with `null`, and nothing contradicts it.

There is no heuristic omission. Dropping the `omit` line above does not make the
case impossible; it makes the `cases` statement non-exhaustive, even though the
premise that would discharge it is in scope. The verifier never searches the
context to decide that a missing arm was intentional, so an accidental omission
cannot pass as a proved one.

If the verifier cannot establish that contradiction, the omission is an error
naming the omitted case. When it can, the omission is an obligation of its own,
checked by the kernel apart from the proof it is written in and counted in the
trust report as `Omitted cases proven`.

### 9.1. Case splits in a verified body

Written as a statement of a verified function's body, `cases` splits the rest
of that path by the states of a value read there. Nothing runs: each arm is the
path continued in one case, with that case's facts, and the code after the split
is verified once per arm. This proves what arithmetic over the whole range
cannot, such as a nonlinear fact that holds in each case separately:

<!-- cppl-example: verify -->

```cpp
enum class Mode : unsigned { idle = 0u, busy = 1u };

proof same(unsigned x)
    proves (x == x)
{
    refl;
}

verified unsigned settled(Mode m)
    expects (static_cast<unsigned>(m) <= 1u)
    ensures (result * result == result)
{
    cases m {
        Mode::idle => {
        }

        Mode::busy => {
        }

        omit unnamed by contradiction same(0u);
    }
    return static_cast<unsigned>(m);
}
```

Without the split, the same contract is refused. `omit unnamed` is checked
against the path's facts and the residual case's own discriminator, exactly as
in a proof body; the precondition is what rules the case out.

An arm has no goal of its own to close, because the path it continues has
none. It holds only a nested `cases` or `decompose`, and a `contradiction`
claim that ends its path. `refl`, `exact`, `apply`, `assume` and `rewrite` in an
arm are refused.

A case fact describes the value where the split was written and nothing later.
After a write, a write through a reference that may be the same object, a call
that may change it, or inside a loop that writes it, the storage has a new
version, and a split after that point reads the new one:

<!-- cppl-example: verify -->

```cpp
enum class Mode : unsigned { idle = 0u, busy = 1u };

proof same(unsigned x)
    proves (x == x)
{
    refl;
}

verified void rewritten(Mode& m)
    expects (m == Mode::idle)
{
    cases m {
        Mode::idle => {
        }

        omit Mode::busy by contradiction same(0u);

        omit unnamed by contradiction same(0u);
    }
    m = Mode::busy;
    cases m {
        Mode::busy => {
        }

        omit Mode::idle by contradiction same(0u);

        omit unnamed by contradiction same(0u);
    }
}
```

After the write, omitting `busy` instead is refused: the entry fact was about
the old version. What is known of a new version comes only from what
established it, such as the value written or a verified callee's postcondition.

The subject is any expression the body can read: a parameter, a local, a
member such as `s.mode`, an element such as `modes[0]`, or an arm's binder. A
local aggregate is tracked member by member and has no single value, so split
on its members rather than on the whole object. Verified code cannot write a
`std::optional`, `std::variant` or `std::expected`, and cannot reassign a
pointer local, since those are calls or forms it does not model; splits over
them read values that therefore do not change within the body. A split in a
function template declares its binders once, so a template whose
specializations bind values of different types is refused at the ones that do
not match.

Where the translation unit uses `cases` or `decompose` as a C++ name, such as
a type, the statement is ordinary C++ and the compiler warns that it is not a
split. In a function that is not verified, a split is refused.

## 10. Induction

`cases` splits possible states.

`induction` additionally supplies an induction hypothesis for each recursive
predecessor.

`decreases` proves runtime termination; it is not an induction hypothesis.

Unsigned machine induction uses `zero` and `successor(pred)`. The successor case
includes the range premise preventing wraparound:

```cpp
#include <climits>

proof unsigned_identity(unsigned n)
    proves (Eq<unsigned>(n, n))
{
    induction n {
        zero => {
            refl;
        }

        successor(pred) => {
            assume below : pred < UINT_MAX;
            assume ih : Eq<unsigned>(pred, pred);
            rewrite ih;
            refl;
        }
    }
}
```

`pred` binds the predecessor value.

`assume ih : P;` names the hypothesis supplied by the induction principle. It
cannot choose a stronger hypothesis.

Recursive structures need a defined well-founded principle, not just a pointer to
a node. Cyclic or dangling pointers do not supply induction.

The short form asks automation to solve every case:

```cpp
proof unsigned_identity_automatic(unsigned n)
    proves (Eq<unsigned>(n, n))
{
    induction n;
}
```

Nested proof commands remain scoped to their arms; induction evidence and case
facts cannot escape their binders.

Both forms erase entirely.

## 11. Invariants and termination

An invariant holds before the first iteration and is preserved on every
continuing iteration, including `continue`.

Normal loop exit combines it with the failed condition; `break` retains only the
facts on its own path.

<!-- cppl-example: verify -->

```cpp
verified unsigned count(unsigned n)
    ensures (result == n)
{
    unsigned i = 0u;

    while (i < n)
        invariant (i <= n)
    {
        ++i;
    }

    return i;
}
```

This establishes partial correctness.

Adding `decreases (n - i)` requests termination as well: the measure belongs to a
well-founded domain and strictly decreases on every continuing iteration.

An unsigned bound is finite, ordered by the machine's own non-wrapping `<`; a
signed measure has no least element and is refused.

<!-- cppl-example: verify -->

```cpp
verified unsigned terminating_count(unsigned n)
    ensures (result == n)
{
    unsigned i = 0u;

    while (i < n)
        invariant (i <= n)
        decreases (n - i)
    {
        ++i;
    }

    return i;
}
```

For loops put the same clauses after the header:

<!-- cppl-example: verify -->

```cpp
verified unsigned count_for(unsigned n)
    ensures (result == n)
{
    unsigned i = 0u;

    for (; i < n; ++i)
        invariant (i <= n)
    {
    }

    return i;
}
```

A `for` without a condition is left only by a `break` or a `return`, and may
state the same clauses.

A `do` loop places clauses after `do`, before its body, keeping the trailing
`while` in its ordinary C++ position. Its invariant must hold before the body
first runs, when nothing has checked the condition yet, and the loop is left
where the condition fails after a body, so what follows sees that body's
values rather than the invariant:

<!-- cppl-example: verify -->

```cpp
verified unsigned one_iteration()
    ensures (result == 1u)
{
    unsigned i = 0u;

    do
        invariant (i < 1u)
        decreases (1u - i)
    {
        ++i;
    } while (i < 1u);

    return i;
}
```

`invariant (i <= 1u)` would not do: it allows `i == 1u` before a body, after
which the loop would return 2.

A range-for uses the same location for its clauses, but this implementation
does not model range-based `for` yet and refuses it:

```cpp
verified unsigned visit_three()
    ensures (result == 0u)
{
    unsigned values[3] = {0u, 0u, 0u};

    for (unsigned value : values)
        invariant (values[0] == 0u)
    {
        static_cast<void>(value);
    }

    return 0u;
}
```

Recursive termination uses the same measure syntax, over the function's
parameters:

<!-- cppl-example: verify -->

```cpp
verified unsigned descend(unsigned n)
    ensures (result == 0u)
    decreases (n)
{
    if (n == 0u) {
        return 0u;
    }
    return descend(n - 1u);
}
```

Every recursive call must be made at a strictly smaller measure, under the facts
of its own path: here `n != 0u` is what makes `n - 1u` smaller rather than a
wrap to the largest value. Recursion is verified only with a measure, and the
contract a recursive call supposes is the induction hypothesis the descent
justifies, not a proof of the contract: a false base case is still refused.

`decreases (outer, inner)` is one lexicographic measure list, not two clauses:
the first component falls, or it stays and the rest fall. Functions that call
each other state measures of one length, and every call between them descends:

<!-- cppl-example: verify -->

```cpp
unsigned odd_steps(unsigned n);

verified unsigned even_steps(unsigned n)
    ensures (result == 0u)
    decreases (n)
{
    if (n == 0u) {
        return 0u;
    }
    return odd_steps(n - 1u);
}

verified unsigned odd_steps(unsigned n)
    ensures (result == 0u)
    decreases (n)
{
    if (n == 0u) {
        return 0u;
    }
    return even_steps(n - 1u);
}
```

A contract is total when every loop it runs states a measure and every function
it calls is total; otherwise it is partial correctness. The trust report keeps
the two apart:

```text
Function contracts proven:   2
  partial correctness only:  0
...
Loop measures proven:        0
Recursive call measures proven: 2
...
Partial-correctness contracts: 0
```

A function that states `decreases` asks that it terminate, so a loop without a
measure, a call to a partial function or an unsafe block in it is refused.

Proof-producing computation must terminate even without a written measure.

Runtime verification is partial correctness unless termination is requested or
required by its specification role.

A requested termination proof may not be silently dropped.

An invariant `i < n` fails on entry when `n == 0u`.

A continuing iteration that does not change `n - i` fails strict descent.

These are proof failures, not formatting problems; a linter must not weaken the
predicates.

## 12. Trusted and unsafe boundaries

`trusted` explicitly admits a proposition without proving it.

Its sole production declaration form is `trusted law`, with a semicolon:

```cpp
trusted law supplied_zero(unsigned sample)
    proves (sample == 0u);
```

This is deliberately a very strong assumption.

Because Law parameters are universally quantified, the declaration means the
proposition is trusted for every admissible `sample`.

It must therefore **not** be used as a convenient way to validate one runtime
value.

The trust report names the assumption and source location. Evidence depending on
it must retain that dependency.

The kernel still checks derived evidence relative to the explicit assumptions.

Trust is not ordinary convenience syntax and is never what `assume` means.

A similarly strong external assumption might be:

```cpp
trusted law calibrated_measurement(unsigned reading)
    proves (reading <= 100u);
```

This means the trusted boundary claims that every value represented by that Law's
parameter satisfies the property.

If only a particular runtime reading is known to be valid after inspection, use
runtime validation instead of turning the statement into a universal theorem.

Trust admission and runtime validation are different boundaries.

`unsafe` permits an operation whose safety the verifier has not established.

It does not assert that the operation is correct:

<!-- cppl-example: verify -->

```cpp
unsafe unsigned read_device();

verified unsigned poll_device()
    ensures (result <= 100u)
{
    unsigned value = 0u;

    unsafe {
        value = read_device();
    }

    if (value > 100u) {
        return 100u;
    }
    return value;
}
```

These are the unsafe function-declaration and block forms; there is no unsafe
expression form.

Runtime operations still execute.

Unsafe code cannot produce proof evidence or refinement facts merely because it
is marked `unsafe`. After the block, `value` could hold anything: the bound comes
from the check that follows it, which is ordinary runtime validation. Returning
`value` unchecked, with the same postcondition, is refused.

An unmodeled result remains unverified unless a separately justified validation
or trust boundary admits it.

### 12.1. Trust is not validation

Keep these three operations conceptually separate:

```text
PROVEN
    verifier established the property

RUNTIME-CHECKED
    runtime code inspected a value and accepted/rejected it

TRUSTED
    the property was explicitly admitted without proof
```

Do not replace a runtime check with `trusted law` simply to make an obligation
disappear.

### 12.2. Using a trusted law, and what rests on it

A proof uses a trusted law by naming it, exactly as it names a proof
(`SPEC.md` TRUSTED-006, PROOFSRC-005):

<!-- cppl-example: verify -->

```cpp
pure unsigned zero() {
    return 0u;
}

trusted law sensor_identity(unsigned x)
    proves (x + zero() == x);

trusted law device_bound(unsigned x)
    expects (x == 3u)
    proves (x + 1u == 4u);

proof first_link(unsigned y)
    proves (y + zero() == y)
{
    exact sensor_identity(y);
}

proof second_link(unsigned y)
    proves (y + zero() == y)
{
    exact first_link(y);
}

law bound_after_identity(unsigned x)
    expects (x == 3u)
    proves (x + zero() + 1u == 4u)
{
    assume is_three : x == 3u;
    rewrite sensor_identity(x);
    apply device_bound(x);
    exact is_three;
}
```

Every claim here is `PROVEN`, relative to the trusted laws it rests on
(`SPEC.md` STATUS-002). The kernel checks each one with those laws supposed as
premises, so a claim cannot use an assumption it is not reported as resting on.
`second_link` never names `sensor_identity`, but it uses a proof that does, so
it rests on it too. `apply device_bound(x)` does not assume `x == 3u`: the
premise is still owed (TRUSTED-007), and `exact is_three` pays it.

A trusted law is used only where a statement names it (TRUSTED-008). It is not a
premise standing in the proof, so `assume` cannot name it and `contradiction`
does not reason from it unless it is the evidence named. A name that is both a
proof and a trusted law, or two overloaded trusted laws, is refused rather than
resolved (TRUSTED-009).

`--cppl-trust-report` then says what every proven claim rests on:

```text
Laws proven:                 1
  by a written proof:        1
  assumption-free:           0
  relative to trusted laws:  1
Proof declarations proven:   2
  assumption-free:           0
  relative to trusted laws:  2
...
Trust-dependent claims:      3
  law bound_after_identity (guide.cpp:24), identity 79145ac641c8226e
    rests on sensor_identity (guide.cpp:5), named directly
    rests on device_bound (guide.cpp:8), named directly
  proof first_link (guide.cpp:12), identity 2e028b16cd2be25c
    rests on sensor_identity (guide.cpp:5), named directly
  proof second_link (guide.cpp:18), identity 22fb66e5fc124363
    rests on sensor_identity (guide.cpp:5), through a proof it uses
Unsafe-dependent claims:     0
Assumption-free claims:      0
Unused trusted laws:         0
```

Every proven claim is listed, with the content identity of what it states: those
that rest on trusted laws under `Trust-dependent claims`, each with every law it
rests on and whether it names that law or reaches it through what it uses, and
those that rest on none under `Assumption-free claims`. A law reached twice, or
both directly and through a proof, is listed once. Each kind of proven claim is
also split into what is proven outright and what rests
on trusted laws, and the two always add up to the count above them. The same
split is given for function contracts, and for omitted cases and impossible
paths, which are never counted as each other or as the proof they occur in. An
omitted case rests on every trusted law of the proof it is written in. A runtime
path claimed not to occur rests on those of the proof it names, and so does the
contract of the function it is written in, and the contract of every function
that calls that one. A trusted law nothing rests on
is listed under `Unused trusted laws`, so an audit can see which assumptions a
build could drop without changing any result. Each `assumed:` line also gives
the law's identity, derived from what it states, so the same assumption
included into several translation units is recognizable as one.

If the report cannot account for a dependency, the build fails with an internal
error rather than printing a shorter list (`TRUST.md` 2.10).

### 12.3. Trusted memory propositions

A trusted law may admit a memory proposition, such as a guarantee an operating
system or a device gives about a buffer (`SPEC.md` TRUSTED-003, VERIFIED-044):

<!-- cppl-example: verify -->

```cpp
trusted law device_window(unsigned* registers, unsigned count)
    expects (count <= 64u)
    proves (readable(registers, count));
```

It is an explicit assumption like any trusted law: `TRUSTED`, never proven, and
named in the trust report with its location, its identity and what it admits:

```text
Laws trusted:                1
  assumed:                 device_window (guide.cpp:1), identity fefb8672a6f55f05, admits readable(registers, count)
...
Unused trusted laws:         1
  unused:                  device_window (guide.cpp:1), no statement can use a memory proposition
```

A capability is a property of the execution state, not a proposition the kernel
checks (RFC 0014 §10), so no proof goal or premise can be one. A proof statement
naming `device_window` is refused, and so is a proof or an ordinary law whose own
claim is a memory proposition: only an explicit assumption may state one. A
trusted law supplies what it states only to the statements that name it
(TRUSTED-008), so nothing in this implementation rests on one; the report says so
rather than leave it looking forgotten.

### 12.4. What an unsafe block leaves unknown, and what rests on it

Inside a verified body, an unsafe block's statements run and are not verified.
After the block, every place it could have written holds a value nothing
describes: a local it names or whose address the body takes, what a pointer
designates, and what a reference parameter designates. A local the block never
names, and whose address is never taken, keeps its facts:

<!-- cppl-example: verify -->

```cpp
unsafe unsigned read_sample();

verified unsigned count_samples(unsigned n)
    ensures (result == n)
{
    unsigned i = 0u;
    unsigned sample = 0u;
    while (i < n)
        invariant (i <= n)
    {
        unsafe {
            sample = read_sample();
        }
        ++i;
    }
    return i;
}
```

The block names only `sample`, so `i` and `n` keep what the invariant says of
them. A block that named `i`, even to pass it by value, could have kept its
address and written through it later, so `i` would be unknown after it and the
invariant would not be preserved. Name in a block only what it needs.

No memory capability survives a block either: writing through a pointer after
one, or passing it to a verified function that requires `writable`, is refused.
Control goes on after the block, so a `return`, a `goto`, or a `break` or
`continue` out of it is refused, and so is proof syntax inside it. An unsafe
function is neither verified nor pure and states no contract; a verified body
calls one only inside an unsafe block.

A contract proven across an unsafe block holds only as far as that block is
sound, which nothing checked, and it holds only if the block returns, so it is
partial correctness. The trust report keeps that visible:

```text
Function contracts proven:   1
  partial correctness only:  1
  assumption-free:           0
  relative to trusted laws:  0
  relying on unsafe code:    1
...
Unsafe-dependent claims:     1
  contract of count_samples (guide.cpp:9), identity 97f2b36f7939ef77
    rests on unsafe block (guide.cpp:11:9), in its own body
Assumption-free claims:      0
...
Unsafe regions:              2
  unsafe function:         read_sample (guide.cpp:1:17)
  unsafe block:            guide.cpp:11:9, in verified function count_samples
```

Every contract that calls `count_samples` rests on the same block, reached
through a verified call, and is listed the same way. An unsafe block is not a
trusted assumption: it adds nothing to `Laws trusted`, and nothing it did is ever
a premise.

## 13. References, pointers and memory validity

References and pointers retain C++ binding, aliasing and lifetime semantics.

Verification tracks places, capabilities and logical value versions through
shared read/write rules.

A cast, reference binding or pointer test cannot manufacture proof.

A non-null pointer does **not** establish:

```text
readable
writable
initialized storage
bounds
provenance
lifetime
```

These are capability concepts in the storage model, not ordinary Boolean
functions that this guide invents.

There is no user-defined `bool readable(int*)` shortcut that grants a capability.

Rejected use of non-nullness as dereference evidence:

```cpp
verified int read_pointer(int* pointer)
    expects (pointer != nullptr)
    ensures (result == 0)
{
    return *pointer;
}
```

The contract lacks evidence of a valid readable initialized pointee.

Even if dereference validity were established, that alone would not establish the
claimed zero.

Pointer/refined-pointee operations must use the common capability and
refinement-crossing machinery, with any external capability assumption explicitly
recorded as trusted.

Const references do not protect facts from mutation through another alias.

### 13.1. Runtime validation and external input

Some values cannot be known until execution.

Typical examples include values from:

```text
network
file
database
command line
device
OCR
FFI
user input
```

C++L must not pretend to prove such values before execution.

Runtime validation is performed using ordinary C++ control flow. C++L does not
require a special `validate<T>()` language construct or standard runtime validator. C++L ships no required runtime support library and injects no verification runtime into the executable.

For example:

```cpp
type Percentage = int where (self >= 0 && self <= 100);

int raw = read_from_network();

if (raw >= 0 && raw <= 100) {
    Percentage percentage = raw;
    use_percentage(percentage);
}
```

The runtime `if` performs the actual validation.

On the successful branch, C++L may use the path facts:

```text
raw >= 0
raw <= 100
```

to establish that `raw` satisfies the refinement predicate for `Percentage`.

The normal flow is therefore:

```text
untrusted runtime value
    |
    v
ordinary C++ runtime check
    |
    +---- validation failed -> ordinary runtime error/result path
    |
    v
path establishes refinement predicate
    |
    v
refined value
    |
    v
verified code
```

No hidden runtime check is generated by:

```cpp
type Percentage = int where (self >= 0 && self <= 100);
```

and this is not an implicit conversion:

```cpp
Percentage percentage = raw;
```

unless the current proof context already establishes the refinement predicate.

This distinction is fundamental:

```text
runtime validation
    = ordinary C++ execution establishes facts on a runtime path

refinement introduction
    = C++L verifies that the required predicate is known on that path

trusted
    = an explicit assumption admitted without proof

compile-time proof
    = property established without executing the program
```

Runtime validation may use any ordinary C++ structure whose semantics C++L can
soundly reason about.

For example:

```cpp
if (raw > 0) {
    Positive value = raw;
}
```

or a helper function with a checked contract:

```cpp
verified bool is_percentage(int value)
    ensures (result == (value >= 0 && value <= 100))
{
    return value >= 0 && value <= 100;
}
```

which can then be used as ordinary runtime control flow:

```cpp
if (is_percentage(raw)) {
    Percentage percentage = raw;
}
```

The verifier may use the checked postcondition of `is_percentage` to recover the
corresponding path fact.

There is no requirement that validation use one particular helper API.

The important rule is:

```text
ordinary C++ performs the runtime check

C++L verifies what becomes known on each resulting path

a refined value may be introduced only when its predicate is established
```

This keeps runtime validation explicit and avoids introducing a separate C++L
validation framework into the core language.

### 13.2. Defined C++ behavior is part of verification

C++L does not redefine undefined C++ behavior into mathematical behavior.

Verified code must remain valid according to the underlying C++ abstract machine.

Examples that may require proof obligations include:

```text
signed overflow
division by zero
invalid shifts
out-of-bounds access
invalid pointer arithmetic
use after lifetime end
dereference of invalid storage
use of an uninitialized value
invalid downcasts
violations of object lifetime or aliasing rules
```

For example:

<!-- cppl-example: verify -->

```cpp
#include <climits>

verified int divide(int x, int y)
    expects (y != 0 && x != INT_MIN)
    ensures (result == x / y)
{
    return x / y;
}
```

needs both conditions: division by zero is not repaired by theorem reasoning,
and `INT_MIN / -1` is a quotient `int` does not hold. Section 13.3 lists what
each arithmetic operation owes.

Similarly:

```cpp
verified int read(int* pointer)
    expects (pointer != nullptr)
{
    return *pointer;
}
```

is not sufficient merely because nullness was excluded. The pointer must also
refer to readable live initialized storage.

A mathematical identity never licenses undefined C++ execution.

The required relationship remains:

```text
verified proposition
    +
defined C++ execution
    =
valid C++L guarantee
```

The verifier must fail closed when it cannot establish required definedness.

### 13.3. Signed arithmetic, division and conversions

Integer operations keep their exact C++ meaning (`SPEC.md` 29, RFC 0019).
Unsigned `+`, `-`, `*` and unary `-` wrap modulo `2^width` and owe nothing.
The others are defined only under a condition, and each evaluation owes it
where it happens:

| Operation                                  | Owes, where it is evaluated                            |
| ------------------------------------------ | ------------------------------------------------------ |
| signed `a + b`, `a - b`, `a * b`, `-a`     | the exact result is a value of the type                |
| `a / b`, `a % b`, signed or unsigned       | `b != 0`                                               |
| signed `a / b`, `a % b`                    | not the least value divided by `-1`                    |
| a conversion to a signed type, or a cast   | the value fits the target, in every C++ mode           |

The proof uses what the path knows there: guards, preconditions, refinements,
loop invariants and callee postconditions. Afterwards the path knows the
condition held, so `x + 1 > x` follows once `x + 1` was computed.

<!-- cppl-example: verify -->

```cpp
#include <climits>

verified int saturating_increment(int x)
    ensures (result >= x)
{
    if (x < INT_MAX)
        return x + 1;
    return x;
}

verified int sum_below(int n)
    expects (n >= 0 && n <= 1000)
    ensures (result >= 0 && result <= n * 1000)
{
    int total = 0;
    for (int i = 0; i < n; ++i)
        invariant (0 <= i && i <= n && 0 <= total && total <= i * 1000)
    {
        total += i;
    }
    return total;
}

verified unsigned bucket(unsigned hash, unsigned size)
    expects (size > 0u)
    ensures (result < size)
{
    return hash % size;
}
```

A few things to know:

- Promotions and the usual arithmetic conversions are the ones Clang puts in the
  program. Two `short` values are added in `int`, so the sum always fits there,
  and returning it as `short` is the conversion that owes the bound.
- A comparison of an `int` with an `unsigned` converts the `int`: `i < 0u` is
  false for every `i`.
- `/` truncates toward zero and `%` has the dividend's sign: `-7 / 2` is `-3` and
  `-7 % 2` is `-1`.
- A specification means what C++ would compute. `ensures (result + 1 > result)`
  also requires that `result + 1` is defined, which it is not for `INT_MAX`.
- Only the calls C++ sequences before an operation lend it their postconditions.
  In `f(x) + (x + 1)`, `f` may not have returned when `x + 1` runs.
- A `pure` function is unfolded wherever it is called with nothing owed there,
  so one that adds two signed values is refused as a definition. Write it as a
  `verified` function with a contract instead.
- Today, a product of two unknowns is decided only where their types bound it
  (two values promoted from 8- or 16-bit types, except two `unsigned short`
  values), and a quotient by an unknown divisor is left unknown; shifts,
  bitwise operators and `bool` conversions are refused.

## 14. Templates

Templates retain ordinary C++ syntax.

Place definitions and required verification metadata where instantiation can see
them, normally in a header:

```cpp
template <typename T>
verified T identity(T value)
    ensures (result == value)
{
    return value;
}
```

The specialization must have modeled equality and body semantics.

A template constraint is not an implicit theorem about arbitrary `T`.

An indexed refinement can be used with a template parameter:

```cpp
type Index(unsigned n) = unsigned where (self < n);

template <unsigned N>
verified unsigned widen_index(Index<N> value)
    ensures (result < N)
{
    return value;
}
```

Clang resolves substitution and type identity.

The refinement declaration, contract, effect metadata and any referenced
Laws/evidence must remain available at the instantiation site.

An explicit-instantiation strategy must preserve the same information.

### 14.1. Ordinary C++ inside verified code

A `verified` body is still ordinary C++.

C++L does not replace:

```text
if
switch
for
while
references
pointers
RAII
templates
overload resolution
constructors
destructors
the standard library
```

with a separate runtime language.

Clang remains responsible for ordinary C++ parsing, typing and overload
resolution.

C++L adds proof obligations for the semantics it models.

If the verifier cannot soundly model a construct used by verified code, it must
reject that verification path rather than invent a fact.

Check [STATUS.md](./STATUS.md) for current semantic coverage.

This distinction matters:

```text
valid C++
    does not automatically mean
currently verifiable C++

unsupported verification
    does not mean
invalid C++
```

### 14.2. Template verification happens for real instantiations

A template declaration does not give every possible specialization free proof
facts.

For example:

```cpp
template <typename T>
verified T identity(T value)
    ensures (result == value)
{
    return value;
}
```

must only be accepted for instantiations whose operations and equality semantics
are modeled sufficiently to verify the body and contract.

Each instantiated specialization must satisfy the obligations induced by:

```text
its actual types
its actual non-type parameters
its selected overloads
its refinements
its effects
its called functions
```

Ordinary C++ constraints and concepts participate in normal Clang template
selection.

They do not automatically become arbitrary logical axioms.

For example:

```cpp
template <typename T>
requires SomeConcept<T>
verified T f(T value)
{
    ...
}
```

means ordinary C++ has established the `SomeConcept<T>` constraint according to
C++ rules.

C++L may use formal facts associated with that concept only when those facts have
a defined verification model.

The compiler should report template verification failures with both:

```text
the template source location
the relevant instantiation context
```

so developers can see which specialization generated the obligation.

## 15. Organizing C++L code in .h/.hpp and .cpp

**Public contract → header. Runtime implementation → source file.**

Callers need the contract, not access to the function body.

Use existing C++ extensions; a special header suffix is unnecessary.

`include/account.hpp`:

```cpp
#pragma once

verified unsigned withdraw(unsigned old_balance, unsigned amount)
    expects (amount <= old_balance)
    ensures (result == old_balance - amount);
```

`src/account.cpp`:

```cpp
#include "account.hpp"

unsigned withdraw(unsigned old_balance, unsigned amount)
{
    return old_balance - amount;
}
```

The definition inherits the verified declaration through Clang's resolved
function entity.

Do not duplicate the contract.

A changed parameter name does not create another entity.

Conflicting contracts on redeclarations are errors; identical repetition is legal
when source organization requires it.

The formatter never copies a declaration contract onto a definition.

Use a direct contracted definition for private/static helpers.

Shared refinements and Laws belong beside the APIs that use them.

Implementation-only Laws belong in the source, commonly in an unnamed namespace:

```cpp
namespace {

law local_identity(unsigned x)
    proves (x + 0u == x);

}
```

This is a translation-unit-local theorem, not a block-local Law declaration.

Laws and proofs produce no runtime symbols.

Class-scope Laws follow ordinary member lookup and quantify the implicit object;
they do not create runtime methods.

A realistic layout is:

```text
include/
    money.hpp       shared refinements and arithmetic Laws
    account.hpp     public contracts and boundary declarations

src/
    account.cpp     runtime definitions and private proof helpers
```

A separate `proofs/` directory is optional.

Shared theorem evidence can live with its interface; do not split formal metadata
away from callers that need it.

| Construct                        | Normally in header/interface?                  |
| -------------------------------- | ---------------------------------------------- |
| Public verified contract         | Yes                                            |
| Runtime function body            | Usually no                                     |
| Template definition and contract | Yes, unless explicit instantiation is arranged |
| Shared refinement                | Yes                                            |
| Shared Law and reusable evidence | Yes                                            |
| Implementation-only Law/proof    | No                                             |
| Trusted external assumption      | At the boundary's interface                    |
| Ghost local                      | No; inside its verification-enabled block      |

Across translation units, preserve:

```text
contracts
refinement identity/predicates
Law propositions/evidence
purity/effect metadata
trust dependencies
```

Native erasure does not encode these in ABI symbols.

A visible contract lets a caller state obligations; checked implementation
evidence or an explicit trust boundary is still needed before the summary can be
used as proof.

This compiler supplies it through a verification interface the defining unit
writes and the calling unit imports (15.4). Without one, the call is refused.

Ordinary C++ modules retain their C++ meaning.

No additional C++L module-metadata syntax is introduced here; this guide's
supported interface organization uses headers and included verification metadata.

### 15.1. Redeclarations and conflicts

This is the recommended form:

```cpp
// account.hpp

verified unsigned withdraw(unsigned balance, unsigned amount)
    expects (amount <= balance)
    ensures (result == balance - amount);
```

```cpp
// account.cpp

#include "account.hpp"

unsigned withdraw(unsigned balance, unsigned amount)
{
    return balance - amount;
}
```

Do not repeat a different contract later:

```cpp
verified unsigned withdraw(unsigned balance, unsigned amount)
    expects (amount < balance)
    ensures (result == balance - amount);
```

That is a conflicting redeclaration of the same C++ function entity.

The contract belongs to the function, not to whichever source file happened to
spell it.

A definition whose body states loop clauses must itself be marked `verified`,
so it restates the contract. That is legal when the two state the same contract;
they are compared by meaning, so renamed parameters agree:

```cpp
// counter.hpp
verified unsigned count_to(unsigned n)
    ensures (result == n);
```

```cpp
// counter.cpp
#include "counter.hpp"

verified unsigned count_to(unsigned limit)
    ensures (result == limit)
{
    unsigned i = 0u;
    while (i < limit)
        invariant (i <= limit)
        decreases (limit - i)
    {
        ++i;
    }
    return i;
}
```

Restating it as `ensures (result <= limit)` is refused, naming both statements
(`SPEC.md` TU-003). As `counter.cpp` reaches the compiler, with its header
included:

<!-- cppl-example: verify -->

```cpp
verified unsigned count_to(unsigned n)
    ensures (result == n);

verified unsigned count_to(unsigned limit)
    ensures (result == limit)
{
    unsigned i = 0u;
    while (i < limit)
        invariant (i <= limit)
        decreases (limit - i)
    {
        ++i;
    }
    return i;
}
```

### 15.2. Refinements do not create runtime overload identities

Refinement identity exists for verification, but refinements erase to their base
C++ representation.

Therefore overloads that differ only by refinements must not silently become two
different runtime functions.

For example:

```cpp
type Positive = int where (self > 0);
type Negative = int where (self < 0);

int classify(Positive value);
int classify(Negative value);
```

Both erase to the same underlying C++ signature:

```cpp
int classify(int value);
```

If the language does not have a separate explicit mechanism for such
verification-level dispatch, this is a declaration collision and must be
diagnosed rather than deferred to surprising runtime behavior.

### 15.3. C++L annotations do not change the native ABI by themselves

Verification-only syntax erases before ordinary native compilation.

For a verified function such as:

```cpp
verified int increment(int x)
    ensures (result == x + 1)
{
    return x + 1;
}
```

the runtime callable shape remains equivalent to ordinary C++:

```cpp
int increment(int x)
{
    return x + 1;
}
```

Likewise, refinements lower to their base representation.

Therefore C++L does not require a separate calling convention merely because a
function has:

```text
verified
expects
ensures
pure
refinement types
```

Proof metadata needed for separate verification is not native ABI state.

It must be transported through C++L compiler metadata, headers, module metadata or
another explicit verification interface.

Do not confuse:

```text
native ABI compatibility
```

with:

```text
availability of verification metadata
```

A binary may remain ABI-compatible while another translation unit lacks enough
proof metadata to verify a call. In that case verification must fail closed
rather than inventing the missing contract evidence.

### 15.4. Using a contract proven in another translation unit

The unit that defines a verified function proves its contract and can record
what it proved in a **verification interface**. A unit that only declares the
function imports that interface to rely on the contract (`SPEC.md` Annex L.2.1,
RFC 0017):

```sh
# account.cpp defines withdraw and proves it; the interface records that.
cppl -std=c++20 -c account.cpp -o account.o --cppl-emit-interface=account.cppli

# main.cpp includes account.hpp and calls withdraw.
cppl -std=c++20 -c main.cpp -o main.o --cppl-import-interface=account.cppli

# The objects link as any C++ objects do.
cppl account.o main.o -o app
```

`--cppl-emit-interface=<file>` names where one unit's interface goes; the build
system decides the name, and the file is written only once the unit verified
and its object was produced. `--cppl-import-interface=<file>` may be repeated,
once per unit whose contracts this one uses.

What the caller gets is exactly what it states. The contract the calling unit
relies on is the one it builds from its own declaration of the function; the
interface only says whether the defining unit proved that same contract, for
that same function, as Clang resolves it. A call is therefore refused, with the
reason, when:

```text
no imported interface records the function
    a declaration is not evidence

the declaration here states another contract than the one recorded
    a stronger postcondition, a weaker precondition, another refinement

the call resolves to another overload or another specialization
    f<5> is never proven by f<4>'s record

the interface is not usable here
    malformed, truncated, altered, of another format version, produced by
    another cppl build, kernel, Clang, -std or target, or stale: a file its
    unit was compiled from changed after it was written

what the record rests on is not imported
    a unit proven through a third unit's contract needs that interface too,
    as it was when the proof was made
```

A unit whose proof used another unit's contract records that it did, so a
build imports every interface along the chain. Rebuilding a unit rewrites its
interface; one left from an earlier build of a changed file is refused as stale,
and a unit that no longer verifies removes its interface.

What a caller proves through another unit's contract is `PROVEN` relative to
that record. `--cppl-trust-report` lists every imported contract, and under
`Interface-dependent claims` every claim resting on one, together with any
trusted law and unsafe block the other unit's proof rested on:

```text
Function contracts imported: 1
  imported:                  contract of withdraw [c:@F@withdraw#i#i#], imported from account.cppli, entry 3f0c..., total
...
Interface-dependent claims:  1
  contract of pay (main.cpp:4), identity 91d2...
    rests on the contract of withdraw [c:@F@withdraw#i#i#], imported from account.cppli, entry 3f0c..., called in its own body
```

Such a claim is never counted as assumption-free: the calling unit did not
check the other unit's proof, it trusts the interface that recorded it
(`TRUST.md` 31.1). Totality crosses too: a contract recorded as partial
correctness makes its callers partial, and a function stating `decreases`
cannot call one.

Keep in mind:

- the contract must be on the header declaration; a function whose body needs
  loop clauses restates it on its definition (15.1);
- only functions with external linkage cross; a `static` or anonymous-namespace
  function is its own unit's;
- a member function crosses as a function does, with its contract on its
  declaration in the class; its out-of-line definition does not restate it, so
  a member function whose body needs loop clauses is defined in the class (16);
- a specialization crosses when declared explicitly with its contract,
  `template <> verified unsigned f<4u>(unsigned x) ensures (...);`; one of a
  template a unit only declares is refused, since nothing instantiates its
  contract there;
- recursion across units is not verified;
- the editor (`cppl-lsp`) does not import interfaces yet, so it reports such a
  call as unavailable.

`tests/fixtures/cross_tu/` is a complete three-unit example, built by
`tests/e2e/cross_tu.sh`.

## 16. Member contracts

Use member lookup and `this`, not a second meaning for `self`.

A member function marked `verified` is checked like a function whose implicit
object is storage: each member of the object is a place the body reads and
writes, and a contract names members as the body does. A precondition reads the
object on entry; a postcondition reads it where the function returns
(`SPEC.md` CONTRACT-009, CLASS-008).

<!-- cppl-example: verify -->

```cpp
class Account {
public:
    Account(unsigned balance, unsigned limit) : balance_(balance), limit_(limit) {}

    verified unsigned balance() const
        ensures (result == balance_)
    {
        return balance_;
    }

    verified void deposit(unsigned amount)
        expects (amount <= limit_ && balance_ <= limit_ - amount)
        ensures (balance_ <= limit_)
    {
        balance_ = balance_ + amount;
    }

    verified unsigned withdraw(unsigned amount)
        expects (amount <= balance_)
        ensures (result == balance_);

private:
    unsigned balance_;
    unsigned limit_;
};

unsigned Account::withdraw(unsigned amount)
{
    balance_ = balance_ - amount;
    return balance_;
}
```

Put the contract on the declaration in the class. An out-of-line definition
inherits it and does not restate it: `verified` on a qualified definition such
as `Account::withdraw` is refused, since the class's members are not in scope
where it stands (CONTRACT-005). A body that needs loop clauses is therefore
defined in the class. When the definition is in another translation unit, the
caller relies on the contract through that unit's verification interface, as
for any function (15.4).

What a member function may write follows its qualifiers. A `const` one leaves
its object as it was, so a caller keeps what it knew about the object across
the call; a `mutable` member is the exception, and a `const` member function
that may write one is treated as writing its object. A mutating call gives
every member of its object a new value, of which the caller knows exactly what
the callee's `ensures` states (CLASS-009, CLASS-011):

<!-- cppl-example: verify -->

```cpp
struct Counter {
    unsigned value;
    unsigned limit;

    verified unsigned get() const
        ensures (result == value)
    {
        return value;
    }

    verified void reset()
        ensures (value == 0u)
    {
        value = 0u;
    }
};

verified unsigned read_after_reset(unsigned start)
    ensures (result == 0u)
{
    Counter counter{start, 10u};
    counter.reset();
    return counter.get();
}
```

After `counter.reset()` nothing is known of `counter.limit`: `reset` does not
say it keeps it, and C++L has no `old(...)` yet to say so with. State in
`ensures` every member a caller relies on after a mutating call.

The object a member function runs on is caller storage, so it may be what a
reference parameter designates. A write through the parameter is a write that
may land on a member, and a write to a member is one that may land on what the
parameter designates; each takes away what was known through the other
(CLASS-010). Distinct members of one object are distinct storage, so a write to
one keeps the facts of the others.

A refined member owes its predicate at every write, as a refined local does, and
holds it on entry to every member function (CLASS-010, REFINEOBL-007).

A static member function has no implicit object and is verified as a function
is. Member function templates, members of class templates, member functions of
a union or of a class with a base, and a member function called through a
pointer to member are refused rather than verified (CLASS-015).

A constructor has no return-value `result`; its postcondition describes the
initialized object.

Snapshots cannot read members that were not initialized in the entry state:

```cpp
struct Zero {
    unsigned value;

    verified Zero()
        ensures (value == 0u)
        : value(0u)
    {
    }
};
```

Constructor initializer syntax remains ordinary C++ after the clauses.

A compiler must model initialization and lifetime before accepting that
verification.

### 16.1. Virtual functions and override contracts

A verified virtual function must remain substitutable through its base
interface.

Suppose the base class declares:

```cpp
class Account {
public:
    virtual verified unsigned withdraw(unsigned amount)
        expects (amount <= balance())
        ensures (result <= old(balance())) = 0;

    virtual unsigned balance() const = 0;
};
```

A caller through `Account&` knows only the base contract.

Therefore an override must not require more than the base contract required.

In other words, an override must not strengthen the precondition.

If the base accepts:

```text
amount <= balance()
```

an override cannot require:

```text
amount < balance()
```

because a caller allowed by the base contract could then violate the override.

An override must also provide at least the guarantees promised by the base.

It may strengthen its postcondition, but it must not weaken the base
postcondition.

Conceptually:

```text
override expects
    must accept every state accepted by base expects

override ensures
    must imply the guarantees of base ensures
```

The same principle applies to verified effect and purity summaries where they are
part of the callable contract.

Dynamic dispatch does not permit a derived implementation to invalidate facts
that callers were allowed to establish from the base interface.

If the verifier cannot establish override compatibility, the override is
rejected.

This implementation does not check override compatibility yet, so it refuses
`verified` on every virtual function -- one declared `virtual`, `override` or
`final`, and one that overrides a virtual function without saying so -- and it
refuses a call to a virtual function from a verified body, whether the call
dispatches or names one function by qualification (`SPEC.md` CLASS-014). A
non-virtual member function of a class that also has virtual ones is verified
as usual.

### 16.2. Constructors and destructors are lifetime boundaries

Constructors and destructors need special treatment because object lifetime is
changing.

A constructor has no `result`.

Its postcondition describes the initialized object:

```cpp
struct Counter {
    unsigned value;

    verified Counter()
        ensures (value == 0u)
        : value(0u)
    {
    }
};
```

A constructor postcondition may only rely on members whose initialization and
lifetime are valid on the relevant path.

It cannot use `old(member)` for a member that did not have a live entry-state
value.

A destructor likewise has no returned `result`.

Verification of destruction must respect ordinary C++ destruction order,
subobject lifetime and any effects performed by destructors.

C++L must never reason about an object as still live after its lifetime has ended.

This implementation does not model construction or destruction, so `verified`
on a constructor or a destructor is refused where it is written (`SPEC.md`
CLASS-015). Construct an object in a verified body with an aggregate
initializer, or in ordinary C++, and verify the member functions that run on it.

## 17. Formatting and editor fixes

Run the shared formatter:

```sh
build/dev/bin/cppl-format -i include/account.hpp src/account.cpp
build/dev/bin/cppl-format --check include/account.hpp src/account.cpp
```

The CLI, LSP document formatting and CI share one engine.

Range formatting expands to a complete affected clause/block according to the
established range policy; on-type formatting is conservative.

Ordinary C++ layout comes from clang-format.

The repository style uses:

```text
four spaces
120-column limit
attached ordinary C++ braces
separate opening brace after a contract block
```

Canonical C++L rules are:

```text
one parenthesized clause of each kind
grammar order
continuation lines
one canonical space before `(` for clause-like constructs
verified pure
expanded proof arms
inline refinement `where`
old(x) with expression-like spacing
```

The parser need not make whitespace itself semantic.

For example, these may parse equivalently:

```cpp
ensures(result == x)
ensures (result == x)
```

but the formatter always emits:

```cpp
ensures (result == x)
```

What cppl-lsp fixes automatically through formatting:

Before:

```cpp
verified int f(int x) ensures(result == x) expects(x > 0) {
    return x;
}
```

After:

```cpp
verified int f(int x)
    expects (x > 0)
    ensures (result == x)
{
    return x;
}
```

Migration diagnostics must preserve meaning:

| Input issue                              | Deterministic correction            | Safety condition                                        |
| ---------------------------------------- | ----------------------------------- | ------------------------------------------------------- |
| Missing clause parentheses               | Wrap the delimited expression       | Boundary is unambiguous                                 |
| Wrong order/header-line clauses          | Reorder and format complete clauses | Preserve predicate text and comments                    |
| Repeated `expects`/`ensures`/`invariant` | One ordered `&&` predicate          | Predicates have conjunction semantics                   |
| Law `ensures`                            | Replace keyword with `proves`       | No invalid Law `result` use                             |
| `pure verified`                          | `verified pure`                     | Both are contextual modifiers                           |
| Compact proof arms                       | Expanded `Label(bindings) => { }`   | Same labels, bindings and steps                         |
| Obsolete proof `case`                    | `cases`                             | Inside a proof, never a C++ switch                      |
| Untyped refinement index                 | Write its declared index type       | Intended type is established; otherwise ask for an edit |

A migration note is not compiler acceptance of a legacy dialect.

If a correction could alter meaning, the diagnostic asks for a source edit rather
than guessing.

Coloring follows the same rule. The editors' grammar leaves a statement such as
`exact h;` or `contradiction nothing;` uncolored, because it is spelled like a
C++ declaration. cppl-lsp colors it once the compiler has read it as a proof
statement. A `contradiction` claim in a verified body stays uncolored while any
part of the translation unit, an included header too, uses the word as a C++
name, because there the statement declares a variable.

Navigation follows the compiler's reading too. Go to definition on a name in a
Law's proposition, a contract clause, a loop invariant or a refinement
predicate asks Clang, which resolved that same expression for the compiler; a
Law named in a proof's `proves` clause leads to the Law, a parameter named in a
clause to the parameter written, and a refinement type to its `type`
declaration. A name Clang resolves to something nobody wrote, such as `result`,
leads nowhere rather than into generated code. Find references lists the same
uses: a proof's parameter is one name wherever the proof mentions it, in its
claim and in its `assume` statements alike. The names proof statements use
navigate the same way, from the compiler's own resolution: `exact p;` and
`apply p;` lead to proof `p` or trusted Law `p`, `rewrite h;` to the `assume`
that bound `h`, and `contradiction e;` to its evidence, whether it closes a
proof, discharges an omitted case or claims a path cannot occur. Hovering any of
these names shows the declaration as written, so a Law reads as its `law`
declaration and a refinement type says what it refines and erases to.

Completion offers C++L's own syntax where it may be written, laid out as the
formatter lays it out: a `law`, `proof`, `verified` function or refinement
`type` where a declaration may begin, the proof statements inside a proof, the
proofs and assumptions `exact` or `apply` can name, and `proves`, `expects` or
`ensures` after a declaration's parameters.

The editor also shows what the compiler concluded. A code lens over each Law,
proof and verified function states its verdict -- `PROVEN`, `PROVEN relative to
trusted` the Laws it rests on, `TRUSTED`, or `UNRESOLVED` and why -- and hover
lists each obligation with the goal the kernel was given. These are the
verdicts `cppl --cppl-trust-report` would print for the same text, and they
disappear while the buffer has changes the compiler has not seen.

No fix may:

```text
weaken a Law
remove an arm
invent a premise
turn a failed proof into trust
change runtime C++ semantics
```

## 18. Reading diagnostics

A useful diagnostic identifies:

```text
source location
goal
available premises
failed obligation
trust provenance
```

Distinguish these common causes:

| Diagnostic                            | Action                                                |
| ------------------------------------- | ----------------------------------------------------- |
| Law needs `proves`                    | Correct its conclusion keyword                        |
| Clause requires parentheses/order     | Apply the syntax/layout fix                           |
| Refinement introduction failed        | Establish the predicate on this value version         |
| Non-exhaustive cases                  | Add the missing state or prove it impossible          |
| `assume` does not match a premise     | Use only evidence actually supplied by the context    |
| Ghost value affects runtime           | Keep runtime computation independent of erased state  |
| Capability obligation failed          | Supply valid memory evidence, not just non-nullness   |
| Termination measure does not decrease | Correct the algorithm or its justified measure        |
| Unsupported semantics                 | Keep verification fail-closed; no implicit assumption |
| Callee precondition failed            | Establish the callee's `expects` before the call      |
| Conflicting redeclaration             | Make all declarations describe one logical contract   |
| `verification-interface`              | Import the defining unit's current interface (15.4)   |

Use `cppl` with ordinary Clang compile options.

For example:

```sh
build/dev/bin/cppl -std=c++20 -fsyntax-only source.cpp
```

runs verification without linking a native executable.

Consult [TRUST.md](./TRUST.md) for trust-report meaning and
[tools/cppl-lsp/README.md](../tools/cppl-lsp/README.md) for editor setup.

### 18.1. Failed proof versus unsupported verification

These are different problems.

A failed proof means C++L understands the operation but cannot establish the
required proposition.

For example:

```cpp
type Positive = int where (self > 0);

verified Positive bad(int x)
{
    return x;
}
```

The verifier understands the refinement introduction but lacks `x > 0`.

Unsupported verification means the current verifier cannot soundly model a
construct at all.

In that case the compiler must say so explicitly.

It must never convert:

```text
unsupported
```

into:

```text
assumed true
```

### 18.2. Common developer questions

**Does `verified` make the function compile-time-only?**

No. Its body is runtime C++.

**Does `law` run?**

No.

**Does `proof` run?**

No.

**Does `ghost` allocate runtime storage?**

No.

**Does `expects` insert a runtime check?**

No.

**Can runtime input satisfy a refinement?**

Yes, after explicit runtime validation or another sound evidence-producing
boundary.

**Does `pointer != nullptr` prove dereference validity?**

No.

**Can a Law use `result`?**

No. Laws do not return runtime values.

**Why does a function use `ensures` while a Law uses `proves`?**

Because a function has a runtime postcondition, while a Law establishes a
compile-time theorem.

### 18.3. Verification failures are build failures, not advisory warnings

When source claims a C++L guarantee, failure to prove that guarantee must fail the
verified compilation.

Examples include:

```text
failed `ensures`
failed Law
unproved callee precondition
invalid refinement crossing
non-exhaustive proof cases
failed memory capability
failed termination measure
unsupported semantics needed for the proof
```

These must not silently degrade into warnings while still reporting the code as
verified.

A build system may separately choose to compile ordinary unverified C++ where the
project permits it, but that must not be reported as successful C++L verification.

The developer should always be able to distinguish:

```text
compiled as ordinary C++

compiled and verified as C++L

compiled with explicit trust dependencies

compiled with unsafe runtime operations
```

## 19. Practical recipes

The previous sections describe individual language features. This section shows
how they fit together in normal C++L development.

### 19.1. Public API contract with a source implementation

Header:

```cpp
// account.hpp

#pragma once

verified unsigned withdraw(unsigned balance, unsigned amount)
    expects (amount <= balance)
    ensures (result == balance - amount);
```

Source:

```cpp
// account.cpp

#include "account.hpp"

unsigned withdraw(unsigned balance, unsigned amount)
{
    return balance - amount;
}
```

Caller:

```cpp
verified unsigned empty_account(unsigned balance)
    ensures (result == 0u)
{
    return withdraw(balance, balance);
}
```

The caller proves:

```text
amount == balance
    -> amount <= balance
    -> withdraw precondition satisfied
    -> result == balance - balance
    -> result == 0
```

### 19.2. Runtime value into a refinement

Suppose external input produces an ordinary runtime `int`:

```cpp
int raw = read_from_network();
```

It cannot automatically become:

```cpp
Percentage
```

because the compiler cannot know the network value ahead of execution.

Use runtime validation:

```text
raw int
    -> runtime validation
    -> validated Percentage
    -> verified code
```

After validation succeeds, verified code can rely on:

```text
0 <= percentage <= 100
```

without repeating the runtime check everywhere downstream.

### 19.3. Law with an explicit proof

The theorem:

```cpp
law identity_theorem(unsigned x)
    proves (Eq<unsigned>(x, x))
{
    refl;
}
```

Another proof may reuse it:

```cpp
proof use_identity_theorem(unsigned x)
    proves (Eq<unsigned>(x, x))
{
    exact identity_theorem(x);
}
```

All of this erases.

No runtime call to `identity_theorem` exists.

### 19.4. Contract composition

```cpp
verified unsigned one_from_zero(unsigned x)
    expects (x == 0u)
    ensures (result == 1u)
{
    return x + 1u;
}

verified unsigned two_from_zero(unsigned x)
    expects (x == 0u)
    ensures (result == 2u)
{
    return one_from_zero(x) + 1u;
}
```

The first contract contributes evidence to the second verification.

The runtime program still contains ordinary function calls and arithmetic.

### 19.5. Loop with ghost state, invariant and termination

<!-- cppl-example: verify -->

```cpp
verified unsigned count_preserving_input(unsigned input)
    ensures (result == input)
{
    ghost unsigned original = input;
    unsigned i = 0u;

    while (i < input)
        invariant (i <= input && input == original)
        decreases (input - i)
    {
        ++i;
    }

    return input;
}
```

The roles are distinct:

```text
ghost original
    -> proof-only snapshot

invariant (...)
    -> loop correctness

decreases (...)
    -> termination

return input
    -> runtime behavior
```

### 19.6. Crossing from unverified input into verified code

A common real application boundary looks like this:

```text
external source
    |
    v
ordinary runtime value
    |
    v
validation
    |
    +---- invalid -> reject / error path
    |
    v
refined value
    |
    v
verified core
```

For example:

```cpp
type Percentage = unsigned where (self <= 100u);

verified unsigned apply_percentage(
    unsigned value,
    Percentage percentage)
{
    return value * percentage / 100u;
}
```

An external parser may produce:

```cpp
unsigned raw_percentage;
```

Do not simply reinterpret it as `Percentage`.

First validate:

```text
raw_percentage <= 100
```

at runtime.

Only the successful branch may introduce the refined value and pass it into the
verified core.

This pattern is recommended for:

```text
network input
JSON
database rows
files
command-line arguments
FFI
device data
OCR
user input
```

It keeps runtime uncertainty at the edge and lets the core of the program operate
on values whose required properties are already established.

### 19.7. Verified core with an unverified runtime shell

C++L does not require an entire application to be formally verified.

A practical architecture is:

```text
unverified / ordinary C++ shell
    |
    | parsing, OS APIs, UI, networking
    v
validated boundaries
    |
    v
verified C++L core
    |
    | contracts, refinements, Laws
    v
ordinary native execution
```

The important rule is that every transition into the verified core must establish
the properties that the verified interface expects.

This allows incremental adoption without pretending that unverified code already
carries proof.

### 19.8. Concurrency, atomics and shared mutation

C++L does not get thread safety merely from proving sequential expressions.

Concurrent code introduces additional concerns such as:

```text
data races
atomic ordering
inter-thread happens-before relations
shared mutation
lifetime across threads
lock invariants
```

Ordinary C++ concurrency semantics remain authoritative.

If C++L has no formal model for a concurrency construct used by a verified proof,
the verifier must reject that verification path or require an explicitly modeled
boundary.

It must not reason as if concurrently mutable storage were stable simply because
the current function did not write to it.

In particular:

```text
const
    != immutable across threads

non-atomic read
    != stable shared fact

pure
    != automatically thread-safe
```

Any future concurrency model must make its synchronization and interference rules
explicit rather than implicitly extending sequential proofs.

## 20. C++L cheat sheet

### Core constructs

| Goal                                | Canonical C++L form                                   |           Runtime? | Meaning / usual location                                                 |
| ----------------------------------- | ----------------------------------------------------- | -----------------: | ------------------------------------------------------------------------ |
| Verify a runtime function           | `verified int f(...)`                                 |                Yes | Runtime C++ body with compile-time verification                          |
| Require a caller condition          | `expects (...)`                                       |                 No | Function precondition or Law premise                                     |
| State a runtime postcondition       | `ensures (...)`                                       |                 No | Guarantee after normal function return                                   |
| State a theorem                     | `law L(...) proves (...);`                            |                 No | Compile-time theorem                                                     |
| State a theorem with explicit proof | `law L(...) proves (...) { ... }`                     |                 No | Law plus authored proof body                                             |
| Define reusable proof evidence      | `proof P(...) proves (...) { ... }`                   |                 No | Named compile-time evidence                                              |
| Define a refined type               | `type Positive = int where (self > 0);`               |    Base value only | Verification-level restriction over a C++ type                           |
| Define an indexed refinement        | `type Index(unsigned n) = unsigned where (self < n);` |    Base value only | Refinement parameterized by proof-level index metadata                   |
| Mark an effect-free function        | `pure int f(...)`                                     |                Yes | Runtime function checked for purity                                      |
| Introduce proof-only local state    | `ghost int snapshot = value;`                         |                 No | Verification-only local; erased                                          |
| State a loop invariant              | `invariant (...)`                                     |                 No | Property preserved across loop iterations                                |
| Prove termination                   | `decreases (...)`                                     |                 No | Well-founded measure for recursion or loops                              |
| Split proof states                  | `cases value { ... }`                                 |                 No | Proof-only sum/state decomposition                                       |
| Decompose product fields            | `decompose value { ... }`                             |                 No | Proof-only product decomposition                                         |
| Prove by induction                  | `induction value { ... }`                             |                 No | Proof using base/step cases and induction hypothesis                     |
| Admit an explicit trusted theorem   | `trusted law L(...) proves (...);`                    |                 No | Assumption proofs use by name; the trust report lists what rests on it   |
| Mark unchecked runtime operations   | `unsafe { ... }`                                      | Operations execute | Runtime code executes; verifier does not infer correctness from `unsafe` |
| Validate runtime input              | ordinary runtime validator                            |                Yes | Runtime check that may establish evidence for verified code              |

### Function contracts

Canonical order:

```cpp
verified int f(int x)
    expects (x >= 0)
    ensures (result >= 0)
    decreases (measure)
{
    // ordinary C++ runtime body
}
```

Meaning:

```text
expects (...)
    caller must establish this before the call

ensures (...)
    function body must establish this on every normal return

decreases (...)
    execution must make this well-founded measure decrease
```

A verified function still runs at runtime.

Contracts do not insert hidden runtime checks.

A member function takes the same clauses, after its qualifiers:

```cpp
struct Counter {
    unsigned value;

    verified void bump() &
        expects (value < 100u)
        ensures (value <= 100u)
    {
        value = value + 1u;
    }
};
```

```text
member names in a clause
    the object's members, by ordinary member lookup and `this`

expects
    reads the object on entry

ensures
    reads the object where the function returns

const
    the call leaves the object as it was, unless a `mutable` member is written

virtual, constructors, destructors, member templates
    refused by this implementation
```

### Laws

Automatic proof:

```cpp
law identity(unsigned x)
    proves (x + 0u == x);
```

Explicit proof:

```cpp
law identity(unsigned x)
    proves (x + 0u == x)
{
    refl;
}
```

A Law:

```text
is a compile-time theorem
has no runtime body
has no runtime result
uses `proves`, never `ensures`
may have an `expects` premise
is erased before native compilation
```

Canonical Law order:

```cpp
law theorem(...)
    expects (...)
    proves (...);
```

### `law` versus `proof`

```text
law
    = theorem / proposition that other verification may rely on

proof
    = explicitly named reusable evidence
```

Example:

```cpp
law reflexivity(int x)
    proves (Eq<int>(x, x));

proof reflexivity_evidence(int x)
    proves (Eq<int>(x, x))
{
    refl;
}
```

Neither produces a runtime function.

A Law may contain its explicit proof directly, so a separate `proof` is needed
only when separately named reusable evidence is useful.

### Proof commands

| Command                   | Purpose                                                          |
| ------------------------- | ---------------------------------------------------------------- |
| `refl;`                   | Close a definitionally reflexive equality                        |
| `exact evidence;`         | Finish the current goal with evidence that already matches it    |
| `apply theorem;`          | Apply evidence/theorem and reduce the goal to remaining premises |
| `assume h : P;`           | Name a premise already supplied by the proof context             |
| `rewrite h;`              | Rewrite the current goal using checked equality evidence         |
| `contradiction h;`        | Close the goal because the context here cannot occur             |
| `cases value { ... }`     | Split proof by possible states                                   |
| `decompose value { ... }` | Expose product components                                        |
| `induction value { ... }` | Prove recursively using an induction hypothesis                  |

The evidence `exact`, `apply`, `rewrite` and `contradiction` name is a `proof`,
a premise named by `assume`, or a `trusted law`. Naming a trusted law makes the
proof rest on it, and so everything that uses that proof in turn (§12.2).

Quick choice:

```text
goal is definitionally obvious
    -> refl

already have exact evidence
    -> exact

have theorem P -> Q and goal Q
    -> apply

have equality useful for transforming goal
    -> rewrite

have evidence the context here cannot hold together with
    -> contradiction

need state split
    -> cases

a state cannot occur under the premises
    -> omit label by contradiction evidence; inside the cases

need field/product projections
    -> decompose

need recursive proof
    -> induction
```

`assume` never creates arbitrary truth. It only names evidence already supplied
by the current proof context.

### Logical quantifiers

`forall` and `exists` are C++L proof/specification constructs.

```cpp
forall (unsigned x) {
    P(x)
}
```

means:

```text
P holds for every unsigned x
```

```cpp
exists (unsigned x) {
    P(x)
}
```

means:

```text
there exists at least one unsigned x for which P holds
```

Example:

```cpp
law reflexive_for_all()
    proves (
        forall (unsigned x) {
            Eq<unsigned>(x, x)
        }
    );
```

Quantifiers do not generate runtime loops.

Their domain is determined by the binder type:

```text
unsigned
    finite C++ machine domain

@N
    mathematical natural numbers

@Z
    mathematical integers
```

### Special specification identifiers

| Identifier  | Meaning                           | Valid context            |
| ----------- | --------------------------------- | ------------------------ |
| `result`    | Value returned by the function    | Non-void `ensures`       |
| `old(expr)` | Value of `expr` at function entry | Function postcondition   |
| `self`      | Value being refined               | Refinement `where (...)` |

Example:

```cpp
verified void increment(unsigned& value)
    ensures (value == old(value) + 1u)
{
    ++value;
}
```

Example refinement:

```cpp
type Percentage = unsigned where (self <= 100u);
```

Outside their C++L contexts, these names remain ordinary C++ identifiers where
the C++ grammar permits them.

### Refinements

Basic refinement:

```cpp
type Positive = int where (self > 0);
```

Nested refinement:

```cpp
type NonNegative = int where (self >= 0);
type Percentage = NonNegative where (self <= 100);
```

Indexed refinement:

```cpp
type Index(unsigned n) = unsigned where (self < n);
```

Application:

```cpp
Index<4u>
```

Important rules:

```text
refinement identity
    verification-only

runtime representation
    underlying C++ base type

base -> refinement
    requires proof or runtime validation

stronger refinement -> weaker refinement
    requires checked implication

write into refined storage
    must re-establish its predicate

possible alias mutation
    may invalidate previously known refinement facts
```

Refinements do not create hidden wrappers, tags or runtime validation.

### Runtime validation

Use runtime validation when the value cannot be known until execution.

Typical sources:

```text
network
file
database
JSON
command line
device
OCR
FFI
user input
```

Typical flow:

```text
ordinary runtime value
    |
    v
runtime validation
    |
    +---- failure -> ordinary runtime error path
    |
    v
refined value
    |
    v
verified code
```

Runtime validation:

```text
executes at runtime
survives erasure
is not a compile-time theorem
is not the same as `trusted`
```

### Ghost state

```cpp
ghost unsigned original = value;
```

`ghost` state:

```text
exists only during verification
is erased
may support proofs and invariants
must never affect runtime computation
```

This is invalid:

```cpp
verified unsigned bad(unsigned value)
{
    ghost unsigned snapshot = value;
    return snapshot;
}
```

because runtime output would depend on erased state.

### Cases and decomposition

Sum/state decomposition:

```cpp
cases value {
    some(payload) => {
        ...
    }

    none => {
        ...
    }
}
```

Product decomposition:

```cpp
decompose point {
    components(x, y) => {
        ...
    }
}
```

The state providers are:

```text
scoped enum         Enum::name..., unnamed(value)
std::variant        alternative<i>(value)..., valueless
std::optional       some(value), none
std::expected       value(payload), error(reason)      (C++23)
pointer             null, non_null
```

Products are records, `std::pair`, `std::tuple`, `std::array` and built-in
arrays, each with one `components(...)` arm.

The last state of each sum is its residual, derived by the engine as none of the
others holding. `_` is not a proof catch-all.

An omitted state is legal only when the verifier proves that state impossible
from the existing proof context.

In a verified body, `cases` and `decompose` split the rest of the path by the
subject's states where they are written (§9.1). An arm there holds only nested
splits and a `contradiction` claim.

### Induction

```cpp
induction n {
    zero => {
        ...
    }

    successor(pred) => {
        assume ih : ...;
        ...
    }
}
```

Remember:

```text
cases
    possible states

induction
    possible recursive states + induction hypothesis

decreases
    runtime termination
```

These are different concepts.

### Loops

Canonical loop:

```cpp
while (i < n)
    invariant (i <= n)
    decreases (n - i)
{
    ++i;
}
```

```text
invariant (...)
    proves loop correctness

decreases (...)
    proves termination
```

Without required termination proof, verification may establish partial
correctness only.

### Arithmetic

```text
signed + - * and unary -       owe: the exact result fits the type
/ and %                        owe: divisor != 0, and signed: not MIN / -1
conversion to a signed type    owes: the value fits the target
unsigned + - * and unary -     wrap modulo 2^width, owe nothing
```

Each is owed where the operation runs, from what the path knows there, and
holds afterwards. See section 13.3.

### Purity

```cpp
pure unsigned identity(unsigned value)
{
    return value;
}
```

Canonical verified combination:

```cpp
verified pure unsigned identity(unsigned value)
    ensures (result == value)
{
    return value;
}
```

Do not confuse:

```text
pure
    effect property

const
    ordinary C++ qualifier

constexpr
    ordinary C++ constant-evaluation facility

decreases
    termination proof

verified
    contract/body verification
```

### Pointers and references

A non-null pointer proves only non-nullness.

```text
pointer != nullptr
```

does not by itself prove:

```text
valid lifetime
readability
writability
initialization
bounds
provenance
ownership
```

Likewise:

```text
const reference
    != globally immutable object
```

Another alias may still mutate the same storage.

A possible alias write may invalidate facts about the aliased value.

### Trusted versus unsafe

`trusted` and `unsafe` are not synonyms.

```text
trusted
    explicitly admits verification trust/evidence

unsafe
    permits a runtime operation whose safety was not established
```

Example:

```cpp
trusted law external_guarantee(...)
    proves (...);
```

Example:

```cpp
unsafe {
    operation();
}
```

`unsafe` does not magically produce proof evidence.

`trusted` must remain visible in trust reporting.

### Verification boundaries

Entering verified code requires the required facts to be established.

```text
verified caller
    proves callee `expects`

unverified caller
    gets no hidden runtime contract check

external runtime input
    should normally be validated before entering verified core
```

Recommended architecture:

```text
ordinary / unverified shell
    |
    | I/O, networking, parsing, OS APIs
    v
runtime validation
    |
    v
verified C++L core
    |
    v
ordinary native execution
```

### Headers and source files

Recommended organization:

```text
public contract
    -> .h / .hpp / interface

runtime implementation
    -> .cpp

shared refinement
    -> header/interface

shared Law
    -> header/interface

implementation-only Law/proof
    -> source

ghost local
    -> inside verified/proof body
```

Header:

```cpp
verified unsigned withdraw(unsigned balance, unsigned amount)
    expects (amount <= balance)
    ensures (result == balance - amount);
```

Source:

```cpp
unsigned withdraw(unsigned balance, unsigned amount)
{
    return balance - amount;
}
```

Do not duplicate the contract on the out-of-line definition, unless its body
states loop clauses; then it restates the same contract (15.1).

Another unit calling it:

```text
cppl -c account.cpp --cppl-emit-interface=account.cppli     proves, records
cppl -c main.cpp --cppl-import-interface=account.cppli      uses, reports
```

### Templates, virtual functions and other C++ syntax

These are ordinary C++, not C++L constructs:

```text
template
requires          // C++ constraints
virtual
override
final
constexpr
consteval
const
noexcept
if
switch
for
while
sizeof
alignof
decltype
```

C++L may add verification semantics around them.

For example:

```cpp
template <typename T>
verified T identity(T value)
    ensures (result == value)
{
    return value;
}
```

Here:

```text
template
    = C++

verified / ensures
    = C++L
```

Similarly:

```cpp
virtual verified unsigned withdraw(unsigned amount)
    expects (...)
    ensures (...) = 0;
```

Here:

```text
virtual
    = C++

verified / expects / ensures
    = C++L
```

The grammar admits this declaration. This implementation refuses it, since it
does not yet check that every override honours the base contract (`SPEC.md`
CLASS-014).

### C++L-specific vocabulary

The main C++L language surface includes:

```text
verified
pure
law
proof
proves
expects
ensures
decreases
invariant
type
where
ghost
trusted
unsafe
forall
exists
cases
decompose
induction
refl
exact
apply
assume
rewrite
```

Special contextual specification identifiers:

```text
result
old
self
```

Mathematical proof-only domains:

```text
@N
@Z
@Seq<T>
@Set<T>
@Map<K, V>
```

These should remain contextual wherever possible rather than unnecessarily
becoming globally reserved C++ keywords.

### Canonical formatting

Clause-like syntax:

```cpp
expects (...)
ensures (...)
proves (...)
invariant (...)
decreases (...)
where (...)
```

Expression-like syntax:

```cpp
old(value)
```

Canonical contract order:

```text
verified function:
    expects
    ensures
    decreases

Law:
    expects
    proves

loop:
    invariant
    decreases
```

Whitespace before `(` in clauses is formatting, not semantic syntax. The parser
may accept:

```cpp
ensures(result == x)
```

but the formatter emits:

```cpp
ensures (result == x)
```

### Runtime / proof-only summary

| Construct                                         | Runtime? |
| ------------------------------------------------- | -------: |
| Ordinary C++ function body                        |      Yes |
| `verified` function body                          |      Yes |
| `pure` function body                              |      Yes |
| `unsafe` operations                               |      Yes |
| Runtime validation                                |      Yes |
| `expects`                                         |       No |
| `ensures`                                         |       No |
| `proves`                                          |       No |
| `law`                                             |       No |
| `proof`                                           |       No |
| `refl` / `exact` / `apply` / `assume` / `rewrite` |       No |
| `forall` / `exists`                               |       No |
| `cases` / `decompose` / `induction`               |       No |
| `ghost`                                           |       No |
| `invariant` / `decreases`                         |       No |
| Refinement predicate/index metadata               |       No |
| Refinement base value                             |      Yes |
| `trusted law`                                     |       No |

### The shortest mental model

```text
Runtime C++:
    ordinary C++ bodies
    verified bodies
    pure bodies
    unsafe operations
    runtime validators

Function contract:
    expects -> ensures

Theorem:
    expects -> proves

Explicit evidence:
    proof

Proof commands:
    refl / exact / apply / assume / rewrite

Logical reasoning:
    forall / exists
    cases / decompose / induction

Types:
    type ... where (...)

Loop correctness:
    invariant (...)

Termination:
    decreases (...)

Proof-only state:
    ghost

Explicit trust:
    trusted

Unchecked runtime operation:
    unsafe
```

And the central rule:

```text
Functions ensure.
Laws prove.

C++ runs.
C++L proves properties about what runs.
```
