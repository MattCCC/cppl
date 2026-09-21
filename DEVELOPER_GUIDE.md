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

| Construct | Runtime behavior | Erasure |
| --- | --- | --- |
| Ordinary C++ body | Executes normally | Preserved |
| `verified`, `pure` | Body executes | Modifiers/metadata removed |
| `expects`, `ensures`, `proves` | None | Removed |
| `law`, `proof` | None | Entire declaration removed |
| `cases`, `decompose`, `induction` | None | Removed with proof |
| `ghost` local | None | Removed |
| Refinement predicate/indices | None | Removed |
| Refinement base value | Ordinary C++ value | Base representation preserved |
| `invariant`, `decreases` | None | Clauses removed; loop/body retained |
| `trusted law` | None | Assumption retained in verification report, erased from executable |
| `unsafe` | Body/operation executes | Marker removed, runtime operations retained |
| Explicit runtime validation | Executes | Preserved |

A contract is not a hidden runtime assertion. A failed proof is a compilation
error. Tests, solver output and AI suggestions cannot replace kernel-checked
evidence. `TRUSTED`, `UNSAFE`, `RUNTIME-CHECKED` and `PROVEN` are distinct statuses.

## 2. Verified functions

Put a contract after the complete C++ declarator. Each clause has parentheses,
one space before `(`, and its own continuation line. The order is `expects`,
`ensures`, `decreases`, with at most one of each.

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

| Identifier | Special meaning and scope | Outside that scope |
| --- | --- | --- |
| `result` | Returned value in non-void `ensures` | Ordinary C++ name |
| `old(expression)` | Entry value in function `ensures` | Ordinary C++ call/name |
| `self` | Candidate value in refinement `where` | Ordinary C++ name |

Members use C++ `this` and ordinary member lookup; `self` is not a second spelling
for the implicit object. Ordinary C++ remains valid:

<!-- cppl-example: verify -->
```cpp
int result = 0;
int self = 1;
int old(int value) {
    return value;
}
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
axiom. Write a proof body when explicit evidence is useful:

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

A Law can express a property without calling a runtime implementation. A Law
mentioning a function needs that function's declaration and a checked formal
model. Do not write a function name into a theorem as if spelling alone supplied
its semantics.

Common mistake (rejected syntax):

```cpp
law wrong(int x)
    ensures (x == x);
```

Use `proves`, not `ensures`. Merely changing the word cannot repair a Law that
also refers to an undefined return-value `result`.

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

| Statement | Meaning |
| --- | --- |
| `refl;` | Close a definitionally reflexive equality |
| `exact same(x);` | Close the goal with existing evidence |
| `apply same(x);` | Apply evidence; discharge its premises |
| `assume h : x == 0u;` | Name a matching context-supplied premise |
| `rewrite h;` | Rewrite the goal left-to-right using checked equality |

Commands are statements, not calls such as `exact(same);`. An evidence reference
may itself have arguments. `assume` never asserts an arbitrary proposition:

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

A proof of existence needs witness evidence; absence of a counterexample is
insufficient. Quantifiers produce no runtime loops. C++ pointer member access
inside a formal proposition is parenthesized so `->` is not confused with formal
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
wrapper, tag, allocation or hidden check. Nested refinements require every
predicate inherited from the base.

Every introduction needs evidence: local initialization, call argument, return,
assignment, member/element write and verified call effect. Branch facts can
establish the predicate:

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
`ensures`. A stronger refinement may be used where a weaker one is required only
when implication is proven. An arbitrary base value does not acquire a refinement
by conversion or spelling.

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
at instantiation. There is one declaration/application spelling, not a second
C++ template system.

A trusted proposition about a value does not silently validate external input.
Only an explicit trusted boundary or a retained runtime validator can supply the
corresponding entry evidence. Such trust remains in the report.

## 7. Pure functions

`pure` requests checked referential transparency. Its body remains ordinary C++:

<!-- cppl-example: verify -->
```cpp
pure unsigned same_value(unsigned value) {
    return value;
}

verified pure unsigned checked_value(unsigned value)
    ensures (result == value)
{
    return value;
}
```

The canonical combination is `verified pure`. A pure member uses explicit input
and stable object state:

```cpp
struct Number {
    unsigned value;

    pure unsigned get() const {
        return value;
    }
};
```

Purity forbids observable mutation, I/O and calls whose effects are not admitted.
For example, this is rejected when relied upon as pure:

```cpp
unsigned global_count = 0u;

