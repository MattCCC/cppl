# Grammar and declaration syntax

This is the normative concrete grammar referenced by [SPEC.md](../SPEC.md).
[RFC 0015](rfcs/0015-canonical-language-surface.md) reconciles earlier spellings.
Ordinary C++ categories below retain the selected C++ grammar and Clang semantics.
EBNF uses quoted tokens, `[]` for optional parts, `{}` for repetition and `|`
for alternatives. Whitespace separates tokens; canonical layout is section 49.

## 1. Lexical rule

`law`, `proof`, `proves`, `pure`, `verified`, `ghost`, `unsafe`, `trusted`, `type`,
`where`, `expects`, `ensures`, `decreases`, `invariant`, `forall`, and `exists`
are contextual words. `refl`, `exact`, `apply`, `assume`, `rewrite`, `cases`,
`decompose`, and `induction` are contextual proof statements. `result`, `old`,
and `self` have only the scopes defined below. C++ keywords take precedence;
`case` remains a runtime switch label. None of these additions globally reserves
an ordinary C++ identifier.

## 2. Extended declarations

```ebnf
cppl-declaration ::= law-declaration | proof-declaration
                  | refinement-type-declaration | ghost-declaration
                  | cppl-function-declaration | unsafe-declaration
```

## 3. Law declaration

```ebnf
law-declaration ::= "law" identifier "(" [parameter-declaration-list] ")"
                    [expects-clause] proves-clause (";" | proof-body)
                  | "trusted" "law" identifier "(" [parameter-declaration-list] ")"
                    [expects-clause] proves-clause ";"
proves-clause ::= "proves" "(" specification-expression ")"
```

A Law has one theorem conclusion, no runtime result, and at most one premise.
`ensures` is invalid on a Law. A semicolon requests automatic proof; failure
is a compilation error. A body supplies explicit proof steps. Neither introduces
an axiom. Only the explicit `trusted law` form introduces an assumption.

```cpp
law zero_identity(unsigned x)
    proves (x + 0u == x);

law given_zero(unsigned x)
    expects (x == 0u)
    proves (x == 0u)
{
    assume h : x == 0u;
    exact h;
}
```

## 4. Proof declaration

```ebnf
proof-declaration ::= "proof" identifier "(" [parameter-declaration-list] ")"
                      proves-clause proof-body
proof-body ::= "{" {proof-statement} "}"
```

A `proof` names reusable evidence. Its body must close its goal. It has no runtime
representation. Parameters are universally quantified.

## 5. Proof statements

```ebnf
proof-statement ::= "refl" ";"
                  | "exact" evidence-reference ";"
                  | "apply" evidence-reference ";"
                  | "rewrite" evidence-reference ";"
                  | "assume" identifier ":" specification-expression ";"
                  | cases-statement | decompose-statement | induction-statement
evidence-reference ::= qualified-id ["(" [argument-expression-list] ")"]
```

### 5.1 `refl`

`refl;` closes a definitionally reflexive equality. It cannot prove `1 == 2`.

### 5.2 `exact`

`exact evidence;` closes the goal with existing evidence of the required type.
Instantiation is written `exact evidence(x);`, never `exact(evidence);`.

### 5.3 `apply`

`apply evidence;` applies established evidence and leaves its premises as goals.

### 5.4 `assume`

`assume h : P;` names a premise supplied by the proof context. The stated
proposition must match that premise. It does not assert `P` or introduce trust.
The same form names Law premises, implication premises, case facts and induction
hypotheses. No arbitrary-proposition `assume (P);` form exists.

### 5.5 `rewrite`

`rewrite equality;` uses checked equality evidence, left to right, to transform
the goal. The transformed goal must still be proven; a nonmatching rewrite fails.

### 5.6 `cases` and product decomposition

```ebnf
cases-statement ::= "cases" specification-expression "{" {proof-arm} "}"
decompose-statement ::= "decompose" specification-expression "{" proof-arm "}"
```

Providers define states, discriminators and labels; the engine owns arms and
exhaustiveness. `decompose` uses `components(bindings)` for products. Bindings
are logical projections or aliases, never new runtime objects. Cases may be
omitted only with checked evidence that their discriminator contradicts the
context. The engine derives a residual discriminator by negating named states.
There is no wildcard and `_` is not a proof catch-all. See SPEC section 20.

### 5.7 `induction`

```ebnf
induction-statement ::= "induction" identifier ";"
                     | "induction" identifier "{" proof-arm {proof-arm} "}"
```

