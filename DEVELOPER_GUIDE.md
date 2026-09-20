# C++L Developer Guide

C++L is **C++ with Laws**.

It is a source-compatible superset of C++ that lets developers express formal intent and prove that implementations satisfy it.

The basic workflow is:

```text
write normal C++
    ↓
add a Law
    ↓
prove the implementation satisfies it
    ↓
C++L checks the proof
    ↓
proof-only information is erased
    ↓
ordinary C++ is compiled with Clang/LLVM
```

You do not need to rewrite an existing C++ project to start using C++L.

Verification is additive.

---

# 1. Start with normal C++

Ordinary supported C++ remains valid C++L.

```cpp
#include <algorithm>

int clamp(int value, int low, int high) {
    return std::min(std::max(value, low), high);
}
```

You should be able to compile ordinary C++ through:

```bash
cppl clamp.cpp
```

without adding Laws, proofs, refined types, or other C++L constructs.

C++L does not require every function to be formally verified.

---

# 2. Add formal intent with a Law

A `law` describes something that must be true.

Conceptually:

```cpp
law clamp_bounds(int value, int low, int high)
    ...
```

A Law is stronger than a unit test.

A test checks selected inputs.

A Law expresses a proposition that must hold for all values covered by its assumptions.

For example:

```text
for every value, low, and high:

if low <= high

then clamp(value, low, high)
must return a value between low and high
```

Conceptually:

```cpp
law clamp_bounds(int value, int low, int high)
    expects low <= high
    ensures result >= low && result <= high;
```

The exact surface syntax is defined by `SPEC.md`.

---

# 3. Laws are not tests

This:

```cpp
assert(clamp(5, 0, 10) == 5);
assert(clamp(-4, 0, 10) == 0);
assert(clamp(20, 0, 10) == 10);
```

checks three examples.

A Law can express the general property:

```text
∀ value low high,
    low <= high
    →
    low <= clamp(value, low, high) <= high
```

That is the difference between:

```text
examples
```

and:

```text
formal intent
```

Tests still matter.

They are not proofs.

---

# 4. `law`

Use `law` to declare a proposition that an implementation is expected to satisfy.

Example:

```cpp
law absolute_nonnegative(int x)
    ensures abs(x) >= 0;
```

A Law may describe:

- return-value constraints
- arithmetic invariants
- state transitions
- ownership invariants
- parser invariants
- conservation rules
- impossible states
- relationships between inputs and outputs

Example:

```cpp
law withdrawal_preserves_balance(
    int old_balance,
    int amount
)
    expects amount >= 0
    expects amount <= old_balance
    ensures result == old_balance - amount;
```

A Law is part of the specification.

Do not weaken it merely because the current implementation cannot prove it.

---

# 5. Preconditions and postconditions

C++L can express facts that must hold before and after an operation.

Conceptually:

```cpp
int divide(int numerator, int denominator)
    expects denominator != 0
    ensures result == numerator / denominator;
```

`expects` describes what must be true before execution.

`ensures` describes what must be true after successful execution.

This is conceptually related to:

```text
{ P } program { Q }
```

from Hoare logic.

Where:

```text
P = precondition
Q = postcondition
```

Do not use ordinary C++ `requires` as a C++L contract keyword.

`requires` already has C++ meaning.

---

# 6. `proof`

A `proof` provides machine-checkable evidence that a proposition is true.

Conceptually:

```cpp
proof clamp_bounds_proof(...)
{
    ...
}
```

A proof is not:

- a comment
- a test
- an assertion
- an AI explanation
- a solver success flag

The proof must ultimately be accepted by the C++L proof kernel.

Conceptually:

```text
Law
    ↓
proof evidence
    ↓
kernel
    ├── valid   → PROVEN
    └── invalid → rejected
```

---

# 7. Simple equality proof

Proofs are about ordinary C++ functions and types. Suppose we have:

```cpp
pure unsigned identity(unsigned x) {
    return x;
}
```

We can state:

```cpp
law identity_returns_input(unsigned x)
    ensures(identity(x) == x);
```

The proof is immediate because the expression reduces definitionally:

```cpp
proof identity_returns_input_holds(unsigned x)
    proves(identity_returns_input(x))
{
    refl;
}
```

Conceptually:

```text
identity(x)
    ↓ normalize
x
```

Therefore:

```text
identity(x) = x
```

No new data type is needed. C++L reasons about `unsigned` as C++ defines it.

