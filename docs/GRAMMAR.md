# Grammar and declaration syntax

This section defines the normative C++L surface grammar.

C++L extends C++ with contextual constructs.

Unless explicitly redefined below, all ordinary declarations, expressions, types, statements, templates, namespaces, modules, attributes, and preprocessing behavior retain their selected C++ grammar and semantics.

C++L grammar is therefore defined as:

```text
C++ grammar
+
the extensions below
```

The notation used here is:

```text
A B        sequence
A | B      alternative
[A]        optional
{A}        zero or more repetitions
(A)        grouping
"token"    literal token
```

Identifiers and ordinary C++ syntactic categories use the selected C++ grammar.

---

## 1. Lexical rule

C++L-specific words are contextual keywords.

They are recognized as C++L syntax only when the complete surrounding grammar matches a C++L production.

The contextual words are:

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

The contextual identifiers:

```text
result
old
self
```

have special meaning only in the specification contexts defined below.

The proof-statement words:

```text
refl
exact
apply
assume
rewrite
cases
induction
```

have special meaning only as statements inside a proof body (§5).

The mathematical-domain spellings `@N`, `@Z`, `@Seq`, `@Set` and `@Map` are
single tokens that name proof-only domains (§20). They are never valid C++, so
they change the meaning of no ordinary program.

Ordinary C++ remains valid:

```cpp
int law = 1;
void proof();
struct ghost {};
int verified = 0;
```

---

# 2. Extended declaration grammar

At namespace or class scope, C++L extends the C++ declaration grammar with:

```ebnf
cppl-declaration
    ::= law-declaration
     | proof-declaration
     | refinement-type-declaration
     | ghost-declaration
     | trusted-declaration
     | verified-function-declaration
     | pure-function-declaration
```

Where ordinary C++ allows a declaration, a conforming implementation MAY additionally allow the relevant C++L declaration if its scope rules permit it.

---

# 3. Law declaration

A Law declares a proposition.

```ebnf
law-declaration
    ::= ["trusted"] "law" identifier
        "(" [parameter-declaration-list] ")"
        {law-expects-clause}
        law-ensures-clause
        ";"

law-expects-clause
    ::= "expects" "(" specification-expression ")"

law-ensures-clause
    ::= "ensures" "(" specification-expression ")"
```

A Law MUST contain exactly one `ensures` clause.

A Law MAY contain zero or more `expects` clauses.

Multiple `expects` clauses are conjoined.

Example:

```cpp
law clamp_bounds(int value, int low, int high)
    expects(low <= high)
    ensures(clamp(value, low, high) >= low &&
            clamp(value, low, high) <= high);
```

Equivalent meaning:

```text
∀ value low high,
    low <= high
    →
    low <= clamp(value, low, high)
    ∧
    clamp(value, low, high) <= high
```

A trusted Law is written:

```cpp
trusted law operating_system_contract(int fd)
    expects(fd >= 0)
    ensures(os_handle_valid(fd));
```

A trusted Law is an explicit assumption.

It is not `PROVEN`.

---

# 4. Proof declaration

A proof declaration constructs evidence for a proposition.

```ebnf
proof-declaration
    ::= "proof" identifier
        "(" [parameter-declaration-list] ")"
        "proves" "(" specification-expression ")"
        proof-body

proof-body
    ::= "{" {proof-statement} "}"
```

Example:

```cpp
proof integer_reflexivity(int x)
    proves(Eq<int>(x, x))
{
    refl;
}
```

Proof parameters are universally quantified.

Conceptually:

```text
proof P(T x) proves(Q(x))
```

means:

```text
∀ x : T, Proof<Q(x)>
```

---

# 5. Proof statements

The initial core proof-statement grammar is:

```ebnf
proof-statement
    ::= "refl" ";"
     | "exact" proof-expression ";"
     | "apply" qualified-id [proof-argument-list] ";"
     | "assume" identifier ":" specification-expression ";"
     | "rewrite" qualified-id [proof-argument-list] ";"
     | proof-let-statement
     | cases-statement
     | induction-statement
```

The language MAY grow additional derived proof syntax without changing the primitive proof semantics.

---

## 5.1 `refl`

```ebnf
refl-statement
    ::= "refl" ";"
```

`refl` proves an equality only when both sides are definitionally equal.

Valid:

```cpp
proof same(int x)
    proves(Eq<int>(x, x))
{
    refl;
}
```

Invalid:

```cpp
proof false_equality(int x)
    proves(Eq<int>(x, x + 1))
{
    refl;
}
```

---

## 5.2 `exact`

```ebnf
exact-statement
    ::= "exact" proof-expression ";"
```

`exact e;` completes the current proof goal if `e` has the required proof type.

