# Reasoning over C++ types: proof case analysis and induction

Status: accepted design decision, not implemented; normative rules are SPEC.md
19–21. It supersedes the earlier `data` declarations and `match` expressions,
which were specified but never implemented.

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

```cpp
proof foo(Result r)
    proves(...)
{
    cases r {
        Result::ok => {
            ...
        }

        Result::error => {
            ...
        }
    }
}
```

`induction` applies the induction principle the verifier provides for a value's
domain:

```cpp
proof property(Node* n)
    proves(...)
{
    induction n {
        null => {
            ...
        }

        node => {
            ...
        }
    }
}
```

Its short form leaves every case to automation, which must still produce
kernel-checked evidence:

```cpp
proof add_zero(unsigned x)
    proves(add(x, 0u) == x)
{
    induction x;
}
```

Both constructs share one arm grammar, `label => { proof statements }`. The
enclosing construct decides which labels are valid. The C++ keyword `case` is
not reused. Both words are recognized only as proof statements inside a proof
body, so no ordinary C++ changes meaning.

Neither construct generates runtime code, and neither can appear in executable
code. Ordinary code keeps using `if`, `switch`, and `std::visit`.

## Soundness requirements

The cases must be complete with respect to C++ semantics, not with respect to
the declared names. An enumeration with a fixed underlying type, which includes
every scoped enumeration, can hold values other than its enumerators. A
`std::variant` can be `valueless_by_exception()`. Where the labelled arms do not
cover the type's full C++ value set by construction, `cases` generates an
additional exhaustiveness obligation that the context must discharge. If it
cannot be discharged, the proof is rejected. Syntax for an arm covering the
remaining values is not yet specified.

An induction principle must be well founded and must match runtime behavior.
For an unsigned type, the successor step applies only below the type's maximum
value, so it never wraps. A pointer type has no induction principle by type
alone, because a `Node*` may be cyclic, dangling, or shared. Induction over a
linked structure needs an explicit well-founded premise, such as finite acyclic
reachability under the memory model. Without one it is rejected. An induction
principle is checked by the kernel like any other rule. Unknown domains fail
closed.

## Mathematical domains

Specifications may eventually use proof-only mathematical objects: ℕ, ℤ, and
sequences, sets, and maps (Seq⟨T⟩, Set⟨T⟩, Map⟨K,V⟩). For example, a vector's
contract is easier to state over an abstract sequence:

```cpp
verified void grow_capacity(std::vector<int>& v, std::size_t n)
    ensures(model(v) == old(model(v)));
```

These objects have no runtime representation. They are never silently
identified with machine types: a C++ `unsigned` is not ℕ, and a C++ `int` is not
ℤ. What a model function says about a C++ type must be proven or stated as an
explicit trusted assumption.

Their source spelling is not specified. The names in this RFC, including
`model`, are metanotation, not reserved C++L identifiers. A later RFC chooses the
spelling. It must not reuse a C++ keyword such as `int`, must not ambiguously
shadow common C++ or `std` names such as `set` or `map`, must keep mathematical
and machine integers visibly distinct, and must stay verification-only.

## Consequences

The contextual words `data` and `match` are removed. The proof statements
`cases` and `induction` are added. No implementation, test, or editor grammar
used the removed words. Trust and runtime behavior do not change until the
construct is implemented. When it is, case analysis and each induction principle
will need kernel rules, with positive, negative, and adversarial tests. Those
tests must cover out-of-range enumeration values, valueless variants,
wraparound at the unsigned maximum, and cyclic pointer structures.

Unresolved: the source spelling of mathematical domains; how an arm names its
case's fields and its induction hypothesis; the labels each built-in principle
exposes; whether `cases` accepts compound expressions; and the syntax for
remaining-value arms.
