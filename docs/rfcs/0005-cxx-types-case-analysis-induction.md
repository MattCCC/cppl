# Reasoning over C++ types: proof case analysis and induction

> Surface syntax and formatting in this historical RFC are superseded by
> [RFC 0015](0015-canonical-language-surface.md) and the
> [normative grammar](../GRAMMAR.md). Semantic rationale remains applicable.

Status: accepted design decision. Its case-analysis half is implemented, over
generic decomposition providers; its induction and proof-only-domain halves are
not. Normative rules are SPEC.md 19–21 and GRAMMAR.md 5.7–5.9, 18 and 20. It
supersedes the earlier `data` declarations and `match` expressions, which were
specified but never implemented.

RFC 0013 supersedes this RFC wherever the two differ on decomposition. In
particular, the residual-label table below records this RFC's original design;
the labels RFC 0013 specifies are the implemented ones, and nothing here implies
that a representation is given its own case-analysis implementation.

## Decision

C++L does not introduce general-purpose algebraic data types or runtime pattern
matching. It reasons directly over C++ types. It may provide proof-only
mathematical domains and proof constructs such as exhaustive case analysis and
induction. All such constructs are erased and have no runtime representation.

## Why

C++L verifies real C++ programs. If a program uses `unsigned x;` or
`struct Node { Node* next; };`, the verifier reasons about those types, which it
takes from Clang. A developer should not have to restate their data structures
in a second logical language before proving anything about them.

The earlier design did require that. `data Nat { Zero; Succ(Nat predecessor); };`
was a new runtime-looking type system, and `match (x) { Zero => ... }` was a new
control-flow construct with its own runtime lowering and ABI questions. What the
verifier needs from them is narrower: splitting a proof into cases, and
induction. Those are proof steps, so C++L provides them as proof steps. The
lesson of the `Nat` example, structural induction, remains. Its datatype syntax
does not.

`match` has also been proposed as runtime pattern-matching syntax for C++
itself. Keeping it out of C++L avoids a future collision with the language C++L
must remain a superset of.

## The two proof constructs

`cases` creates one proof obligation for every case of a value:

```text
using PaymentResult = std::variant<Receipt, Error>;

proof settle(PaymentResult result)
    proves (...)
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

`induction` applies the induction principle for a value's domain:

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

Its short form, `induction x;`, leaves every case to automation, which must
still produce kernel-checked evidence.

Both constructs share one arm grammar, `label(binders) => { proof statements }`,
where the binder list is optional. The enclosing construct decides which labels
are valid. The C++ keyword `case` is not reused. `cases` and `induction` are
recognized only as proof statements inside a proof body, so no ordinary C++
changes meaning.

Neither construct generates runtime code, and neither can appear in executable
code. Ordinary code keeps using `if`, `switch`, and `std::visit`.

## Arm binders and hypotheses

Binders name structural components only: a variant alternative's value, an
enumeration's underlying value, a predecessor, a node's children. Proof evidence
is never bound by position.

The premises an arm receives are already in its proof context. They are the case
fact, and for induction also the range condition and one induction hypothesis
per recursive component. The existing `assume` statement names them:

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

`assume` never creates a premise. It binds one that the principle supplied. The
kernel rejects it unless the stated proposition matches that premise exactly.
Induction hypotheses come from the principle, not from a proof invoking itself.

Alternatives rejected:

- Positional binders for evidence, as in
  `node(value, left, right, left_ih, right_ih)`, mix program values with proof
  evidence and make principles harder to read and evolve.
- Implicit fixed names such as `ih` hide bindings and shadow user names.
- Recursive self-calls would turn ordinary proof recursion into an implicit
  induction mechanism that the checker must police.

## Residual cases, no wildcard

Case analysis models the complete semantic state space of the C++ type. That
includes states with no ordinary named alternative. Those states are explicit,
type-specific residual cases:

```text
enumeration      its enumerators
                 + unnamed(value), when its value set exceeds them
std::variant     alternative<i>(value), one per alternative index
                 + valueless
std::optional    some(value)
                 + none
std::expected    value(payload)
                 + error(reason)
pointer          null
                 + non_null