Example:

```cpp
proof forward(Proof<P> p)
    proves(P)
{
    exact p;
}
```

---

## 5.3 `apply`

```ebnf
apply-statement
    ::= "apply" qualified-id
        ["(" [argument-expression-list] ")"]
        ";"
```

`apply` applies an existing theorem, Law, or proof-producing declaration whose conclusion can satisfy the current goal.

Any generated premises become proof obligations.

---

## 5.4 `assume`

```ebnf
assume-statement
    ::= "assume" identifier
        ":"
        specification-expression
        ";"
```

`assume` introduces a proposition already justified by the surrounding proof context.

It MUST NOT create an arbitrary trusted proposition.

For example, inside a proof of:

```text
P → Q
```

the premise `P` may become available as an assumption.

Inside a `cases` or `induction` arm (§5.8), the premises the arm received can be
named the same way. They are the case fact, and for induction also any range
condition and one induction hypothesis per recursive component. The stated
proposition must match a supplied premise exactly. Otherwise the proof is
rejected.

`assume` is not equivalent to `trusted`.

---

## 5.5 `rewrite`

```ebnf
rewrite-statement
    ::= "rewrite" qualified-id
        ["(" [argument-expression-list] ")"]
        ";"
```

`rewrite e;` uses established evidence for an equality to transform the current
proof goal.

Given evidence:

```text
e : a = b
```

and a goal containing `a`, `rewrite e;` replaces occurrences of `a` by `b` and
leaves the transformed proposition as the new proof obligation.

The equality is not trusted merely because it is named. The kernel checks the
evidence for `a = b` before performing the substitution.

For example:

```cpp
law identity_at_zero(unsigned x)
    expects(x == 0u)
    ensures(identity(x) == 0u);

proof identity_at_zero_holds(unsigned x)
    proves(identity_at_zero(x))
{
    assume h : x == 0u;
    rewrite h;
    refl;
}
```

Here `assume` is valid because the current goal is an implication whose premise
is `x == 0u`: the Law's `expects` clause is what makes it one. A proof of a Law
with no `expects` clause has no premise to assume, and `assume` there is an
error.

Conceptually:

```text
goal: (x = 0) -> identity(x) = 0

assume h : x = 0

context:
    h : x = 0

goal:
    identity(x) = 0

rewrite h

goal:
    identity(0) = 0
```

The hypothesis `h` exists only within the implication-introduction scope that
introduced it. It is not granted globally and cannot escape that scope.

The evidence used by `rewrite` may also come from an already established proof
declaration, optionally instantiated:

```cpp
rewrite identity_returns_input_holds(x);
```

`rewrite` transforms the goal; it does not assert an equality.

The equality must itself already have valid evidence, and the transformed goal
remains a proof obligation like any other.

Semantically, rewriting requires equality substitution:

```text
Γ ⊢ e : a = b
Γ ⊢ P(a)
────────────────
Γ ⊢ P(b)
```

or, when used as a goal transformation, the corresponding backwards proof step:

```text
Γ ⊢ e : a = b
goal P(a)
────────────────
new goal P(b)
```

The kernel performs the substitution itself. The elaborator may identify the
requested equality and the occurrences to rewrite, but it may not manufacture
the resulting proposition and ask the kernel to trust it.

A `rewrite` whose equality does not match the current goal is an error, not a
no-operation.

The initial form rewrites from the left-hand side of the equality to the
right-hand side:

```text
a = b

a  ↦  b
```

Reverse rewriting, if introduced, must be explicit rather than inferred
heuristically.

---

## 5.6 `cases`

```ebnf
cases-statement
    ::= "cases" identifier
        "{" proof-arm {proof-arm} "}"
```

`cases x { ... }` splits the current goal into one obligation for every case of
`x`. Each arm proves the goal for one case, with that case as a premise.

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

The cases come from the C++ type of `x`. They include residual cases such as
`unnamed`, for enumeration values that match no enumerator (§18). Every case
needs an arm unless the proof context proves it impossible. There is no wildcard
arm (SPEC.md 20). `cases` is not runtime control flow and generates no runtime
code.

Arm syntax is the same for every representation. A label is either a name the
representation reserves for a state with no C++ expression, such as `unnamed`,
or a qualified C++ expression that Clang resolves, such as `State::idle`. How
many binders an arm takes is decided by the case, not by the syntax. Which cases
exist comes from the subject's decomposition provider, so nothing here is parsed
differently per type family.

Today one provider exists: scoped enumerations, with qualified named arms and an
explicit `unnamed(value)` arm. Every arm must be written; omission of impossible
cases remains refused, and a representation with no provider is refused by name.
The precise implementation boundary is SPEC.md 20.5.

---