The short form requests automation for every case. Explicit arms use the
settled domain labels, including `zero` and `successor(pred)` for unsigned
machine induction. The principle supplies its range premise and induction
hypothesis; `assume` names them. A domain needs a well-founded principle.

### 5.8 Proof arms

```ebnf
proof-arm ::= proof-arm-label ["(" proof-binder-list ")"] "=>" proof-body
proof-arm-label ::= id-expression | "alternative" "<" integer-literal ">"
proof-binder-list ::= identifier {"," identifier}
```

This grammar is shared by every representation and by induction. The provider
controls binder arity and label meaning. Arm bodies may nest.

## 6. Function specification clauses

```ebnf
cppl-function-declaration ::= cpp-prefix-specifiers ["verified"] ["pure"]
                              cpp-function-declarator
                              [expects-clause] [ensures-clause] [decreases-clause]
                              cpp-function-body-or-semicolon
expects-clause ::= "expects" "(" specification-expression ")"
ensures-clause ::= "ensures" "(" specification-expression ")"
```

Each kind occurs at most once and in the displayed order. An expects-only
verified function still incurs body safety obligations. No postcondition means
no additional postcondition assertion; it does not waive those obligations.

## 7. Modifier ordering

Preserve ordinary C++ specifier order. Place C++L function modifiers after
ordinary prefix specifiers and before the return type, in `verified pure` order.
Examples: `static verified int`, `inline verified pure int`,
`constexpr verified pure int`, `consteval verified pure int`,
`virtual verified int`. C++ restrictions on combinations still apply.
`ghost` prefixes a local declaration; `trusted` prefixes `law`; `unsafe`
prefixes a function declaration. These are not interchangeable function flags.

## 8. `verified`

`verified` requests discharge of the function's contract and required body
obligations. It does not change runtime representation, linkage or ABI.

## 9. `pure`

`pure` requests checked referential transparency. Legal forms are pure free and
member functions and `verified pure` functions. It permits neither observable
mutation nor unmodeled effects. Purity alone does not prove termination.

## 10. Preconditions

`expects (P)` is a caller precondition on functions and a theorem premise on Laws.
Use one predicate, for example `expects (amount >= 0 && amount <= balance)`.

## 11. Postconditions

`ensures (Q)` applies only to runtime functions and describes normal return.
Use one predicate; combine independent conditions with `&&`.

## 12. `result`

The return-value identifier exists only in non-void function postconditions.
It has no special meaning in Laws, preconditions, refinements or runtime bodies.
An ordinary C++ declaration named `result` remains valid outside that context.
A void postcondition cannot refer to a return value.

## 13. `old`

```ebnf
old-expression ::= "old" "(" specification-expression ")"
```

`old(expression)` denotes the expression's function-entry value in a function
postcondition. It does not copy a runtime object. The entry expression must be
well-defined and specification-visible. `result` and nested `old` are not
entry values. Elsewhere `old` is an ordinary C++ name, not a snapshot form.

## 14. Refinement declaration

```ebnf
refinement-type-declaration ::= "type" identifier
                                ["(" formal-index-parameter-list ")"]
                                "=" type-id "where" "(" specification-expression ")" ";"
```

```cpp
type NonNegative = int where (self >= 0);
type Percentage = NonNegative where (self <= 100);
```

The predicate remains attached to the declaration. Refinement identity is
verification-level; the runtime representation is its base C++ type.

## 15. `self`

`self` denotes the candidate value only in a refinement predicate. Member
contracts use ordinary member lookup and `this`; they introduce no second
meaning for `self`. Outside refinements it is an ordinary C++ identifier.

## 16. Indexed refinements

```ebnf
formal-index-parameter-list ::= parameter-declaration {"," parameter-declaration}
refinement-application ::= qualified-id "<" argument-expression-list ">"
```

```cpp
type Index(std::size_t n) = std::size_t where (self < n);
```

Index declarations are typed; applications use `Index<4>`. Indices are in scope
in the predicate. There is no alternate untyped index or call-style application.
Dependent names and template substitution use Clang's ordinary C++ rules.

## 17. Data types

Use ordinary C++ enums, classes, structs and standard-library types. C++L
introduces no second algebraic data declaration or template system.

## 18. Decomposition labels