pure unsigned bump() {
    return ++global_count;
}
```

`const` is an ordinary C++ qualifier; it is not by itself proof of purity or
freedom from alias mutation. Purity also does not by itself prove termination.

## 8. Ghost locals

`ghost` prefixes a local declaration in a verification-enabled block. Its value
exists only for verification. There are no ghost runtime parameters, members or
globals in this grammar.

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
observable mutation or require runtime copies/destruction. Runtime values may be
observed symbolically; proof-only values cannot flow back into runtime behavior.

Rejected ghost leak:

```cpp
verified unsigned leaked(unsigned x)
    ensures (result == x)
{
    ghost unsigned snapshot = x;
    return snapshot;
}
```

The return is runtime behavior and would depend on erased state. The same rule
forbids ghost-dependent branches, addresses, I/O, object layout and FFI arguments.

## 9. Cases and product decomposition

`cases` is proof-only state splitting. It produces no runtime `switch`, `if` or
`std::visit`. Every representation uses the same `Label(bindings) => { }` arms.
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
`unnamed` names that real residual state. It is not a wildcard. Adding a distinct
enumerator must break a proof that omitted its named arm; a catch-all would hide
that stale proof. `_` is not a C++L proof catch-all.

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

It proves no lifetime, bounds, provenance, initialization, ownership or
writability. Product decomposition uses the separate product operation and the
same arm body/binder grammar:

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
prove an omitted state impossible under the context. For example:

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
premise and that state's discriminator. There is no heuristic omission. If the
verifier cannot establish that contradiction, the omitted arm is an error.

## 10. Induction

`cases` splits possible states. `induction` additionally supplies an induction
hypothesis for each recursive predecessor. `decreases` proves runtime termination;
it is not an induction hypothesis.

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

`pred` binds the predecessor value; `assume ih : P;` names the hypothesis supplied
by the principle. It cannot choose a stronger hypothesis. Recursive structures
need a defined well-founded principle, not just a pointer to a node. Cyclic or
dangling pointers do not supply induction.

The short form asks automation to solve every case:

```cpp
proof unsigned_identity_automatic(unsigned n)
    proves (Eq<unsigned>(n, n))
{
    induction n;
}
```

Nested proof commands remain scoped to their arms; induction evidence and case
facts cannot escape their binders. Both forms erase entirely.

## 11. Invariants and termination

An invariant holds before the first iteration and is preserved on every
continuing iteration, including `continue`. Normal loop exit combines it with
the failed condition; `break` retains only the facts on its own path.

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

This establishes partial correctness. Adding `decreases (n - i)` requests
termination as well: the measure belongs to a well-founded domain and strictly
decreases on every continuing iteration. An unsigned bound is finite; arbitrary
signed subtraction requires its definedness and lower bound to be proven.

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

A range-for uses that same location. A `do` loop places clauses after `do`, before
its body, keeping the trailing `while` in its ordinary C++ position:

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
required by its specification role. A requested termination proof may not be
silently dropped.

An invariant `i < n` fails on entry when `n == 0u`. A continuing iteration that
does not change `n - i` fails strict descent. These are proof failures, not
formatting problems; a linter must not weaken the predicates.

## 12. Trusted and unsafe boundaries

`trusted` explicitly admits a proposition without proving it. Its sole
production declaration form is `trusted law`, with a semicolon:

```cpp
trusted law supplied_zero(unsigned sample)
    proves (sample == 0u);
```

This example is a deliberately strong external assumption, not a valid theorem
about every unsigned value. The trust report names the assumption and source
location. Evidence depending on it must retain that dependency. The kernel still
checks any derived evidence relative to the explicit assumptions. Trust is not
ordinary convenience syntax and is never what `assume` means.

An external boundary should state exactly the relation the external component
promises, for example a supplied measurement range:

```cpp
trusted law calibrated_measurement(unsigned reading)
    proves (reading <= 100u);
```

Do not use that declaration to silently treat arbitrary input as a checked
measurement. Trust admission and runtime validation remain explicit boundaries.

`unsafe` permits an operation whose safety the verifier has not established. It
does not assert that the operation is correct:

```cpp
unsafe unsigned read_device();