## 5.7 `induction`

```ebnf
induction-statement
    ::= "induction" identifier ";"
     | "induction" identifier
        "{" proof-arm {proof-arm} "}"
```

`induction x { ... }` proves the current goal for every value of `x` by the
induction principle of its domain. Each arm proves one case of that principle.

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

The binder `pred` names the predecessor. The premises `pred < UINT_MAX` and the
induction hypothesis come from the principle, and `assume` names them (§5.4).

`induction x;` leaves every case to proof automation, which must still produce
kernel-checked evidence:

```cpp
proof add_zero(unsigned x)
    proves(add(x, 0u) == x)
{
    induction x;
}
```

A domain without a defined, well-founded principle is rejected (SPEC.md 21).
`induction` is not runtime control flow and generates no runtime code.

---

## 5.8 Proof arms

`cases` and `induction` share one arm grammar:

```ebnf
proof-arm
    ::= proof-arm-label
        ["(" proof-binder-list ")"]
        "=>" proof-body

proof-arm-label
    ::= id-expression

proof-binder-list
    ::= identifier {"," identifier}
```

`id-expression` is the C++ category covering both `State::idle` and `null`. The
enclosing construct determines which labels are valid and how many binders each
takes (§18). Each case may have at most one arm. An arm body is an ordinary proof
body, so arms nest.

Binders name the structural components of a case only, such as a variant
alternative's value or a predecessor. They never bind proof evidence. The
premises an arm receives are named with `assume` (§5.4).

There is no wildcard arm, and `_` is not a label. The C++ keyword `case` is not
part of this grammar.

---

# 6. Function specification clauses

C++L extends function declarations and definitions with specification clauses.

The canonical order is:

```text
[verified] [pure] ordinary-function-declaration
    expects(...)
    ensures(...)
    decreases(...)
```

The exact grammar is:

```ebnf
cppl-function-declaration
    ::= function-prefix
        function-declarator
        {function-specification-clause}
        function-body-or-semicolon

function-prefix
    ::= ["verified"] ["pure"]
```

The underlying `function-declarator` and `function-body-or-semicolon` are ordinary C++ grammar productions.

The specification clauses occur after the ordinary declarator and before the function body or terminating semicolon.

In the implemented verification fragment a definition requires an `ensures`
clause or a Clang-resolved refined return type (SPEC.md 17.3.1). The latter supplies
its own postcondition; this decision belongs to semantic elaboration, not grammar.

```ebnf
function-specification-clause
    ::= expects-clause
     | ensures-clause
     | decreases-clause

expects-clause
    ::= "expects" "(" specification-expression ")"

ensures-clause
    ::= "ensures" "(" specification-expression ")"

decreases-clause
    ::= "decreases" "(" specification-expression-list ")"
```

Example:

```cpp
verified int divide(int x, int y)
    expects(y != 0)
    ensures(result == x / y)
{
    return x / y;
}
```

Example:

```cpp
pure int square(int x)
    ensures(result == x * x)
{
    return x * x;
}
```

Example:

```cpp
verified pure int identity(int x)
    ensures(result == x)
{
    return x;
}
```

---

# 7. Ordering of function modifiers

The canonical C++L modifier order is:

```text
verified pure
```

Therefore:

```cpp
verified pure int f(int x);
```

is canonical.

The grammar MUST NOT require global reservation of either word.

A conforming implementation MAY reject alternative C++L modifier orderings such as:

```cpp
pure verified int f(int x);
```

to keep the grammar deterministic.

---

# 8. `verified`

`verified` is a contextual declaration specifier.

```ebnf
verified-specifier
    ::= "verified"
```

It may appear only in C++L function/declaration positions explicitly permitted by this specification.

Example:

```cpp
verified int abs_value(int x)
    ensures(result >= 0)
{
    ...
}
```

It requests that all mandatory verification obligations of the declaration be discharged.

---

# 9. `pure`

`pure` is a contextual declaration specifier.

```ebnf
pure-specifier
    ::= "pure"
```

Example:

```cpp
pure int max2(int a, int b) {
    return a > b ? a : b;
}
```

A declaration marked `pure` is subject to the purity rules defined elsewhere in this specification.

---

# 10. Preconditions

Preconditions use:

```cpp
expects(expression)
```

not:

```cpp
requires(expression)
```

because `requires` already belongs to C++.

Example:

```cpp
verified int read(
    const std::vector<int>& values,
    std::size_t index
)
    expects(index < values.size())
{
    return values[index];
}
```

Multiple clauses are legal:

```cpp
verified int withdraw(Account& account, int amount)
    expects(amount >= 0)
    expects(amount <= account.balance)
    ensures(account.balance == old(account.balance) - amount)
{
    account.balance -= amount;
    return account.balance;
}
```

