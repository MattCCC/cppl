# Writing C++L

C++L adds checked contracts, Laws, proofs and refinements to ordinary C++.

**Functions ensure. Laws prove.** Runtime bodies remain C++; specifications and
proofs are checked before erasure and native compilation.

[SPEC.md](SPEC.md) defines meaning and [the grammar](docs/GRAMMAR.md) defines
syntax. This guide uses their canonical spelling. [STATUS.md](STATUS.md) records
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
| `forall`                                      | None                              | Removed with specification/proof                                           |
| `exists`                                      | None                              | Removed with specification/proof                                           |
| `cases`                                       | None                              | Removed with proof                                                         |
| `decompose`                                   | None                              | Removed with proof                                                         |
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
postcondition. An expects-only function still has to satisfy its body safety
obligations. Here unsigned arithmetic avoids signed overflow:

<!-- cppl-example: verify -->

```cpp
verified unsigned next(unsigned x)
    expects (x < 100u)
{
    return x + 1u;
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

An evidence reference may itself have arguments.

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
there is at least one unsigned x for which P(x) holds
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

For example:

```cpp
law every_percentage_is_bounded()
    proves (
        forall (unsigned x) {
            x <= 100u -> x <= 100u
        }
    );
```

does not generate a loop over all `unsigned` values. The verifier reasons about
the quantified proposition symbolically.

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

For example:

```cpp
law an_unsigned_zero_exists()
    proves (exists (unsigned x) { x == 0u });
```

must ultimately be justified by evidence corresponding to a concrete witness such
as `0u`, according to the proof grammar.

A useful mental model is:

```text
forall
    = introduce an arbitrary value and prove the property for it

exists
    = provide a witness and prove the property for that witness
```

Quantified variables are proof/specification binders. They do not introduce
runtime variables, storage, allocation, object lifetime or ABI-visible state.

The canonical syntax is:

```cpp
forall (Type name) {
    proposition
}

exists (Type name) {
    proposition
}
```

Inside `proves (...)`, this becomes:

```cpp
law example()
    proves (
        forall (unsigned x) {
            exists (unsigned y) {
                y == x
            }
        }
    );
```

`forall` and `exists` are contextual C++L words. Outside a C++L specification
context, ordinary C++ identifiers with those names remain ordinary C++ where
the grammar permits them.

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

## 8. Ghost locals

`ghost` prefixes a local declaration in a verification-enabled block. Its value
exists only for verification.

There are no ghost runtime parameters, members or globals in this grammar.

A snapshot can support an invariant:

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

Ghost values may also support proof steps:

```cpp
proof ghost_bookkeeping(unsigned x)
    proves (Eq<unsigned>(x, x))
{
    ghost unsigned snapshot = x;
    refl;
}
```

Their initializers must be specification-safe. Ghost bookkeeping cannot perform
observable mutation or require runtime copies/destruction.

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

The return is runtime behavior and would depend on erased state.

The same rule forbids ghost-dependent:

```text
runtime branches
addresses
I/O
object layout
FFI arguments
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

        non_null(address) => {
            refl;
        }
    }
}
```

It proves no:

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
    }
}
```

Omitting `non_null` requires checked evidence of contradiction between the entry
premise and that state's discriminator.

There is no heuristic omission.

If the verifier cannot establish that contradiction, the omitted arm is an error.

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

An unsigned bound is finite; arbitrary signed subtraction requires its
definedness and lower bound to be proven.

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

A range-for uses that same location.

A `do` loop places clauses after `do`, before its body, keeping the trailing
`while` in its ordinary C++ position:

```cpp
verified unsigned one_iteration()
    ensures (result == 1u)
{
    unsigned i = 0u;

    do
        invariant (i <= 1u)
    {
        ++i;
    } while (i < 1u);

    return i;
}

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

Recursive termination uses the same measure syntax:

```cpp
verified unsigned descend(unsigned n)
    ensures (result == 0u)
    decreases (n)
{
    return n == 0u ? 0u : descend(n - 1u);
}
```

`decreases (outer, inner)` is one lexicographic measure list, not two clauses.

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

```cpp
unsafe unsigned read_device();

unsigned poll_device()
{
    unsigned value = 0u;

    unsafe {
        value = read_device();
    }

    return value;
}
```

These are the unsafe function-declaration and block forms; there is no unsafe
expression form.

Runtime operations still execute.

Unsafe code cannot produce proof evidence or refinement facts merely because it
is marked `unsafe`.

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

Some values cannot be known at compile time.

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

C++L must not pretend to prove the value before execution.

The normal flow is:

```text
untrusted runtime value
    |
    v