```

The implemented labels are `some`/`none` rather than `engaged`/`empty`, and
alternatives are named by index so that repeated and aliased alternative types
remain distinct states. `non_null` binds nothing: binding a pointee would assert
that a live, initialized object exists, which a non-null pointer does not
establish. RFC 0013 and SPEC.md 20.1 are normative for all of this.

A product representation - a record, `std::pair`, `std::tuple`, `std::array` or
a built-in array - has one state and so is not case analysis at all. It is
written `decompose subject { components(...) => { ... } }`, a separate statement
with its own keyword.

There is no wildcard arm. Every case has an arm or is proven impossible from the
proof context. A wildcard would silently absorb an enumerator added later. With
named residual cases, a new enumerator is a new named case, and every proof that
does not cover it stops checking.

Residual labels are defined by the verifier. They have meaning only as labels of
the corresponding construct, and they are not reserved identifiers. Enumerator
labels are always qualified, as in `State::idle`, so an enumerator named
`unnamed` cannot collide with the residual label.

`nonnull(p)` establishes only that `p` is not null. It says nothing about the
lifetime of the object `p` points to. That remains a question for the memory
model.

## Mathematical domains

Specifications may use five proof-only mathematical domains:

```text
@N           natural numbers: 0, 1, 2, ...
@Z           mathematical integers: ..., -1, 0, 1, ...
@Seq<T>      finite sequences
@Set<T>      sets
@Map<K, V>   finite maps
```

The set is closed. `@` does not open a general identifier namespace, so `@Foo`
is an error unless a later RFC adds user-defined domains. Machine and
mathematical values cannot be confused on sight. `int` is a C++ machine integer
and `@Z` is not. `std::vector<int>` is a runtime object and `@Seq<int>` is a
proof-only sequence. A vector's contract is easier to state over its abstract
sequence:

```cpp
verified void grow_capacity(std::vector<int>& v, std::size_t n)
    ensures (model(v) == old(model(v)));
```

Here `model` is illustrative. It stands for a proof-only function that returns
an `@Seq<int>`. What such a function says about a C++ type must be proven or
stated as an explicit trusted assumption.

A domain is accepted only where a verification type is expected. That covers a
Law or proof parameter, a quantifier binder, a ghost declaration, and an
argument of another domain. Domains have no object representation, storage, ABI,
lifetime, address, `sizeof`, alignment, constructor, or destructor.
`@Z runtime_value;`, `sizeof(@Z)` and `new @Seq<int>()` are rejected.

The five spellings are C++L lexical constructs. Each is a single token, so a
macro named `N` does not expand inside `@N`. `@` cannot appear in valid C++
outside literals and comments. The only valid C++ programs this could affect are
ones that stringize one of these spellings after macro expansion. Objective-C++
also uses `@`-prefixed constructs. Supporting Objective-C++ lies outside the
core grammar and may need a separate frontend mode. If a future C++ standard
gives `@` an incompatible meaning, C++L will version its grammar or revisit the
spelling at that point.

## Soundness requirements

The cases must be complete with respect to C++ semantics, not with respect to
declared names. Omitting a residual case is allowed only when the proof context
establishes that it cannot occur. Otherwise the proof is rejected.

An induction principle must be well founded and must match runtime behavior.
For an unsigned type, the principle's cases are `zero` and `successor(pred)`,
with premises `pred < max` and `P(pred)`. The step never wraps. A pointer type
has no induction principle by type alone, because a `Node*` may be cyclic,
dangling, or shared. Induction over a linked structure needs an explicit
well-founded premise, such as finite acyclic reachability under the memory
model. Without one it is rejected. Each induction principle is checked by the
kernel like any other rule. Unknown domains fail closed.

## Consequences

The contextual words `data` and `match` are removed. The proof statements
`cases` and `induction`, the five domain spellings, and the residual labels are
added. No implementation, test, or editor grammar used the removed words. Trust
and runtime behavior do not change until these constructs are implemented.

Implementations need kernel-checked evidence, with positive, negative, and
adversarial tests.

Case analysis is implemented once, for every representation, over decomposition
providers (RFC 0013). Scoped enumerations were the first vertical implementation
and are now the first provider; nothing in this RFC implies that a representation
needs its own `cases` implementation. The generic engine derives case analysis
from existing conditional elimination and needs no additional kernel rule, for
enumerations or for any later representation whose states are distinguished by
decidable conditions on modeled values.

Induction principles still need their own checked justification, and stay
separate from finite case decomposition. Tests must cover:

- out-of-range enumeration values, valueless variants, and omitted residual
  cases
- wildcard arms, which must be rejected
- `assume` statements that do not match a supplied premise
- wraparound at the unsigned maximum
- cyclic pointer structures
- domains used in runtime positions
- macros named `N`, `Z`, `Seq`, `Set`, or `Map`

## Unresolved

- the explicit conversion between machine values and domain values, such as
  `int` to `@Z`, and its spelling
- how proof-only model functions such as `model(v)` are declared
- literal syntax for domain values
- induction principles for signed integer types
- the labels and premise form of induction over pointer-linked structures
- whether `cases` accepts compound expressions
- an Objective-C++ frontend mode
- user-defined mathematical domains
