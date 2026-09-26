# Machine arithmetic: normalization and linear arithmetic

> Surface syntax and formatting in this historical RFC are superseded by
> [RFC 0015](0015-canonical-language-surface.md) and the
> [normative grammar](../GRAMMAR.md). Semantic rationale remains applicable.
>
> Signed arithmetic, division, remainder and integral conversions, which this
> RFC refuses, are admitted with their defined-behavior obligations by
> [RFC 0019](0019-signed-arithmetic-and-division.md); its first unresolved
> question below is answered there. "Unsigned operands" below means operands
> whose common type, after the integral promotions and the usual arithmetic
> conversions, is unsigned (SPEC.md ARITH-003): two `unsigned char` or
> `unsigned short` operands promote to `int` and are a signed operation.

Status: implemented by this slice; normative rules are SPEC.md 7.1, 7.5, 12.5
and 29.

## Summary

Unsigned C++ `+`, `-` and `*` are the operations of the ring of integers
modulo 2^width. The core gains wrapping subtraction and multiplication beside
wrapping addition, definitional equality decides every commutative-ring
identity of those operations, and comparisons are put in a canonical form that
the machine type justifies. Order consequences such as `i < n -> i + 1 <= n`
are not identities, so they are established by a ninth kernel rule, linear
arithmetic, which checks a certificate against a system of integer constraints
the kernel states itself. Signed arithmetic stays refused.

## Motivation

Without these, `result == x + 2u` could not be proven of a body that adds one
twice, `x - 0u` could not be stated at all, and no loop counter could be shown
to stay within its bound. A loop invariant needs order reasoning at every
preservation and exit obligation; normalization alone cannot supply it.

## Static semantics

**Primitives.** `sub_wrap` and `mul_wrap` join `add_wrap`: total operations on
two's-complement bit patterns of one machine type, each the ring operation of
the integers modulo 2^width. Lowering maps C++ `+`, `-`, `*` onto them only for
unsigned operands of one modeled type, where C++ defines the result modulo
2^width exactly (SPEC.md 29.2). Signed operands are refused: C++ leaves their
overflow undefined, and the obligation that would justify a wrapping model is
not part of the core. `bool` arithmetic promotes to `int` and is refused as the
conversion it is. Division, remainder, shifts and bitwise operators are refused.

**Definitional equality.** Normalization reads an arithmetic term as a
polynomial over opaque factors (variables, comparisons, selections) with
coefficients modulo 2^width, and renders it canonically: monomials in a fixed
structural order, like terms combined, zero terms dropped, constants folded.
Two terms are definitionally equal when their renderings coincide, which
happens only when they are equal as polynomials over the ring, and so only when
they denote the same machine value for every assignment. Associativity,
commutativity, distributivity, identities of zero and one, and cancellation of
addition all follow; nothing about order does.

**Canonical comparisons.** `a > b` is `b < a`; `a <= b` is `!(b < a)`;
`a >= b` is `!(a < b)`; `a != b` is `!(a == b)`; a double negation cancels.
`a == b` is stated of the difference `a - b` against zero, choosing between the
difference and its negation by structural order, because equality is
cancellative in the ring. `a < b` is decided only between literals, between
identical terms, and at the bounds of the type (`x < 0u` and `max < x` are
false). No arithmetic moves across `<`: wrapping makes order non-cancellative.
Selection with equal arms is that arm, and selection on a negated condition
swaps its arms.

**Linear arithmetic.** The rule's proof term carries facts, each a proposition
with its own evidence, and a certificate. The kernel checks every piece of
evidence, then builds the system itself: each term is normalized and read as a
polynomial of its type; each monomial becomes an integer variable bounded by the
type; a polynomial that is not a single monomial becomes its integer reading
minus a fresh integer multiple of 2^width, bounded by the type. That is exact:
every machine assignment yields an integer solution and every integer solution
is one. An equality of values becomes two inequalities; a comparison known to be
one or zero becomes its order or its negation; a disequality becomes a
disjunction of the two strict orders. The goal's negation joins the system. The
certificate refutes it by a tree of three steps: a Farkas sum (positive
multiples of standing constraints summing to a positive constant), an integer
split (a linear form is at most zero or at least one), and a case split on a
disjunction. Every leaf must be a Farkas sum, arithmetic is exact in 128 bits
with overflow refused, and certificate size and depth are bounded.

## Runtime semantics

None change. `+`, `-` and `*` compile as written; no check is inserted.

## Trust impact

The logical TCB grows by the polynomial normalizer, the canonical comparisons,
the constraint builder and the certificate checker: about 1,000 lines of
deterministic kernel code, comments included, with no dependency. The rule is one capability and no
assumption. Certificates are found by Fourier-Motzkin elimination with case
splitting in `compiler/automation`, which is untrusted: the kernel re-derives the
system and checks every step. Kernel and core versions move to 0.3.0.

## Alternatives considered

- *Order axioms as separate rules* (transitivity, successor, antisymmetry):
  several rules, each small, and a search to chain them. The certificate rule is
  one rule that is complete for the linear fragment.
- *An SMT solver*: a larger trusted component unless it emits checkable proofs,
  which is Phase 13 work. The certificate format here is that shape already.
- *Mathematical integers with overflow side conditions*: the right model for
  signed arithmetic, which needs UB obligations first (SPEC.md 31).

## Testing strategy

A differential test evaluates random terms and their normal forms with an
evaluator written independently of the kernel, on every assignment of small
types and at the edges of 64-bit types. Kernel tests cover ring identities,
non-identities, typed comparisons, budgets, and hand-built certificates,
including malformed, one-sided, overflowing and misapplied ones. Automation
tests cover loop-shaped goals, wrapping at 32 and 64 bits, contradictions and
goals that must stay unproven. Compiler tests cover the fragment end to end and
refuse false order strengthenings, wrap-around claims, and signed arithmetic.

## Unresolved questions

- Signed `+`, `-`, `*` need no-overflow obligations; the linear rule can
  discharge them once they are generated.
- Nonlinear order facts (`x * x >= 0`) are outside the linear fragment.