runtime validation
    |
    +---- failure -> ordinary runtime error/result path
    |
    v
refined / validated value
    |
    v
verified code
```

Conceptually:

```cpp
int raw = read_from_network();
auto percentage = validate<Percentage>(raw);
```

If `raw` is `50`, runtime validation may succeed and provide a value carrying the
evidence required by `Percentage`.

If `raw` is `150`, validation fails.

The exact validation API is ordinary runtime code/library surface; the important
C++L rule is that successful validation must establish the refinement predicate
before the value enters verified code.

This is:

```text
RUNTIME-CHECKED
```

not:

```text
PROVEN-FROM-NOTHING
```

and not:

```text
TRUSTED
```

A validator executes at runtime and is preserved after erasure.

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

Check [STATUS.md](STATUS.md) for current semantic coverage.

This distinction matters:

```text
valid C++
    does not automatically mean
currently verifiable C++

unsupported verification
    does not mean
invalid C++
```

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

A compiler lacking separate-evidence transport must reject that verification
step.

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

## 16. Member contracts

Use member lookup and `this`, not a second meaning for `self`.

Put a public member contract on its class declaration:

```cpp
class Account {
public:
    unsigned balance_;

    verified unsigned withdraw(unsigned amount)
        expects (amount <= balance_)
        ensures (result == old(balance_) - amount && balance_ == result);

    verified pure unsigned balance() const
        ensures (result == balance_);
};

unsigned Account::withdraw(unsigned amount)
{
    balance_ -= amount;
    return balance_;
}

unsigned Account::balance() const
{
    return balance_;
}
```

The out-of-line definitions inherit their declarations.

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

Use `cppl` with ordinary Clang compile options.

For example:

```sh
build/dev/bin/cppl -std=c++20 -fsyntax-only source.cpp
```

runs verification without linking a native executable.

Consult [TRUST.md](TRUST.md) for trust-report meaning and
[tools/cppl-lsp/README.md](tools/cppl-lsp/README.md) for editor setup.

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

## 20. C++L cheat sheet

| Goal / canonical form                                 | Runtime?           | Main context / usual location              |
| ----------------------------------------------------- | ------------------ | ------------------------------------------ |
| `verified int f(int x)`                               | Body runs          | Public contract in header; body in source  |
| `expects (x > 0)`                                     | No                 | Function precondition / Law premise        |
| `ensures (result == x)`                               | No                 | Runtime function postcondition             |
| `law L(int x) proves (x == x);`                       | No                 | Shared header or private source theorem    |
| `proof P(int x) proves (Eq<int>(x, x)) { refl; }`     | No                 | Reusable evidence                          |
| `type Positive = int where (self > 0);`               | Base only          | Shared type declaration in header          |
| `type Index(unsigned n) = unsigned where (self < n);` | Base only          | Indexed refinement, applied as `Index<4u>` |
| `pure unsigned f(unsigned x)`                         | Body runs          | Checked effect-free function               |
| `invariant (i <= n)`                                  | No                 | Loop clause in runtime implementation      |
| `decreases (n - i)`                                   | No                 | Function/loop termination measure          |
| `ghost unsigned original = x;`                        | No                 | Verification-only local                    |
| `cases value { Label => { refl; } }`                  | No                 | Proof state split                          |
| `decompose point { components(x, y) => { refl; } }`   | No                 | Proof product projections                  |
| `induction n;`                                        | No                 | Proof with domain induction principle      |
| `trusted law boundary(...) proves (...);`             | No                 | Explicit universal trusted assumption      |
| `unsafe { operation(); }`                             | Operations execute | Explicit unsafe block                      |
| Runtime validator                                     | Yes                | External/runtime value boundary            |

Quick mental model:

```text
Runtime function:
    verified + expects / ensures

Theorem:
    law + expects / proves

Explicit reusable evidence:
    proof + proves

Refined type:
    type ... where (...)

Loop correctness:
    invariant (...)

Termination:
    decreases (...)

Proof-only state:
    ghost

Case proof:
    cases

Product proof:
    decompose

Inductive proof:
    induction

Explicit trust:
    trusted law

Potentially unsafe runtime operation:
    unsafe

External runtime data:
    runtime validation -> refined/verified value
```

And the most important distinction:

```text
Functions ensure.
Laws prove.

verified function body
    -> runs

Law
    -> does not run

proof
    -> does not run

ghost
    -> does not run

runtime validation
    -> runs
```