---

# 11. Postconditions

Postconditions use:

```cpp
ensures(expression)
```

Example:

```cpp
verified int identity(int x)
    ensures(result == x)
{
    return x;
}
```

Multiple `ensures` clauses MAY appear on a function declaration.

```ebnf
function-specification-clause
    ::= ...
```

therefore permits repeated `ensures`.

They are conjoined.

Example:

```cpp
verified int clamp(int x, int low, int high)
    expects(low <= high)
    ensures(result >= low)
    ensures(result <= high)
{
    ...
}
```

---

# 12. `result`

Within an `ensures` clause of a non-void function:

```text
result
```

denotes the returned value.

Example:

```cpp
verified int zero()
    ensures(result == 0)
{
    return 0;
}
```

Outside a valid postcondition context, `result` is an ordinary identifier.

A `void` function MUST NOT use `result` as the return-value metavariable.

---

# 13. `old`

The contextual specification form:

```cpp
old(expression)
```

refers to the value represented by `expression` in the function pre-state.

Grammar:

```ebnf
old-expression
    ::= "old" "(" specification-expression ")"
```

Example:

```cpp
verified void increment(int& x)
    ensures(x == old(x) + 1)
{
    ++x;
}
```

`old` is meaningful only where a pre-state exists.

It is not globally reserved.

---

# 14. Refinement type declaration

The canonical refinement declaration syntax is:

```ebnf
refinement-type-declaration
    ::= "type" identifier
        ["(" [formal-index-parameter-list] ")"]
        "=" type-id
        "where" "(" specification-expression ")"
        ";"
```

Example:

```cpp
type Percentage =
    int where(self >= 0 && self <= 100);
```

Example with an index:

```cpp
type Index(std::size_t n) =
    std::size_t where(self < n);
```

---

# 15. `self`

Within the `where` predicate of a refinement declaration:

```text
self
```

denotes the candidate value of the base type.

Example:

```cpp
type Positive =
    int where(self > 0);
```

Outside a refinement predicate, `self` has no special C++L meaning.

---

# 16. Dependent refinement parameters

Formal index parameters use ordinary parameter declaration syntax unless explicitly restricted.

```ebnf
formal-index-parameter-list
    ::= parameter-declaration
        {"," parameter-declaration}
```

An index written as a bare name, with no type of its own, takes the type being
refined:

```cpp
type Index(n) = std::size_t where (self < n);
```

is `type Index(std::size_t n) = std::size_t where (self < n);`.

Example:

```cpp
type BoundedInt(int low, int high) =
    int where(self >= low && self <= high);
```

Instantiation:

```cpp
BoundedInt<0, 100>
```

or another syntax MUST be defined consistently by the type grammar before implementation.

For the canonical C++L syntax, dependent type application uses angle-bracket syntax:

```cpp
BoundedInt<0, 100>
```

Therefore:

```cpp
type BoundedInt(int low, int high) =
    int where(self >= low && self <= high);

BoundedInt<0, 100> percentage;
```

This intentionally aligns with C++ template-like type syntax.

---

# 17. Data types are C++ types

C++L has no declaration syntax for algebraic or inductive data types.

The data a C++L program reasons about is declared with ordinary C++:

```cpp
enum class Result { ok, error };

struct Node {
    int value;
    Node* next;
};

using PaymentResult = std::variant<Receipt, Error>;
```

Laws, proofs, `cases`, and `induction` refer to these types directly. See
SPEC.md 19.

---

# 18. Case labels

A `proof-arm-label` (§5.8) names one case of the construct that encloses it:

```text
cases over an enumeration
    each enumerator, qualified          State::idle
    + unnamed(value), if its values exceed its enumerators

cases over a std::variant
    each alternative type               Receipt(receipt)
    + valueless

cases over a std::optional
    engaged(value)
    + empty

cases over a pointer
    null
    + nonnull(p)

induction over an unsigned integer type or @N
    zero
    successor(pred)

induction over another domain
    the case names of that domain's principle
```

Labels after a `+` are residual cases. They cover C++ states that have no
ordinary named alternative. They have meaning only as labels of the matching
construct and are not reserved identifiers.

A label that does not name a case of the enclosing construct is an error. So is
a label with the wrong number of binders.

Every case needs an arm unless the proof context proves it impossible
(SPEC.md 20.2). No label covers several cases.

---

# 19. No runtime pattern matching

C++L defines no `match` expression or statement.

Executable code branches with ordinary C++:

```cpp
switch (r) {
case Result::ok:
    ...
    break;
case Result::error:
    ...
    break;
}
```

`cases` and `induction` are proof statements (§5.6, §5.7). They cannot appear in
executable code and have no runtime representation.