unsigned poll_device() {
    unsigned value = 0u;
    unsafe {
        value = read_device();
    }
    return value;
}
```

These are the unsafe function-declaration and block forms; there is no unsafe
expression form. Runtime operations still execute. Unsafe code cannot produce
proof evidence or refinement facts. An unmodeled result remains unverified
unless a separately justified validation or trust boundary admits it.

## 13. References, pointers and memory validity

References and pointers retain C++ binding, aliasing and lifetime semantics.
Verification tracks places, capabilities and logical value versions through
shared read/write rules. A cast, reference binding or pointer test cannot
manufacture proof.

A non-null pointer does **not** establish `readable`, `writable`, initialized
storage, bounds, provenance or lifetime. These are capability concepts in the
storage model, not ordinary Boolean functions that this guide invents.
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

The contract lacks evidence of a valid readable initialized pointee; even that
would not establish the claimed zero. Pointer/refined-pointee operations must
use the common capability and refinement-crossing machinery, with any external
capability assumption explicitly recorded as trusted. Const references do not
protect facts from mutation through another alias.

## 14. Templates

Templates retain ordinary C++ syntax. Place definitions and required verification
metadata where instantiation can see them, normally in a header:

```cpp
template <typename T>
verified T identity(T value)
    ensures (result == value)
{
    return value;
}
```

The specialization must have a modeled equality and body semantics. A template
constraint is not an implicit theorem about arbitrary `T`. An indexed refinement
can be used with a template parameter:

```cpp
type Index(unsigned n) = unsigned where (self < n);

template <unsigned N>
verified unsigned widen_index(Index<N> value)
    ensures (result < N)
{
    return value;
}
```

Clang resolves substitution and type identity. The refinement declaration,
contract, effect metadata and any referenced Laws/evidence must remain available
at the instantiation site. An explicit-instantiation strategy must preserve the
same information.

## 15. Organizing C++L code in .h/.hpp and .cpp

**Public contract → header. Runtime implementation → source file.** Callers need
the contract, not access to the function body. Use existing C++ extensions; a
special header suffix is unnecessary.

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

unsigned withdraw(unsigned old_balance, unsigned amount) {
    return old_balance - amount;
}
```

The definition inherits the verified declaration through Clang's resolved
function entity. Do not duplicate the contract. A changed parameter name does
not create another entity. Conflicting contracts on redeclarations are errors;
identical repetition is legal when source organization requires it. The formatter
never copies a declaration contract onto a definition.

Use a direct contracted definition for private/static helpers. Shared refinements
and Laws belong beside the APIs that use them; implementation-only Laws belong
in the source, commonly in an unnamed namespace:

```cpp
namespace {
law local_identity(unsigned x)
    proves (x + 0u == x);
}
```

This is a translation-unit-local theorem, not a block-local Law declaration.
Laws and proofs produce no runtime symbols. Class-scope Laws follow ordinary
member lookup and quantify the implicit object; they do not create runtime
methods.

A realistic layout is:

```text
include/
    money.hpp       shared refinements and arithmetic Laws
    account.hpp     public contracts and boundary declarations
src/
    account.cpp     runtime definitions and private proof helpers
```

A separate `proofs/` directory is optional. Shared theorem evidence can live with
its interface; do not split formal metadata away from callers that need it.

| Construct | Normally in header/interface? |
| --- | --- |
| Public verified contract | Yes |
| Runtime function body | Usually no |
| Template definition and contract | Yes, unless explicit instantiation is arranged |
| Shared refinement | Yes |
| Shared Law and reusable evidence | Yes |
| Implementation-only Law/proof | No |
| Trusted external assumption | At the boundary's interface |
| Ghost local | No; inside its verification-enabled block |

Across translation units, preserve contracts, refinement identity/predicates,
Law propositions/evidence, purity/effect metadata and trust dependencies. Native
erasure does not encode these in ABI symbols. A visible contract lets a caller
state obligations; checked implementation evidence or an explicit trust boundary
is still needed before the summary can be used as proof. A compiler lacking
separate-evidence transport must reject that verification step.

Ordinary C++ modules retain their C++ meaning. No additional C++L module-metadata
syntax is introduced here; this guide's supported interface organization uses
headers and included verification metadata.

## 16. Member contracts

Use member lookup and `this`, not a second meaning for `self`. Put a public member
contract on its class declaration:

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

unsigned Account::withdraw(unsigned amount) {
    balance_ -= amount;
    return balance_;
}

unsigned Account::balance() const {
    return balance_;
}
```

The out-of-line definitions inherit their declarations. A constructor has no
return-value `result`; its postcondition describes the initialized object.
Snapshots cannot read members that were not initialized in the entry state:

```cpp
struct Zero {
    unsigned value;