---

# 8. `pure`

`pure` describes computation without observable side effects.

Conceptually:

```cpp
pure int square(int x) {
    return x * x;
}
```

A pure function should not unexpectedly:

- mutate external state
- perform I/O
- modify globals
- depend on hidden mutable state

Purity is useful because mathematical reasoning becomes much simpler when:

```text
same input
→
same result
```

But `pure` must have precise semantics.

It is not merely documentation.

---

# 9. `verified`

`verified` identifies code or interfaces whose required proof obligations have been discharged according to C++L's verification rules.

Conceptually:

```cpp
verified int bounded_add(int a, int b)
    ...
```

Do not confuse:

```text
compiled
```

with:

```text
verified
```

or:

```text
tested
```

with:

```text
verified
```

Verification status must remain explicit.

---

# 10. Verification statuses

C++L distinguishes different assurance levels.

Typical statuses include:

```text
PROVEN
TRUSTED
RUNTIME-CHECKED
UNSAFE
UNVERIFIED
UNRESOLVED
```

These are intentionally different.

For example:

```text
PROVEN
```

means the proposition has machine-checkable proof evidence accepted according to the current formal system.

```text
TRUSTED
```

means some assumption is being accepted rather than proven.

```text
RUNTIME-CHECKED
```

means a property is established by validating a concrete runtime value.

```text
UNVERIFIED
```

means ordinary code may still compile, but C++L is not claiming the property has been proven.

---

# 11. Existing C++ can remain unverified

Suppose you already have:

```cpp
int legacy_calculate(int x) {
    return some_old_library(x);
}
```

You should not have to rewrite it immediately.

C++L can treat it as ordinary C++:

```text
compiles
but
not formally verified
```

Then you can gradually move important parts into verified regions.

This allows migration like:

```text
Day 1
100% ordinary C++

Later
90% ordinary C++
10% verified

Later
50% ordinary C++
50% verified
```

There is no requirement to convert an entire project at once.

---

# 12. Refined types

A refinement type restricts which values belong to a type.

Example:

```cpp
type Percentage =
    int where self >= 0 && self <= 100;
```

Conceptually this means:

```text
Percentage =
{ x : int | 0 <= x <= 100 }
```

A function using it:

```cpp
Percentage discount();
```

can rely on:

```text
0 <= result <= 100
```

provided construction of `Percentage` is properly verified or runtime-validated.

The syntax the compiler accepts today parenthesizes the predicate, and an indexed
refinement names its indices:

```cpp
type Percentage = int where(self >= 0 && self <= 100);
type Index(unsigned n) = unsigned where(self < n);
```

At runtime a `Percentage` *is* an `int`. The declaration lowers to `using
Percentage = int;` and nothing else: no wrapper, no check, no layout change. What
the refinement adds is compile time only:

```cpp
verified int clamped(int x) ensures(result >= 0) {
    if (x >= 0) {
        Percentage p = x;   // the branch proves 0 <= x; 0 <= x <= 100 is owed here
        return p;
    }
    return 0;
}
```

Every value that enters the type owes its predicate where it enters, and the fact
that a branch established it is enough. Going the other way is free: a
`Percentage` is usable wherever an `int` is, and a refined parameter's predicate is
already known inside the body, so there is no need to repeat it as an `expects`
clause.

A refinement of a refinement keeps both predicates, so a value entering the inner
one owes all of them.

Every flow into the type owes the predicate, not only a declaration. An assignment
carries the local's declared type, so this is caught:

```cpp
Percentage p = 0;
p = x;              // owes 0 <= x <= 100 here, exactly as the declaration did
```

and so is passing `x` where a verified function takes a `Percentage`.

Crossing between two refinements of one base type is the implication between their
predicates. Going from the stricter to the looser costs nothing, because the value
already carries what the looser one asks:

```cpp
type NonNegative = int where(self >= 0);
type Percentage = NonNegative where(self <= 100);

verified int widened(Percentage p) ensures(result >= 0) {
    NonNegative n = p;   // p >= 0 is part of what Percentage already gives
    return n;
}
```

The other direction owes the part that does not follow - here `p <= 100`. No runtime
check is inserted either way; there is nothing to check, since both types are `int`.

One consequence of that erasure is worth knowing: two overloads distinguished only
by which refinement they name are the same C++ function, and the compiler says so at
the declaration.

---

# 13. Runtime validation

Some values cannot be known until execution.