---

# 20. Mathematical domains

```ebnf
mathematical-domain
    ::= "@N"
     | "@Z"
     | "@Seq" "<" verification-type ">"
     | "@Set" "<" verification-type ">"
     | "@Map" "<" verification-type "," verification-type ">"

verification-type
    ::= type-id
     | mathematical-domain
```

`@N`, `@Z`, `@Seq`, `@Set` and `@Map` are each one token, written without
internal whitespace. They are lexed before macro expansion, so a macro named `N`
does not expand inside `@N` (SPEC.md 3.2). The set is closed: `@Foo` is an
error.

A mathematical domain may appear only where a verification type is expected:

- as the type of a Law or proof parameter
- as the type of a quantifier binder
- as the type of a ghost declaration
- as an argument of another domain

```cpp
proof sums(@Seq<int> xs)
    proves(...)
{
    ...
}

forall(@N k) { ... }

ghost @Z total;
```

`int` is a C++ machine integer and never denotes a mathematical integer. `@Z` is
the mathematical integers. Documentation may write ℕ, ℤ, Seq⟨T⟩, Set⟨T⟩ and
Map⟨K,V⟩ as metanotation for the same domains. See SPEC.md 19.1.

---

# 21. `ghost` declaration

Ghost declarations use normal C++ declaration syntax preceded by contextual `ghost`.

```ebnf
ghost-declaration
    ::= "ghost" simple-declaration
```

Examples:

```cpp
ghost int original = value;
```

```cpp
ghost auto starting_balance = account.balance;
```

A ghost declaration may appear wherever this specification permits proof/specification state.

Its declared value has no runtime identity.

---

# 22. `unsafe` block

The canonical block syntax is:

```ebnf
unsafe-statement
    ::= "unsafe" compound-statement
```

Example:

```cpp
unsafe {
    platform_intrinsic();
}
```

The body uses ordinary C++ statement syntax.

`unsafe` changes verification semantics, not runtime C++ execution semantics.

---

# 23. Unsafe declaration

A declaration may also be explicitly marked unsafe:

```ebnf
unsafe-declaration
    ::= "unsafe" declaration
```

Where ambiguity with ordinary syntax exists, only forms explicitly permitted by C++L declaration grammar are recognized.

Example:

```cpp
unsafe int read_hardware_register();
```

This does not make the function trusted.

---

# 24. Trusted declaration

`trusted` may qualify formal declarations for which the proposition or specification is assumed rather than proven.

The initial normative trusted form is:

```ebnf
trusted-declaration
    ::= "trusted" law-declaration-without-trusted-prefix
```

Canonical example:

```cpp
trusted law malloc_contract(std::size_t n)
    ensures(...);
```

General `trusted` arbitrary C++ declarations are NOT part of the initial language grammar unless separately specified.

This keeps trust explicit at the proposition boundary.

---

# 25. Loop invariant syntax

C++L extends loop statements with specification clauses.

Canonical `while` form:

```ebnf
cppl-while-statement
    ::= "while" "(" condition ")"
        {loop-specification-clause}
        statement

loop-specification-clause
    ::= invariant-clause
     | decreases-clause

invariant-clause
    ::= "invariant" "(" specification-expression ")"

decreases-clause
    ::= "decreases" "(" specification-expression-list ")"
```

Example:

```cpp
while (i < n)
    invariant(i <= n)
    decreases(n - i)
{
    ++i;
}
```

---

# 26. `for` loop specification

Canonical form:

```ebnf
cppl-for-statement
    ::= "for" "(" for-init-statement conditionopt ";"
                    expressionopt ")"
        {loop-specification-clause}
        statement
```

Example:

```cpp
for (std::size_t i = 0; i < values.size(); ++i)
    invariant(i <= values.size())
    decreases(values.size() - i)
{
    ...
}
```

Implementation note. The clauses are recognized only inside a verified
function and only when a block follows them: `while (c) invariant(x);` is an
ordinary call, and a lone `invariant(name)` before a block that `;` follows is
left to C++, where it declares `name` if `invariant` names a type (§1,
SPEC.md 3.1). `decreases` on a loop is currently refused, because loop
termination is not yet verified (SPEC.md 24.3).

The example above shows the syntax, not the currently verified fragment. Besides
`decreases`, it initializes a `std::size_t` from the `int` literal `0`, which is
an implicit conversion, and calls the member function `values.size()`, which
needs an object model. Both are rejected today (SPEC.md 12.5, 12.8).

---

# 27. `decreases`

A decreases clause accepts one or more measures.

```ebnf
decreases-clause
    ::= "decreases" "(" specification-expression-list ")"

specification-expression-list
    ::= specification-expression
        {"," specification-expression}
```

