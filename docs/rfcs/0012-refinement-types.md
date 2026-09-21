# Refinement and indexed refinement types

Status: implemented. Semantics are SPEC.md 17, 17.5, 18 and GRAMMAR.md 14, 15, 16.

A refinement type is a verification-level type over an ordinary C++ base type:

```text
R = { self : T | P(self) }
```

It introduces no runtime representation of its own. Two refinements of one base
type are the same type to the machine and different types to the verifier.

## Erasure gains a second class

The declaration form cannot survive an erasure that only deletes. Blanking

```cpp
type Percentage = int where (self >= 0 && self <= 100);
```

leaves `Percentage` undeclared in the runtime program, so SPEC.md 17.4 - a
refinement has the runtime representation of its base type - could not hold for any
use of the name. Every other C++L form is additive decoration around text that is
already valid C++; this one is not.

C++L syntax therefore has two erasure classes (TRUST.md 10.1). Proof-only syntax is
blanked as before. A runtime-bearing declaration is replaced by the canonical C++ it
means:

```text
type R = T where (P);        ->  using R = T;
type R(I i) = T where (P);   ->  template <I i> using R = T;
```

The lowering is derived from the declaration alone, and `compiler/erasure`
recomputes it from the recognized declaration rather than trusting the projector, so
nothing else can be put in a declaration's place. It carries one newline per newline
of the declaration, so no line of the program moves and a Clang diagnostic below one
still maps to the line the author wrote. An alias is C++11, so no construct from a
later standard is introduced. The class is deliberately narrow: it is for
declarations whose runtime representation must remain present, not a general
source-to-source rewrite.

## Semantic model

`type` and `where` stay contextual. Only the complete form is C++L, so `type x = 5;`,
`type f(int);`, `using type = int;` and a `where` inside the base type's brackets all
remain ordinary C++.

Clang resolves the base type and the predicate. `self` is projected as an ordinary
parameter of the base type, and each index as a parameter before it, so every name in
a predicate is one Clang binds. An index written as a bare name takes the base type.

Refinement identity is carried beside the erased type rather than in place of it:
`vir::Type` keeps its base together with the refinements it names and the values
their indices were applied at. Ordinary type comparison is the erased one, because
that is what code generation and arithmetic are about; refinement identity is asked
where verification needs it. Clang canonicalizes a refinement to its base type, so
the name is recovered from the alias declaration the written type came through -
not from the type's spelling.

The projector records the physical analysis-buffer identity of each generated
alias. The bridge follows Clang alias declarations, including ordinary `using`
and `typedef` chains, and associates only that alias with its predicate probe.
Identically spelled declarations in separate namespaces therefore remain distinct.
Constant indexed applications in an alias chain retain their own resolved
arguments; unresolved substitution fails closed.

## Introduction and elimination

A declaration asserts nothing. Membership is an obligation with its own origin,
stated where a value enters the type and closed under what the path supposes there,
so a branch fact discharges it:

```cpp
if (x >= 0) {
    NonNegative n = x;   // owes  x >= 0, which the branch establishes
}
```

Elimination is the other direction. A refined parameter's predicate is supposed
inside the body, so the author never restates it as an `expects` clause; a refined
result is stated with the postcondition and proven on every path that returns; and
using a refined value as its base value requires nothing.

A refined return is itself a postcondition: `verified R f(...) { ... }` owes
every predicate of `R` on every return path, without a duplicate `ensures`.
Ordinary and merely `pure` refined-return declarations are rejected unless the
same callable has a verified definition. The structural LSP linter defers this
decision to shared Clang/elaboration analysis instead of guessing from spelling.

A refinement of a refinement states every predicate that applies, so a value
entering `Percentage = NonNegative where (self <= 100)` owes both. An indexed
refinement states its predicate at the values its indices were applied at, which
Clang has already evaluated.

## Trust

Kernel rules, assumptions, axioms and trusted mechanisms added: zero. A refinement
predicate becomes an ordinary proposition and its obligations are checked by the
kernel like any other. No wrapper type, constructor, hidden field, runtime
predicate, runtime check, RTTI distinction or ABI-visible state is generated, and no
unproven membership is accepted or turned into a runtime check.

What is new in the trust boundary is the second erasure class, which is why erasure
recomputes each lowering and why the tests check the emitted C++ as text and run the
erased program compiled by Clang alone.

## Flows and implication

Every flow of a value into a refinement type states the predicate, not only the one
a declaration spells. An assignment or update carries the local's declared type just
as its declaration did, so a write cannot reach a refined local without owing what
the declaration owed. An argument of a call to a verified function owes the
parameter's predicate through that function's precondition, which is where a refined
parameter's predicate already lives.

This requirement also applies to the partial-correctness path for loops and their
callers. Fresh loop-head versions obtain their facts from invariants; every local
write still generates membership evidence. Missing predicate metadata or index
arguments is an error, never an empty requirement.

Crossing between two refinements of one base type is implication and nothing else
(SPEC.md 17.3.2). A value already of a refinement type carries its predicate, so the
goal at the crossing is `P(v) -> Q(v)` under the path conditions there: the looser
direction discharges from what the value has, the stricter direction owes the rest.
No runtime check exists in either direction, because there is nothing to check - both
types are `T` once erased.

That same erasure makes two refinements of one base type one C++ signature, so two
overloads distinguished only by which refinement they name are a redefinition. The
analysis text carries the aliases, so Clang reports it at the declaration the author
wrote rather than in emitted output.

## References alias storage

A reference local that binds a tracked local object is not given a value or a fact
of its own. It resolves to the referent's storage, so a read through it is a read
of that storage's current logical version and a write through it is a write to
that storage, versioned like any other. This is why no alias-invalidation
machinery is needed: a refinement fact describes a version, and a write through
any alias produces a new version, so there is never a stale fact to retract.

A write through an alias owes the predicates of the reference's own refinement and
of the referent's declared type together, because both types still describe that
one storage. The binding itself is a crossing and owes the reference's predicate
at the referent's current version. Anything that is not a direct binding to a
tracked local - a temporary, a parameter, a subobject, a call result - is refused
rather than approximated.

## Boundary

Not modeled, and refused rather than approximated: refined returns of an unverified
function, refined members, reference and pointer parameters, pointers, and
refinements in templated contexts. SPEC.md 17.3.1 states the fragment.