    verified Zero()
        ensures (value == 0u)
        : value(0u) {}
};
```

Constructor initializer syntax remains ordinary C++ after the clauses; a compiler
must model initialization and lifetime before accepting that verification.

## 17. Formatting and editor fixes

Run the shared formatter:

```sh
build/dev/bin/cppl-format -i include/account.hpp src/account.cpp
build/dev/bin/cppl-format --check include/account.hpp src/account.cpp
```

The CLI, LSP document formatting and CI share one engine. Range formatting expands
to a complete affected clause/block according to the established range policy;
on-type formatting is conservative. Ordinary C++ layout comes from clang-format.
The repository style uses four spaces, a 120-column limit, attached ordinary
C++ braces and a separate opening brace after a contract block.

Canonical rules are one parenthesized clause of each kind, grammar order,
continuation lines, one space before `(`, `verified pure`, expanded proof arms,
and inline refinement `where`. `old(x)` retains function-like spacing.

What cppl-lsp fixes automatically through formatting:

Before (noncanonical layout):

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

| Input issue | Deterministic correction | Safety condition |
| --- | --- | --- |
| Missing clause parentheses | Wrap the delimited expression | Boundary is unambiguous |
| Wrong order/header-line clauses | Reorder and format complete clauses | Preserve predicate text and comments |
| Repeated `expects`/`ensures`/`invariant` | One ordered `&&` predicate | Predicates have conjunction semantics |
| Law `ensures` | Replace keyword with `proves` | No invalid Law `result` use |
| `pure verified` | `verified pure` | Both are contextual modifiers |
| Compact proof arms | Expanded `Label(bindings) => { }` | Same labels, bindings and steps |
| Obsolete proof `case` | `cases` | Inside a proof, never a C++ switch |
| Untyped refinement index | Write its declared index type | Intended type is established; otherwise ask for an edit |

A migration note is not compiler acceptance of a legacy dialect. If a correction
could alter meaning, the diagnostic asks for a source edit rather than guessing.
No fix may weaken a Law, remove an arm or turn a failed proof into trust.

## 18. Reading diagnostics

A useful diagnostic identifies the source location, goal, available premises,
failed obligation and trust provenance. Distinguish these common causes:

| Diagnostic | Action |
| --- | --- |
| Law needs `proves` | Correct its conclusion keyword |
| Clause requires parentheses/order | Apply the syntax/layout fix |
| Refinement introduction failed | Establish the predicate on this value version |
| Non-exhaustive cases | Add the missing state or prove it impossible |
| `assume` does not match a premise | Use only evidence actually supplied by the context |
| Ghost value affects runtime | Keep the runtime computation independent of erased state |
| Capability obligation failed | Supply valid memory evidence, not just non-nullness |
| Termination measure does not decrease | Correct the algorithm or its justified measure |
| Unsupported semantics | Keep verification fail-closed; no implicit assumption |

Use `cppl` with ordinary Clang compile options. For example,
`build/dev/bin/cppl -std=c++20 -fsyntax-only source.cpp` runs verification without
linking a native executable. Consult [TRUST.md](TRUST.md) for trust-report meaning
and [tools/cppl-lsp/README.md](tools/cppl-lsp/README.md) for editor setup.

## 19. C++L cheat sheet

| Goal / canonical form | Runtime? | Main context / usual location |
| --- | --- | --- |
| `verified int f(int x)` | Body runs | Public contract in header; body in source |
| `expects (x > 0)` | No | Function precondition / Law premise |
| `ensures (result == x)` | No | Runtime function postcondition |
| `law L(int x) proves (x == x);` | No | Shared header or private source theorem |
| `proof P(int x) proves (Eq<int>(x, x)) { refl; }` | No | Reusable evidence; format as expanded block |
| `type Positive = int where (self > 0);` | Base only | Shared type declaration in header |
| `type Index(unsigned n) = unsigned where (self < n);` | Base only | Indexed refinement, applied as `Index<4u>` |
| `pure unsigned f(unsigned x)` | Body runs | Checked effect-free function |
| `invariant (i <= n)` | No | Loop clause in runtime implementation |
| `decreases (n - i)` | No | Function/loop termination measure |
| `ghost unsigned original = x;` | No | Verification-only local |
| `cases value { Label => { refl; } }` | No | Proof state split; expanded arms |
| `decompose point { components(x, y) => { refl; } }` | No | Proof product projections |
| `induction n;` | No | Proof with domain induction principle |
| `trusted law boundary(unsigned x) proves (x == 0u);` | No | Explicit assumption at a boundary |
| `unsafe { operation(); }` | Operations execute | Explicit unsafe block |

The compact forms in this reference table describe tokens; the formatter expands
clauses and proof bodies to the canonical layout used throughout the guide.