Single measure:

```cpp
decreases(n)
```

Lexicographic measure:

```cpp
decreases(outer, inner)
```

Multiple measures are interpreted lexicographically.

---

# 28. Quantifier expressions

Quantifiers are available only in specification/proof contexts.

Universal quantifier:

```ebnf
forall-expression
    ::= "forall" "(" parameter-declaration-list ")"
        specification-block

specification-block
    ::= "{"
        specification-expression
        "}"
```

Example:

```cpp
ensures(
    forall (int i) {
        i >= 0 -> property(i)
    }
);
```

A quantifier word begins a quantifier expression only in the complete form
above. `forall` or `exists` followed by anything else is an ordinary C++
identifier, and the expression around it is resolved by Clang. The
implementation boundary of `forall` is described in SPEC.md 8.1; the
parameter-declaration-list and the propositions inside the block are resolved by
Clang, not by a fabricated declaration.

Existential quantifier:

```ebnf
exists-expression
    ::= "exists" "(" parameter-declaration-list ")"
        specification-block
```

Existential quantification is recognised and refused; see SPEC.md 9.1.

Example:

```cpp
ensures(
    exists (int i) {
        i >= 0 && values[i] == target
    }
);
```

---

# 29. Logical implication

Because ordinary C++ has no implication operator, C++L specification expressions define:

```text
->
```

as logical implication only inside specification/proof contexts.

Grammar:

```ebnf
implication-expression
    ::= logical-or-expression
        ["->" implication-expression]
```

Meaning:

```text
P -> Q
```

is:

```text
¬P ∨ Q
```

This operator MUST NOT be introduced into ordinary runtime C++ expression grammar.

Because `->` is also C++ member access, and because implication is looser than
every C++ operator (33 below), an `->` outside all brackets in a specification
expression is implication, and one inside them is C++. Member access is
therefore written parenthesized. The implementation boundary is described in
SPEC.md 8.3.

---

# 30. Logical equivalence

Specification expressions MAY use:

```text
<->
```

for logical equivalence.

```ebnf
equivalence-expression
    ::= implication-expression
        {"<->" implication-expression}
```

Meaning:

```text
P <-> Q
```

is:

```text
(P -> Q) ∧ (Q -> P)
```

It is valid only in specification/proof context.

---

# 31. Formal equality syntax

The canonical explicit propositional equality form is:

```cpp
Eq<T>(a, b)
```

Grammar:

```ebnf
formal-equality-expression
    ::= "Eq" "<" type-id ">"
        "(" specification-expression ","
            specification-expression ")"
```

Example:

```cpp
proves(Eq<int>(x, x))
```

Ordinary:

```cpp
x == y
```

remains C++ Boolean equality.

The two are not lexically interchangeable.

The current explicit-equality implementation boundary is described in
SPEC.md 7.2.1. The type-id and argument list are resolved by Clang, not by
a fabricated C++ `Eq` declaration.

---

# 32. Specification-expression grammar

A specification expression is based on ordinary C++ expressions with additional formal forms.

Conceptually:

```ebnf
specification-expression
    ::= cpp-expression
     | forall-expression
     | exists-expression
     | formal-equality-expression
     | implication-expression
     | equivalence-expression
     | old-expression
```

The parser MUST interpret operators according to C++ precedence except where the added specification operators define their own precedence.

---

# 33. Specification operator precedence

Built-in `&&` and `||` between supported C++ Boolean predicates are currently
lifted to conjunction and disjunction; their supported placements and proof
behavior are in SPEC.md 7.6 and 7.8. Formal propositions also compose through
`&&`, `||` and `<->` (SPEC.md 7.6-7.8).

From tighter to looser:

```text
ordinary C++ operators
&&
||
->
<->
```

Therefore:

```text
A && B -> C
```

means:

```text
(A && B) -> C
```

and:

```text
A -> B -> C
```

is right associative:

```text
A -> (B -> C)
```

Parentheses SHOULD be used where meaning could be unclear.

---

# 34. Proof expression

A proof expression is an expression whose formal type is:

```text
Proof<P>
```

for some proposition `P`.

```ebnf
proof-expression
    ::= identifier
     | qualified-id
     | proof-call-expression
     | formal-proof-constructor
```

The exact set of primitive proof constructors is intentionally small.

Derived tactics may elaborate into these primitive forms.

---

# 35. Function declaration examples

Ordinary function:

```cpp
int add(int a, int b);
```

Pure:

```cpp
pure int add(int a, int b);
```

Verified:

```cpp
verified int add(int a, int b)
    ensures(result == a + b);
```

Verified and pure:

```cpp
verified pure int add(int a, int b)
    ensures(result == a + b);
```

