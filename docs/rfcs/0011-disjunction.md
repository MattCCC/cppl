# Disjunction

> Surface syntax and formatting in this historical RFC are superseded by
> [RFC 0015](0015-canonical-language-surface.md) and the
> [normative grammar](../GRAMMAR.md). Semantic rationale remains applicable.

Status: implemented. Semantics are SPEC.md 7.8 and GRAMMAR.md 33.

`P || Q` is a proposition of the formal core, not a Boolean value and not an
encoding over one. It is established by evidence for one of its sides, and
evidence for it is used by proving the goal again under each side. Both are
kernel rules; neither is an assumption about which side holds.

## Kernel change

Proposition constructor `Or(left, right)`. It binds no variable, so de Bruijn
indices mean the same in both sides, and substitution, shifting, description,
validation and obligation hashing recurse into both.

Two proof constructors, taking the kernel from eleven rules to thirteen:

```text
DisjunctionIntroduction    p : A                p : B
                       -----------          -----------
                          A \/ B               A \/ B

DisjunctionElimination  p : A \/ B   q : A -> C   r : B -> C
                        -----------------------------------
                                        C
```

Introduction is goal-directed: the goal states both sides and the evidence
selects one, so a proof cannot widen the goal to a side it finds easier.
Elimination is evidence-directed and restates the disjunction it eliminates,
as universal, implication and conjunction elimination restate theirs, because
the goal names neither side. The kernel checks the evidence against the
restatement and each case against its own side of it. Cases are ordinary
implications, so the premise a case may use is the one an enclosing introduction
puts in the context and no new hypothesis mechanism is added.

The core admits no excluded middle. `P || not P` is not granted, and a goal that
needs it is unproven rather than assumed.

## Frontend and lowering

Clang resolves `||` as it resolves any C++ operator; its operands are pure
specification expressions, so which of them C++ would evaluate does not enter
into what the proposition says. Where an operand is formal syntax the projector
records a two-operand connective shape, the same one conjunction and equivalence
use. `||` is looser than `&&` and tighter than `->` (GRAMMAR.md 33).

A `||` where a value is required - a returned expression, a condition a path is
taken on, a loop invariant - is refused, as `&&` is. Nothing is encoded as a
Boolean to make it fit.

## Automation

Two shapes are proposed and each is put to the kernel: introduction of a
selected side, and, where a supposed premise is a disjunction, a case analysis
that proves the goal again under each side. Each disjunctive premise is split
once, so the analysis terminates. Automation decides nothing: a shape it offers
is evidence only if the kernel accepts it.

## Trust

Kernel rules added: two. Logical assumptions, axioms and trusted mechanisms
added: zero. The kernel and core versions move to 0.5.0, so evidence accepted
under 0.4.0 is not carried over. Existing erasure removes the containing clauses
and proofs; no executable behaviour is introduced.

Tests cover valid evidence for each side, a false disjunction, excluded middle,
a side wrongly taken from a disjunctive premise, a case that covers the wrong
side, a case analysis on invented evidence, malformed operands, binder capture,
hypothesis indices inside a case, value/guard/invariant refusals, written-proof
failure, and standalone erased execution in C++17/20/23.