| Representation  | Labels                                                      |
| --------------- | ----------------------------------------------------------- |
| Scoped enum     | Qualified enumerators and `unnamed(value)`                  |
| `std::variant`  | `alternative<I>(payload)` and `valueless`                   |
| `std::optional` | `some(payload)` and `none`                                  |
| `std::expected` | `value(payload)` and `error(payload)`                       |
| Pointer         | `null` and `non_null(value)`                                |
| Product         | `components(first, second)` with the representation's arity |

Aliases use canonical Clang type identity. Non-null does not prove a memory
capability. `unnamed` and `valueless` are real residual states, not wildcards.

## 19. Representation identity

Providers model public value semantics, never library layout. A newly added
state must invalidate stale exhaustiveness evidence.

## 20. Mathematical domains

```ebnf
mathematical-domain ::= "@N" | "@Z"
                     | "@Seq" "<" verification-type ">"
                     | "@Set" "<" verification-type ">"
                     | "@Map" "<" verification-type "," verification-type ">"
verification-type ::= type-id | mathematical-domain
```

These established names denote proof-only domains. They may occur in Law/proof
parameters, quantifier binders and ghost locals, never runtime storage or ABI.
C++ `int` remains a machine integer.

## 21. Ghost locals

```ebnf
ghost-declaration ::= "ghost" simple-declaration
```

Only locals in verification-enabled blocks are legal. No ghost member, runtime
parameter or global is implied. Initializers must be specification-safe;
runtime computation, lifetime and effects cannot depend on erased state.

## 22. Unsafe blocks

```ebnf
unsafe-statement ::= "unsafe" compound-statement
```

The body executes as ordinary C++. Its unproved safety is explicit and cannot
manufacture evidence.

## 23. Unsafe functions

```ebnf
unsafe-declaration ::= cpp-prefix-specifiers "unsafe" cpp-function-declaration
```

This marks an unverified operation boundary. It is not `trusted`, cannot be
combined with `verified` or `pure` to bypass checks, and has no expression form.

## 24. Trusted declarations

Only `trusted law` is a trusted declaration. It always ends with `;`, never a
proof body. The assumption and its dependent evidence retain trust provenance.

## 25. Loop clauses

```ebnf
loop-clauses ::= [invariant-clause] [decreases-clause]
invariant-clause ::= "invariant" "(" specification-expression ")"
cppl-while-statement ::= "while" "(" condition ")" loop-clauses compound-statement
```

One invariant must hold on entry and be preserved by every continuing iteration.
`decreases` additionally requests termination evidence.

## 26. Other loop forms

```ebnf
cppl-for-statement ::= cpp-for-header loop-clauses compound-statement
cppl-range-for-statement ::= cpp-range-for-header loop-clauses compound-statement
cppl-do-statement ::= "do" loop-clauses compound-statement "while" "(" expression ")" ";"
```

Clauses follow the loop header. For `do`, the header is `do`; the trailing
`while` remains ordinary C++ syntax. Clause scopes follow the underlying loop's
C++ bindings; they cannot refer to body locals before those locals exist.

## 27. Termination measures

```ebnf
decreases-clause ::= "decreases" "(" specification-expression-list ")"
specification-expression-list ::= specification-expression {"," specification-expression}
```

One or more measures form a lexicographic well-founded order. Presence of the
clause requests proof of strict descent on recursive calls/continuing iterations.
Proof-producing computation must terminate independently of whether a clause is
written. Runtime verification without a requested termination property establishes
partial correctness. A failed requested termination proof is an error.

## 28. Quantifiers

```ebnf
forall-expression ::= "forall" "(" parameter-declaration-list ")"
                      "{" specification-expression "}"
exists-expression ::= "exists" "(" parameter-declaration-list ")"
                      "{" specification-expression "}"
```

Quantifiers are specification-only. Binder types and names are Clang-resolved.

## 29. Implication

`P -> Q` is right-associative formal implication in specification context.
Parenthesize C++ pointer member access there so it retains its C++ meaning.

## 30. Equivalence

`P <-> Q` means `(P -> Q) && (Q -> P)` in specification context.

## 31. Formal equality

```ebnf
formal-equality-expression ::= "Eq" "<" type-id ">"
                               "(" specification-expression "," specification-expression ")"
```

`Eq<T>(a, b)` is a formal proposition, not a fabricated runtime C++ template.
C++ `a == b` retains its Boolean semantics.

## 32. Specification expressions

Specification expressions extend side-effect-free modeled C++ expressions with
quantifiers, formal equality, implication, equivalence and contextual `old`.
Unsupported semantics are errors, never implicit assumptions.