Definition:

```cpp
verified pure int add(int a, int b)
    ensures(result == a + b)
{
    return a + b;
}
```

---

# 36. Declaration placement

Unless a more specific rule applies:

- `law` declarations may appear at namespace or class scope;
- `proof` declarations may appear at namespace or class scope;
- local proof declarations MAY be supported only where explicitly defined;
- refinement `type` declarations may appear at namespace or class scope;
- `ghost` declarations may appear in specification/proof-enabled block scope;
- `unsafe` blocks appear in statement context.

The initial language SHOULD avoid allowing every formal declaration in every C++ scope until semantics are explicit.

---

# 37. Class-member Laws

A Law declared within a class may refer to members according to normal C++ scope rules.

Example:

```cpp
class Account {
public:
    int balance;

    law nonnegative()
        ensures(balance >= 0);
};
```

The implicit object is conceptually part of the Law's quantified context.

The exact constness and object-state rules MUST follow the relevant C++ declaration semantics.

---

# 38. Member-function contracts

Contracts may appear on member functions.

Example:

```cpp
class Account {
public:
    int balance;

    verified void withdraw(int amount)
        expects(amount >= 0)
        expects(amount <= balance)
        ensures(balance == old(balance) - amount)
    {
        balance -= amount;
    }
};
```

`old(balance)` refers to the pre-state of the current object.

---

# 39. Templates

C++L constructs may participate in ordinary C++ templates.

Example:

```cpp
template <typename T>
verified pure T identity(T value)
    ensures(result == value)
{
    return value;
}
```

Ordinary C++ template syntax remains authoritative.

Verification occurs under the semantic conditions defined for the template or its instantiated specialization.

---

# 40. Attributes

Ordinary C++ attributes retain ordinary placement and semantics.

C++L declaration modifiers appear outside the ordinary C++ attribute grammar.

Example:

```cpp
[[nodiscard]]
verified int parse(...)
    ensures(...);
```

An implementation MUST preserve normal C++ attribute semantics.

---

# 41. Function specifier interaction

C++L specifiers coexist with ordinary C++ declaration specifiers.

Example:

```cpp
verified pure constexpr int identity(int x)
    ensures(result == x)
{
    return x;
}
```

The C++ declaration portion:

```cpp
constexpr int identity(int x)
```

retains normal C++ meaning.

C++L modifiers add formal obligations.

---

# 42. `noexcept`

`noexcept` remains ordinary C++ syntax.

Example:

```cpp
verified int f(int x) noexcept
    ensures(result >= 0)
{
    ...
}
```

`noexcept` semantics are defined by C++.

C++L verification may use those semantics but does not redefine them.

---

# 43. Trailing return types

C++ trailing-return syntax remains valid.

Example:

```cpp
verified auto identity(int x) -> int
    ensures(result == x)
{
    return x;
}
```

Specification clauses occur after the complete function declarator.

---

# 44. `override`, `final`, and member qualifiers

Ordinary C++ member-function declarator elements retain their normal order.

Example:

```cpp
verified int value() const noexcept override
    ensures(result >= 0);
```

C++L specification clauses follow the complete ordinary C++ declarator.

---

# 45. Function grammar integration rule

The normative integration rule is:

```text
C++ function declarator
then
C++L specification clauses
then
C++ function body or ;
```

Therefore:

```cpp
verified int value() const noexcept
    expects(...)
    ensures(...)
{
    ...
}
```

not:

```cpp
verified int value()
    ensures(...)
    const noexcept;
```

C++L must not split the ordinary C++ declarator.

---

# 46. Law naming

A Law has an ordinary identifier.

Example:

```cpp
law balance_never_negative(...)
```

Qualified Law names use normal C++ qualification syntax:

```cpp
payments::balance_never_negative
```

Law names occupy a formal declaration namespace associated with C++ scope.

An implementation MUST diagnose ambiguous references.

---

# 47. Proof naming

Proof declarations similarly use ordinary identifiers.

Example:

```cpp
proof balance_preservation(...)
```

Proof names may be qualified through ordinary C++ namespace syntax.

---

# 48. Semicolon rules

Declarations that do not contain bodies end with `;`.

Examples:

```cpp
law L(...)
    ensures(...);

trusted law T(...)
    ensures(...);

verified int f(...)
    ensures(...);

type Percentage =
    int where(...);
```

Proof declarations contain a body and do not require an additional semicolon after the closing brace.

`cases` and `induction` statements with arms end at their closing brace. The
short form `induction x;` ends with `;`.

---

# 49. Canonical formatting

Formatting is non-normative, but documentation SHOULD use:

```cpp
verified pure int function(int x)
    expects(...)
    ensures(...)
{
    ...
}
```