For example:

```cpp
int raw = read_from_network();
```

You cannot prove at compile time which value the network will send.

Instead:

```text
network value
    ↓
runtime validation
    ↓
Percentage
```

Conceptually:

```cpp
auto percentage = validate<Percentage>(raw);
```

If `raw` is `50`, validation succeeds.

If `raw` is `150`, validation fails.

The resulting value may then safely enter verified code.

This is:

```text
RUNTIME-CHECKED
```

not compile-time theorem proving.

---

# 14. Impossible states

C++L should encourage modeling invalid states so they cannot be constructed.

For example, instead of:

```cpp
struct Payment {
    bool succeeded;
    bool failed;
};
```

which permits:

```text
succeeded = true
failed = true
```

prefer a C++ type like:

```cpp
using PaymentResult = std::variant<Receipt, Error>;
```

Now contradictory states are structurally impossible.

The variant example below describes the specified direction. `cases` itself is
representation-independent: what states a value has comes from a decomposition
provider for its resolved C++ type, and everything else — arm matching, binders,
exhaustiveness, evidence, erasure — is shared. One provider exists today, for
scoped enumerations (SPEC.md 20.5), whose states include an explicit residual
arm:

```cpp
enum class Flag : unsigned { set = 1u };
proof flag_identity(Flag flag) proves(flag == flag) {
    cases flag {
        Flag::set => { assume selected : flag == Flag::set; rewrite selected; refl; }
        unnamed(value) => { assume other : value != 1u; refl; }
    }
}
```

`value` has type `unsigned`, and `other` names evidence supplied by the residual
path. A failed written arm is an error even when automation could prove the
enclosing proposition.

Variants, optionals, expected, pointers and products have no provider yet, and
`cases` on them is refused by name. The reason is the formal value model, not
the case engine: the core's terms range over machine integers, so there is
nothing to discriminate a variant's alternative or a pointer's nullness on.
ROADMAP.md sequences that work. Omission of impossible arms is also still
refused.

### Adding a representation

Implement one provider under `compiler/decomposition/` and register it, then add
its semantic tests. A provider answers three questions for a resolved type:
which cases exist, what condition holds in each, and which case a written label
denotes. It supplies no evidence, no lowering, no diagnostics and no parsing —
those already exist once, for every representation. If the representation
reserves a label for a state with no C++ expression, add it to
`decomposition/labels.hpp` beside the provider.

Executable code keeps branching with ordinary C++, such as `std::visit`. A proof can split the value into its cases:

```cpp
proof settle_is_total(PaymentResult result)
    proves(...)
{
    cases result {
        Receipt(receipt) => {
            ...
        }

        Error(error) => {
            ...
        }

        valueless => {
            ...
        }
    }
}
```

Each arm is a separate proof obligation. The binders `receipt` and `error` name the value each alternative holds. `cases` generates no runtime code.

Cases follow C++ semantics, not just the declared names. A `std::variant` can become valueless when an exception interrupts an assignment, so `valueless` is a case too. It needs an arm unless the proof context shows it cannot occur. Likewise, an `enum class` value can match no enumerator, which is the `unnamed(value)` case.

There is no catch-all `_` arm. A proof never silently covers a state it did not consider, and adding an enumerator later makes every proof that ignores it fail. See `SPEC.md` §20.

---

# 15. Induction

Some Laws describe infinitely many values.

You do not prove them by testing infinitely many examples.

You prove them symbolically.

You do not need to define natural numbers to do this. Induction works on the C++ types you already have.

For an `unsigned` value:

```text
1. prove the property for 0u
2. assume the property for n, where n is below the type's maximum
3. prove the property for n + 1u
4. conclude the property for every unsigned value
```

The step never wraps past the maximum, so the principle matches runtime `unsigned` arithmetic.

Given:

```cpp
pure unsigned add(unsigned a, unsigned b)
    decreases(a)
{
    return a == 0u ? b : add(a - 1u, b) + 1u;
}
```

the proof is:

```cpp
proof add_zero(unsigned x)
    proves(add(x, 0u) == x)
{
    induction x;
}
```

The short form leaves each case to proof automation, whose evidence the kernel still checks.

Cases can also be written out. Every arm uses the same `label(binders) => { ... }` form as `cases`:

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

The binder `pred` names the predecessor. The induction hypothesis is not a binder: the principle already supplies it, and `assume` gives it a name. If the stated proposition does not match what the principle supplies, the proof is rejected. The same holds for structures with several recursive parts:

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