## 33. Precedence

From tightest to loosest: ordinary C++ operators above `&&`, then `&&`, `||`,
`->`, `<->`. Use parentheses to make mixed formal/C++ structure explicit.

## 34. Evidence references

References resolve in the proof context to established evidence. Spelling or
successful C++ compilation alone never supplies evidence.

## 35. Declaration and definition

A function entity has one logical contract. Put public contracts on header
declarations; the matching definition inherits that contract. Repeated identical
contracts are permitted, conflicting ones are errors. The formatter never
copies contracts from declarations onto definitions.

## 36. Placement

Laws, proofs and refinements have namespace or class scope. Translation-unit
helpers belong in an unnamed namespace. Block-local Law declarations are not a
second declaration form. Ghost declarations have block scope; unsafe blocks
have statement scope. Function contracts follow ordinary declaration scopes.

## 37. Class Laws

The implicit object follows ordinary C++ member lookup and constness rules. It
is part of the quantified specification context, never a runtime theorem object.

## 38. Member contracts and constructors

Member contracts follow the complete declarator. `this` and unqualified members
retain C++ meaning. A constructor has no `result`; its postcondition describes
the initialized object. Entry snapshots cannot read uninitialized subobjects.

## 39. Templates

C++ owns template syntax, lookup and substitution. Contract/refinement/proof
metadata must remain visible at verification of each specialization.

## 40. Attributes

Ordinary attributes keep their C++ placement. C++L never moves an attribute to
another entity while canonicalizing modifiers.

## 41. Ordinary function specifiers

Storage, linkage, `inline`, `constexpr`, `consteval`, and `virtual` retain C++
meaning. Section 7 defines only the placement of the added C++L modifiers.

## 42. `noexcept`

`noexcept` remains in the ordinary declarator, before specification clauses.
Postconditions describe normal returns, not an invented exception model.

## 43. Trailing returns

A trailing return type precedes specification clauses; C++L does not split the
ordinary declarator.

## 44. Member qualifiers

`const`, reference qualifiers, `override`, and `final` keep their C++ positions,
before the specification clauses.

## 45. Integration rule

Ordinary declarator, then C++L clauses, then body or semicolon. Ordinary runtime
bodies continue to be parsed by Clang.

## 46. Law names

Law names occupy a formal namespace associated with C++ scope and produce no
runtime callable symbol. Ambiguous references are errors.

## 47. Proof names

Proof names identify evidence in that formal scope, with no runtime symbol.

## 48. Terminators

Bodyless declarations and primitive proof commands end with `;`. Proof/Law
bodies and decomposition/induction arms end with `}` and no extra semicolon.
The short induction statement ends with `;`.

## 49. Canonical formatting

Use the shared `cppl-format` engine through CLI, LSP or CI. Clauses require one
space before `(`, occur on continuation lines, and use one clause of each kind
in grammar order. A contracted body's `{` is on the next line at declaration
indentation. Other braces follow `.clang-format`. Proof arms are expanded,
with `label(bindings) => {` and one blank line between arms. `where` stays on the
refinement declaration subject to normal ColumnLimit wrapping. Predicates use
the same indentation and line-width configuration as C++ expressions.

## 50. Rejected syntax

Law `ensures`, parenthesis-free clauses, repeated clause kinds, function `proves`,
proof `case`, wildcard `_`, call-style proof commands, untyped refinement indices,
and `pure verified` are noncanonical/illegal forms. Migration tools may offer
an explicit semantics-preserving fix; the compiler accepts no legacy dialect.

## 51. Canonical examples

[DEVELOPER_GUIDE.md](./DEVELOPER_GUIDE.md) supplies practical examples. Tests and
formatter checks maintain their agreement with this grammar.

## 52. Syntax ownership

| Region                                              | Authority                               |
| --------------------------------------------------- | --------------------------------------- |
| Ordinary declarations, templates and runtime bodies | C++ and Clang                           |
| Contracts and refinement predicates                 | C++L grammar and SPEC                   |
| Proof declarations, statements and arms             | C++L grammar and SPEC                   |
| Canonical presentation                              | Shared C++L formatter plus clang-format |

## 53. Superset invariant

A contextual addition must never reinterpret valid supported C++. No spelling
in historical RFCs overrides this grammar. Verification limitations do not
introduce alternative syntax or permission to accept unchecked proof evidence.