and:

```cpp
law name(int x)
    expects(...)
    ensures(...);
```

This makes the distinction between C++ declarator syntax and C++L specification clauses visually obvious.

---

# 50. Invalid combinations

The following are invalid unless a later specification explicitly defines them.

A Law with a runtime body:

```cpp
law L() {
    runtime_call();
}
```

A proof with ordinary runtime side effects:

```cpp
proof P()
    proves(Q)
{
    std::cout << "proof";
}
```

A refinement without a base type:

```cpp
type Positive where(self > 0);
```

A postcondition before the ordinary declarator is complete:

```cpp
verified int f()
    ensures(result > 0)
    const;
```

A `result` reference in a void function:

```cpp
void f()
    ensures(result == 0);
```

A global reinterpretation of contextual words:

```cpp
int law = 1; // MUST remain ordinary C++
```

A mathematical domain in a runtime position, or an unknown domain:

```cpp
@Z runtime_value;
sizeof(@Z);
new @Seq<int>();
@Foo x;
```

A wildcard arm:

```cpp
cases s {
    State::idle => { ... }
    _ => { ... }
}
```

---

# 51. Minimal complete example

```cpp
#include <cstdint>

pure int identity(int x) {
    return x;
}

law identity_returns_input(int x)
    ensures(identity(x) == x);

proof integer_reflexivity(int x)
    proves(Eq<int>(x, x))
{
    refl;
}

verified pure int checked_identity(int x)
    ensures(result == x)
{
    return x;
}

type Percentage =
    int where(self >= 0 && self <= 100);
```

This example demonstrates the canonical syntax for:

```text
pure
law
proof
proves
refl
verified
ensures
type
where
self
```

---

# 52. Grammar summary

The core C++L surface grammar is therefore:

```ebnf
cppl-declaration
    ::= law-declaration
     | proof-declaration
     | refinement-type-declaration
     | ghost-declaration
     | cppl-function-declaration

law-declaration
    ::= ["trusted"] "law" identifier
        "(" [parameter-declaration-list] ")"
        {expects-clause}
        ensures-clause
        ";"

proof-declaration
    ::= "proof" identifier
        "(" [parameter-declaration-list] ")"
        "proves" "(" specification-expression ")"
        proof-body

cppl-function-declaration
    ::= ["verified"] ["pure"]
        cpp-function-declarator
        {expects-clause | ensures-clause | decreases-clause}
        cpp-function-body-or-semicolon

refinement-type-declaration
    ::= "type" identifier
        ["(" [formal-index-parameter-list] ")"]
        "=" type-id
        "where" "(" specification-expression ")"
        ";"

cases-statement
    ::= "cases" identifier
        "{" proof-arm {proof-arm} "}"

induction-statement
    ::= "induction" identifier ";"
     | "induction" identifier
        "{" proof-arm {proof-arm} "}"

proof-arm
    ::= proof-arm-label
        ["(" proof-binder-list ")"]
        "=>" proof-body

proof-arm-label
    ::= id-expression

proof-binder-list
    ::= identifier {"," identifier}

mathematical-domain
    ::= "@N"
     | "@Z"
     | "@Seq" "<" verification-type ">"
     | "@Set" "<" verification-type ">"
     | "@Map" "<" verification-type "," verification-type ">"

verification-type
    ::= type-id
     | mathematical-domain

ghost-declaration
    ::= "ghost" simple-declaration

unsafe-statement
    ::= "unsafe" compound-statement

expects-clause
    ::= "expects" "(" specification-expression ")"

ensures-clause
    ::= "ensures" "(" specification-expression ")"

decreases-clause
    ::= "decreases" "(" specification-expression-list ")"

invariant-clause
    ::= "invariant" "(" specification-expression ")"

forall-expression
    ::= "forall" "(" parameter-declaration-list ")"
        "{"
        specification-expression
        "}"

exists-expression
    ::= "exists" "(" parameter-declaration-list ")"
        "{"
        specification-expression
        "}"

old-expression
    ::= "old" "(" specification-expression ")"

formal-equality-expression
    ::= "Eq" "<" type-id ">"
        "(" specification-expression ","
            specification-expression ")"
```

All unspecified syntax remains ordinary C++ grammar.

---

# 53. Grammar design invariant

C++L grammar MUST remain a strict extension of supported C++ grammar.

When adding future syntax:

```text
do not globally reserve ordinary identifiers

do not redefine existing C++ keywords

do not split or reinterpret ordinary C++ declarators

do not create ambiguity that changes the meaning of valid supported C++

prefer contextual constructs with clear grammatical entry points
```

The grammar exists to add formal semantics to C++, not to create a parallel syntax for functionality C++ already expresses.