A pointer alone does not support induction, because a `Node*` may be cyclic, dangling, or shared. Induction over a linked structure such as this tree needs an explicit well-founded premise, such as a proven finite, acyclic shape. Without one it is rejected.

`induction` is verifier machinery, not runtime branching. It generates no runtime code.

---

# 16. Termination

Proof-producing computation must terminate.

Otherwise a language could accidentally permit something like:

```cpp
proof impossible() {
    return impossible();
}
```

and pretend that divergence produced evidence.

C++L therefore tracks termination where logical consistency requires it.

Recursive functions may need to demonstrate structural descent:

```cpp
pure unsigned gcd(unsigned a, unsigned b)
    decreases(b)
{
    return b == 0u ? a : gcd(b, a % b);
}
```

or another well-founded measure.

---

# 17. Definitional equality

Some expressions are equal because computation reduces them to the same normal form.

Example, with `add` from §15:

```text
add(0u, x)
```

reduces to:

```text
x
```

Therefore the equality may require no separate theorem.

By contrast, `add(x, 0u) == x` does not reduce this way, because `add` recurses on its first argument. It needs induction (§15).

This is **definitional equality**.

---

# 18. Propositional equality

Other equalities require explicit evidence.

Conceptually:

```text
Proof<Eq<A, B>>
```

may be constructed using operations such as:

```text
reflexivity
symmetry
transitivity
rewrite
congruence
transport
```

Definitional equality and propositional equality are not interchangeable.

---

# 19. `ghost`

Ghost data exists only for verification.

Conceptually:

```cpp
ghost int original_balance = balance;
```

It may help prove something like:

```text
new_balance == original_balance - amount
```

but must disappear before runtime code generation.

Ghost information must not affect observable program behavior.

---

# 20. Proof erasure

Proofs and ghost state should normally have zero runtime cost.

Conceptually:

```cpp
proof something(...) {
    ...
}

ghost int x = ...;
```

becomes no runtime machine code after verification where the information is purely logical.

The pipeline is:

```text
C++L
    ↓
verify
    ↓
erase proof-only information
    ↓
ordinary C++
    ↓
Clang / LLVM
```

---

# 21. No theorem runtime

C++L does not require a theorem VM or proof runtime.

You still get an ordinary native executable.

There is no required:

- garbage-collected proof heap
- runtime theorem evaluator
- theorem VM
- special execution engine

Proofs exist primarily during compilation.

---

# 22. `unsafe`

Some operations cannot currently be verified safely.

They may need an explicit unsafe boundary.

Conceptually:

```cpp
unsafe {
    call_platform_assembly();
}
```

Unsafe code is not automatically wrong.

It means:

> C++L is not providing its strongest proof guarantee across this operation.

Unsafe boundaries must remain visible.

They must never silently become `PROVEN`.

---

# 23. `trusted`

Sometimes correctness depends on an assumption outside C++L.

Example:

```text
the operating system API obeys this contract
```

or:

```text
this foreign library behaves according to this specification
```

Such assumptions may be marked trusted.

Conceptually:

```cpp
trusted law operating_system_write_contract(...);
```

Trusted assumptions are not proofs.

If:

```text
Law A
depends on
Trusted assumption B
```

then the trust report must preserve that dependency.

---

# 24. FFI and existing libraries

C++L is intended to work with ordinary native libraries.

For example:

```cpp
#include <openssl/...>
#include <sqlite/...>
#include "legacy_company_library.hpp"
```

The code should still compile.

The important distinction is:

```text
can call
```

versus:

```text
formally verified
```

A foreign library may be:

```text
UNVERIFIED
TRUSTED
RUNTIME-CHECKED
```

depending on how the boundary is modeled.

You should not need to rewrite every dependency in C++L.

---

# 25. Existing `.h` and `.hpp` files

Normal C++ headers remain part of the project.

You can continue using:

```cpp
#include "account.hpp"
#include "payment.h"
```

C++L does not require a special header format.

Headers may also contain C++L declarations where supported.

For example:

```cpp
law valid_balance(const Account& account)
    ensures account.balance() >= 0;
```

Do not duplicate declarations unnecessarily between ordinary C++ and C++L files.

---

# 26. File extensions

Existing source files should continue to work:

```text
.cpp
.cc
.cxx
.h
.hpp
```

A dedicated extension such as:

```text
.cppl
```

may be used for C++L-heavy source if desired.

It should not be required merely to adopt C++L.

---

# 27. Compiler usage

The intended minimal migration is:

Before:

```bash
clang++ main.cpp -O2
```

After:

```bash
cppl main.cpp -O2
```

C++L should preserve ordinary compiler options where practical and forward native compilation to Clang/LLVM.

Existing build systems should require minimal changes.

For CMake:

```bash
cmake -DCMAKE_CXX_COMPILER=cppl ..
```

should be the target experience.

---

# 28. Incremental verification

A realistic existing project may look like:

```text
application
├── payment.cpp        VERIFIED
├── settlement.cpp     VERIFIED
├── parser.cpp         PARTIAL
├── rendering.cpp      UNVERIFIED
├── legacy.cpp         UNVERIFIED
└── third_party/
      └── ...          external
```

This is valid.

C++L should let developers expand the verified region gradually.

---

# 29. Example: account withdrawal

Start with normal C++:

```cpp
struct Account {
    int balance;
};

bool withdraw(Account& account, int amount) {
    if (amount < 0 || amount > account.balance) {
        return false;
    }

    account.balance -= amount;
    return true;
}
```

Now express the desired behavior.

Conceptually:

```cpp
law valid_withdrawal(
    Account before,
    Account after,
    int amount
)
    expects amount >= 0
    expects amount <= before.balance
    ensures after.balance == before.balance - amount;
```

The Law expresses what the implementation must preserve.

The implementation remains recognizably ordinary C++.

That is intentional.

---

# 30. Example: financial conservation

C++L is useful for application-level invariants.

Suppose:

```cpp
struct Basket {
    Money items;
    Money discounts;
    Money fees;
    Money total;
};
```

A Law could express:

```text
total
=
items
-
discounts
+
fees
```

Conceptually:

```cpp
law basket_closes(const Basket& basket)
    ensures basket.total
        == basket.items
         - basket.discounts
         + basket.fees;
```

Now an implementation cannot merely produce a plausible total.

It must satisfy the stated financial relation.

---

# 31. Example: parser invariant

Suppose a parser must never emit two competing authoritative totals.

A Law can describe:

```text
there exists exactly one authoritative settlement total
```

rather than relying on:

```text
tests
comments
engineering convention
```

The implementation can change significantly while the Law remains stable.

That is one of the main benefits of C++L.

---

# 32. Example: safe indexing

Ordinary C++:

```cpp
int read(const std::vector<int>& xs, std::size_t index) {
    return xs[index];
}
```

A C++L contract could require:

```cpp
int read(const std::vector<int>& xs, std::size_t index)
    expects index < xs.size();
```

Now the indexing operation is justified only under the required bound.

Alternatively, a refined index type could encode that relationship more strongly.

---

# 33. Example: non-zero divisor

Normal function:

```cpp
int divide(int x, int y) {
    return x / y;
}
```

Verification must account for:

```text
y != 0
```

A contract can express this:

```cpp
int divide(int x, int y)
    expects y != 0;
```

C++L should never simply assume division is safe.

Undefined behavior and machine semantics matter.

---

# 34. Mathematical integers vs machine integers

C++L distinguishes mathematical reasoning from actual C++ integer behavior.

For example:

```cpp
int x = INT_MAX;
int y = x + 1;
```

must not automatically be reasoned about as:

```text
2147483647 + 1 = 2147483648
```

because runtime C++ semantics matter.

Proofs involving machine arithmetic must use the correct machine model.

---

# 35. Unsupported does not mean uncompilable

A C++ feature may be valid but not yet formally modeled.

For example:

```cpp
asm("...");
```

C++L may classify it as:

```text
UNSAFE
```

or:

```text
UNVERIFIED
```

while still allowing ordinary compilation.

This distinction enables gradual adoption.

---

# 36. What developers should normally do

For application code:

```text
1. write normal clear C++
2. identify critical invariant
3. express it as a Law
4. run verification
5. inspect generated obligations
6. provide proof or improve implementation
7. keep unsafe/trusted boundaries explicit
8. expand the verified region gradually
```

Do not begin by trying to formally prove every line of a large codebase.

Start with the invariants whose failure would actually matter.

---

# 37. What not to do

Do not use C++L as:

```text
a fancy assert system
```

Do not weaken Laws to make builds green.

Do not turn failed proofs into runtime assertions automatically.

