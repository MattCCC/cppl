# Quantified and implicational propositions

Status: implemented by this slice; normative rules are SPEC.md 8, 8.1-8.3, 9.1
and GRAMMAR.md 28, 29, 33.

## Meaning

A proposition may state universal quantification over binders of its own and
implication between two propositions:

```cpp
law identity_everywhere(unsigned x)
    ensures(forall (unsigned y) { Eq<unsigned>(identity(y), y) });

law zero_increments(unsigned x)
    ensures(Eq<unsigned>(x, 0u) -> Eq<unsigned>(add_one(x), 1u));
```

The first holds for every value of `y`, not only for the arguments a caller
supplies. The second claims nothing about its premise: what it states is the
conclusion under that supposition, which is what `expects(P) ensures(Q)` has
always stated for a Law.

Both may appear wherever a proposition may: a Law, a `proves` clause, an
`expects` or `ensures` clause, an `assume` statement, and nested in each other.
Implication is right associative and looser than every ordinary C++ operator, so
`A && B -> C` is `(A && B) -> C` and `A -> B -> C` is `A -> (B -> C)`.

## Why this adds no kernel rule

The kernel has had `Forall` and `Implies` since the first slice: a Law's
parameters are universally quantified and its precondition is an implication. All
this slice does is let an author write those two connectives directly, so the
proposition handed to the kernel is built from the constructors it already had,
checked by the rules it already had. Kernel rules added: none. Axioms,
assumptions and trusted mechanisms added: none.

## Which spellings are formal

C++ comes first (SPEC.md 3.1), so what is formal syntax is decided from syntax
alone, before Clang runs, and as narrowly as the grammar allows:

- `forall` and `exists` begin a quantifier only in the complete form
  `word (parameters) { proposition }`. Spelled any other way they are ordinary
  C++ identifiers, so a program with its own function named `forall` keeps it.
- `->` is implication only outside all brackets. Inside parentheses, an argument
  list or a quantifier body the expression is C++, and an `->` there is member
  access, which Clang resolves. Member access at the top level of a proposition
  is therefore written parenthesized. This is a real ambiguity in the grammar and
  is recorded as an open specification question; nothing modeled today depends on
  it, because pointers are not modeled.

A refusal is never a guess: a quantifier with no binder, a quantifier body that
is not braced, an empty proposition, a formal form nested inside a C++
expression, a quantifier in a loop invariant, and `exists` in its complete form
are each reported and produce no obligation.

## Surface and projection

The projector records the shape of the formal form it emitted and emits C++ that
makes Clang resolve everything inside it, as it already did for `Eq<T>`:
`forall (T x) { P }` becomes a lambda taking those parameters and returning `P`,
and `P -> Q` becomes a lambda whose body is `P;` then `Q;`. Clang declares the
binders, resolves their types and binds every use of them; the bridge walks the
recorded shape against the resolved lambda and refuses anything that is not the
shape it emitted. No declaration named `forall`, `exists` or `->` is introduced
into any namespace, and the runtime text is untouched, so runtime `->` keeps its
C++ meaning.

Because a binder is a C++ parameter Clang scoped, it shadows an outer name
exactly as C++ does, and the lowered proposition gives it the innermost de Bruijn
index. A premise about a shadowed parameter therefore does not close a goal about
the binder, which the kernel rejects and a negative test pins.

## Binders and the depth a statement is written at

A goal is no longer just a declaration's proposition closed over its parameters:
the proposition may add binders of its own. Proof lowering therefore carries how
many binders enclose the goal it is proving and states every term and assumed
proposition against that depth, rather than against the parameter list. That is
what makes `x` in `assume h : Eq<unsigned>(x, 0u)` denote the same parameter
however deeply the goal quantifies. Evidence instantiated at a term that mentions
a variable is offered only where those binders have been introduced; evidence
instantiated at closed terms may also stand at the goal's top level.

A binder a proposition writes for itself has no name a proof statement can use,
so evidence that stays quantified cannot be instantiated at one. That is stated
as a boundary, with its own diagnostic, and recorded as an open specification
question rather than worked around.

## Erasure

Nothing new is removed. A proposition already lived entirely inside a clause the
projector blanks, so the runtime text of a program using either form is the same
text it would have without the clause, byte for byte and line for line. The
erasure check is unchanged, and the erased program compiles as C++17, C++20 and
C++23 on its own.

## Validation

Positive: quantification over a pure function, several binders, a binder that
shadows the parameter, nested quantifiers, an ordinary C++ comparison as the
quantified body, implication chains, quantification over an implication, a
premise supposed under a binder and used by `rewrite`, contracts stating both
forms, a proof declaration stating a quantified proposition, and C++ functions
named `forall` and `exists` alongside runtime `->`, all across C++17, C++20 and
C++23 with the erased program's output unchanged.

Negative: false universals and implications, a false implication under a binder,
a premise about a shadowed parameter, a binder named in a statement, evidence
that stays quantified, wrong evidence at the wrong quantifier prefix, binder
types that are not modeled (`double`, a class, a reference), a quantifier with no
binder, an unbraced body, an empty body, a missing premise or conclusion, a
formal form nested in a call argument, `exists`, a quantifier in a loop
invariant, and nesting past the projection limit. Unit tests cover the lowering
to kernel quantifiers per binder, the implication lowering, a binder being
distinct from the parameter it shadows, malformed quantifier and implication
nodes never becoming obligations, a body reaching past its own scope, and both
false forms being rejected by the kernel.