Do not hide trusted assumptions.

Do not treat solver success as proof unless the trust model explicitly permits it.

Do not duplicate ordinary C++ semantics that Clang already provides.

Do not rewrite working C++ merely to make it look more "formal."

---

# 38. Good first Laws

Good first targets include:

- bounds
- conservation equations
- non-negativity
- state-machine invariants
- uniqueness
- ownership conditions
- impossible states
- ordering
- range restrictions
- parser closure
- serialization round trips
- monotonicity
- idempotence
- deterministic transformations

Example:

```text
sorting preserves element count
```

Example:

```text
withdrawal never increases balance
```

Example:

```text
serialized then deserialized value equals original value
```

Example:

```text
settlement contains exactly one authoritative total
```

---

# 39. Think in properties

Instead of asking:

```text
What tests should I add?
```

also ask:

```text
What must always be true?
```

That question often reveals the Law.

For example:

```text
Test:
input 5 gives output 25
```

becomes:

```text
Law:
for every valid x,
square(x) >= 0
```

The second statement captures intent more directly.

---

# 40. AI-generated code

C++L is designed to work well with AI-generated implementations.

The intended workflow is:

```text
human specifies Law
        ↓
AI proposes implementation
        ↓
AI may propose proof
        ↓
C++L independently checks both
```

The AI is not trusted.

Its implementation and proof must satisfy exactly the same checker as human-written code.

This makes Laws useful as a stable boundary between human intent and generated implementation.

---

# 41. AI should not change Laws silently

When an implementation fails verification, an AI agent must not "fix" the failure by weakening the specification.

Bad:

```text
Law says:
result > 0

implementation returns 0

AI changes Law to:
result >= 0
```

unless the intended requirement itself is genuinely being changed.

The correct question is first:

```text
Is the implementation wrong?
```

---

# 42. Trust reports

For critical code, C++L should be able to explain why a result is believed.

Conceptually:

```text
Law:
  settlement_closes

Status:
  PROVEN

Proof dependencies:
  item_sum                PROVEN
  discount_application    PROVEN
  provider_contract       TRUSTED

Runtime checks:
  0

Unsafe dependencies:
  0
```

This is stronger than simply displaying:

```text
✓ verified
```

because it exposes the actual trust chain.

---

# 43. Verification is not magic

C++L cannot automatically prove every arbitrary C++ program.

Difficult code may require:

- stronger invariants
- helper lemmas
- loop invariants
- refined types
- induction
- explicit ownership reasoning
- solver assistance
- trusted external specifications

The goal is not to pretend verification is free.

The goal is to make formal intent and proof practical inside the C++ ecosystem.

---

# 44. Prefer strong models over clever proofs

When proof becomes difficult, first ask whether the data model is weak.

Instead of proving repeatedly that:

```text
state != impossible_combination
```

consider representing the state so that impossible combinations cannot be constructed.

Good type design often removes proof obligations entirely.

---

# 45. Keep the runtime ordinary

After verification and erasure, runtime code should remain ordinary native C++.

C++L is not intended to replace:

- Clang
- LLVM
- libc++
- the system linker
- native calling conventions
- existing C++ deployment

The verification layer exists primarily at compile time.

---

# 46. Developer mental model

The simplest mental model is:

```text
C++
+
formal specification
+
machine-checkable proof
=
C++L
```

Or:

```text
ordinary code says:
what to do

Law says:
what must always be true

proof says:
why the implementation satisfies it

kernel says:
whether that reasoning is valid
```

---

# 47. Where to read next

Use:

```text
README.md
```

for the project overview.

Use:

```text
GUIDE.md
```

for practical development.

Use:

```text
SPEC.md
```

for normative language semantics.

Use:

```text
FOUNDATIONS.md
```

for the mathematical basis.

Use:

```text
DESIGN.md
```

for design rationale.

Use:

```text
ARCHITECTURE.md
```

for compiler architecture.

Use:

```text
TRUST.md
```

for the Trusted Computing Base and trust boundaries.

Use:

```text
COMPATIBILITY.md
```

for C++ compatibility.

Use:

```text
STATUS.md
```

to determine what is implemented today.

---

# 48. Final rule

When writing C++L, think:

```text
What must always be true?
```

Express that as a Law.

Then let the implementation prove it.

```text
Law
    ↓
implementation
    ↓
proof
    ↓
kernel
    ↓
native C++
```

That is C++L.
