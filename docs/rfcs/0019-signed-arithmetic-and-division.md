# RFC 0019: Signed arithmetic, division and integer conversions

## Status

Accepted and implemented. Extends [RFC 0006](0006-machine-arithmetic.md), whose
refusal of signed arithmetic, division and remainder it replaces. The normative
rules are `SPEC.md` 29 (`ARITH-006` to `ARITH-013`), with Annex T
(`DEFINEDBEHAVIOR-001` to `DEFINEDBEHAVIOR-003`) and Annex U.5
(`ADMISSIBLE-005`).

## Summary

A signed `+`, `-`, `*` or unary `-`, a `/` or a `%`, and an integer conversion
whose value may not fit its target are C++ operations whose behavior is defined
only under a condition. Each one a verified body evaluates owes that condition
as a proof obligation, on the path that evaluates it: the exact result is
representable, the divisor is not zero, the operands are not the least signed
value and `-1`, the converted value fits. Where the condition holds, the value
is the one the core already knows how to compute, so the lowering states it with
the ring operations of RFC 0006 and three new kinds of kernel primitive:
representability of an exact sum, difference or product; truncating quotient and
remainder, made total; and two's-complement conversion between integer types.
Linear arithmetic learns what defines each of them. Integral promotions and the
usual arithmetic conversions are read from the conversions Clang put in the
program, never derived again.

## Motivation

RFC 0006 left signed `+ - *` refused because the obligation that would justify
them did not exist, and refused `/`, `%` and every conversion outright. That
refuses almost every real function over `int`: an index computation, a loop
counter, a digit accumulation, a comparison of a `short` with an `int`, a
division by a constant. Parser invariants, the next consumer of C++L, are made
of exactly these.

## Goals

- Exact C++ semantics for signed `+ - * / %` and unary `-` at every modeled
  width, with every defined-behavior condition an obligation on the path that
  evaluates the operation (`UB-002`).
- Division and remainder with C++'s truncation, for unsigned operands too.
- The integral promotions, the usual arithmetic conversions and explicit
  integral casts as Clang resolved them.
- Arithmetic that composes with contracts, refinements, branches, loops,
  calls and measures through the existing path walk and obligations.
- A kernel change that is small, general and checked independently.

## Non-goals

- Shifts and bitwise operators (`ARITH-005`, `DEFINEDBEHAVIOR-004`,
  `DEFINEDBEHAVIOR-005`), which stay refused.
- Conversions to or from `bool`, enumerations, floating point and pointers.
- Nonlinear reasoning: a product of two unknowns is modeled exactly, but
  nothing decides its representability unless the proof pins a factor or the
  operands' types keep every product within the result type.
- Compound assignment and increment of a type C++ promotes before arithmetic
  (`short s; s += 1;`): libclang does not expose the computation type, and it
  is not derived here.

## Static semantics

### Kernel primitives

All of them are total functions on the bit patterns of machine integer types,
like every core primitive. None is a C++ operator; lowering a C++ operator onto
one is the elaborator's business, together with every side condition C++ puts
on it.

```text
add_fits_T(a, b)   1 when value(a) + value(b), unbounded, is a value of T
sub_fits_T(a, b)   1 when value(a) - value(b), unbounded, is a value of T
mul_fits_T(a, b)   1 when value(a) * value(b), unbounded, is a value of T
quot_T(a, b)       trunc(value(a) / value(b)) reduced into T; 0 when b is 0
rem_T(a, b)        value(a) - trunc(value(a) / value(b)) * value(b); a when b is 0
convert_T(a : S)   value(a) reduced into T by two's complement, S any integer type
```

`quot` reduces only the one quotient that leaves its type, the least signed
value over `-1`, which wraps to itself; `rem` of that pair is 0. A zero divisor
gets a fixed value so the primitives are total; the value is never observed,
because C++ division by zero is an obligation the lowering never omits.

**Typing.** `add_fits`, `sub_fits` and `mul_fits` take two operands of `T` and
are booleans. `quot` and `rem` take two operands of `T` and are of `T`.
`convert` takes one operand of any supported integer type, its own type being
the source, and is of `T`.

**Normalization.** Each folds on literals. `add_fits` and `mul_fits` put their
operands in term order, since a sum and a product do not depend on it. `quot`
and `rem` also fold where the total definition decides them outright: a divisor
of 0, 1 or `-1`, and a dividend of 0; `quot(x, -1)` is `0 - x`. Anything else is
an opaque factor of the polynomial normal form. No identity of mathematical
division is used, so `quot(x, 2) * 2` is not `x`.

**Linear arithmetic.** The kernel states each primitive by what defines it,
exactly or not at all:

- `*_fits` known to be 1 bounds the unbounded sum, difference or product by
  `T`; known to be 0 puts it below the least value or above the greatest, a
  disjunction. A product is linear only when one factor is a constant. For a
  product of two unknowns, the kernel judges from the range each operand's
  variables always lie in, the type's own or, for a widening conversion, the
  narrower source type's: where the products of those ranges' extremes all fit,
  `mul_fits` holds whatever the values, so holding states nothing and failing
  is refuted. Otherwise it is a boolean like any other, which bounds nothing.
  This decides the product of two values promoted from 8- or 16-bit types,
  except two `unsigned short` values, whose product can exceed `int`; it
  decides nothing that needs a bound the facts establish.
- `convert_T(a : S)` equals `a` where every value of `S` is one of `T`, and is
  otherwise `a` minus a fresh multiple of `2^width(T)`, which bounding the
  conversion by `T` pins. The operand's type is read from the binders the
  step stands under, so the checker passes them to the constraint builder.
- `quot_T(a, c)` and `rem_T(a, c)` for a constant `c`, `|c| >= 2`, satisfy
  `a = c * quot + rem`, `|rem| <= |c| - 1`, and, for a signed type, `rem` has
  the sign of `a` or is 0. These determine truncating division uniquely, and
  the quotient cannot leave `T`.
- `rem_T(a, d)` for an unknown divisor: where `d >= 1`, `rem <= d - 1` (and
  `rem >= 1 - d` when signed); signed, `rem` has the sign of `a` or is 0;
  unsigned, `rem <= a`. Each holds for a zero divisor as well. The quotient by
  an unknown divisor is left unknown.

Every one of these is a constraint every machine assignment satisfies, so
nothing false is added; where the definition is not linear and the product rule
above does not apply, nothing is stated, which only loses completeness.

### Lowering and obligations

The bridge reads, from Clang's resolved AST, each implicit conversion (an
implicit cast between two modeled integer types) and each explicit integral
cast as a conversion node, `/` and `%` as binary operators, and unary `-` as
negation of its promoted operand. A negated integer literal is a literal. A
character literal is an integer literal of its character type.

The core term of each operation is its total primitive: a signed `a + b` is
`add_wrap(a, b)`, `-a` is `sub_wrap(0, a)`, `a / b` is `quot(a, b)`, a conversion
is `convert`. Each evaluation owes, as a `DefinedBehavior` obligation:

```text
signed a + b, a - b, a * b    add_fits / sub_fits / mul_fits(a, b)
signed -a                     sub_fits(0, a)
a / b, a % b                  b != 0
signed a / b, a % b           a == MIN -> b != -1
conversion of a to signed T   MIN_T <= a && a <= MAX_T, stated at the source type,
                              each side only where the source type exceeds it
```

Under the obligation the wrapping term wrapped nothing: `add_fits(a, b)` makes
`add_wrap(a, b)` the exact sum, so signed overflow is never modeled as wrapping
(`EQ-002`, `ARITH-003`). Unsigned `+ - *` and unary `-` stay modular with no
obligation, and a conversion to an unsigned type is reduction modulo `2^w`,
which C++ defines. A conversion to a signed type that may not fit is
implementation-defined before C++20 and modular since; this RFC requires
representability in every mode, so what is verified does not depend on the
mode. Wrapping narrowing that C++20 defines is refused, not assumed.

**Where the obligation is owed.** On every runtime path that evaluates the
operation, under exactly what that path supposes before it: its guards, the
arm of every enclosing `?:`, and the postconditions of the calls C++ sequences
before the operation, which are the calls inside its operands and in the
conditions of the `&&`, `||` and `?:` that select it. A call elsewhere
in the same expression is not sequenced before it, so its postcondition is not
supposed: a partial callee whose postcondition is false could otherwise make the
obligation vacuous while the overflow runs first. After the expression, the
path supposes every condition it proved, so what follows may use `x + 1 > x`.
`&&`, `||` and `?:` in a condition are already routes, so an operation in a
right operand is owed only where the left one let it run (`BOUNDARYEX-001`).

**Bodies and definitions.** A verified body that evaluates any such operation
is verified by the conditions walk of RFC 0007, as a body with a loop is, so
each obligation is a condition its contract rests on. A `pure` function is a
total core definition and carries no obligations, so a body containing one of
these operations is refused as a definition (`ARITH-011`).

### Specifications

A contract clause, a law, a proof proposition, a refinement predicate, a loop
invariant and a measure are never evaluated, but each is written in C++ and
means what C++ would compute. So a condition holds only where every operation
it would evaluate is defined: the definedness conditions of its operations,
each under the `?:` arms that select it, are conjoined in front of what the
condition states (`ARITH-010`). `ensures (result == x + 1)` states
`add_fits(x, 1) && result == x + 1`; `expects (!(x + 1 > 5))` holds only where
`x + 1` is defined, never at `INT_MAX`. A C++ condition whose operations are all
defined states exactly what it did before, so no existing proposition changes.
The argument a proof claims a law at is a term the kernel substitutes into the
law, never evaluated; one with a definedness condition is refused rather than
given a meaning C++ does not, and the condition belongs among the law's
premises instead.

### Assignments

`x += e`, `x -= e`, `x *= e`, `x /= e`, `x %= e`, `++x` and `--x` on a local of
a type C++ does not promote are the assignment `x = x op e` at that type, and
owe the operation's obligation. Where the type is promoted, or `e` is of a
wider type, the computation is in a type libclang does not expose, and the
update is refused.

## Runtime semantics

None change. Every operator and conversion compiles as written, and no check is
inserted: verification proves the conditions, it does not test them
(`RUNTIMECHECK-009`).

## C++ interoperability

Promotions and conversions are Clang's, including target-dependent ones: the
width of `long` and the signedness of `char` are whatever the selected target
gives them. A template is verified per specialization, with the conversions its
instantiation contains. `std::numeric_limits<T>::max()` is a call and is not
evaluated; `INT_MAX` and literals are.

## Safety

Rejected, each with its matched twin one step inside accepted by the tests:
`MAX + 1`, `MIN - 1`, `-MIN`, a product past the bound, division and remainder
by zero, `MIN / -1` and `MIN % -1`, and a conversion of a value that does not
fit a signed target. An operation on a path a guard excludes owes nothing on
that path.

## Trust impact

The kernel grows by six primitives: their typing, their folding on literals,
and the constraints linear arithmetic states for them (about 550 lines of
kernel code, comments included). No rule, axiom or assumption is added, and no
existing rule changes meaning; the constraint builder is passed the binder types
it already had at the check. Kernel and core versions move to 0.8.0, so no
evidence checked under 0.7.0 is reused.

The refutation search, which is untrusted, now also examines a wrap multiple
too wide to enumerate and a quotient by a constant value by value: it projects
the standing constraints onto that variable, and splits over the integers the
projection leaves it, or around zero when it leaves none or too many. Each case
it builds is an integer split the kernel checks.

The obligation generator, part of the correspondence layer, decides which
conditions C++ imposes and where they are owed. That is a stated trust in
`TRUST.md`, as for every other obligation it generates.

## Erasure

Nothing is added to the program, so nothing is erased beyond what already is:
contracts, invariants and measures. The expressions in a verified body are the
program's own and remain in it unchanged.

## Diagnostics

A failed obligation names the function, the operation as written, the
condition and the operand types, and cites the rule:

```text
f.cpp:10:12: error [kernel-rejection]: an operation in verified function 'f' is not shown to have defined behavior: signed overflow: 'x + 1' on the signed type 'int' is not shown to stay within 'int'
note: C++ leaves a signed addition undefined unless its exact result is a value of its type, so every evaluation owes that it is (SPEC.md ARITH-006, DEFINEDBEHAVIOR-001)
```

## Alternatives considered

- *Representability as comparisons over wrapping terms*, such as
  `(0 <= b -> a <= MAX - b) && (b < 0 -> MIN - b <= a)`. No kernel change,
  but no such statement exists for a product of two unknowns, and every
  obligation would be a puzzle to read in a diagnostic.
- *A mathematical integer domain with conversions to and from it.* The right
  model for specifications about unbounded values, and much larger: a new
  type, its arithmetic and its interaction with every rule.
- *Modeling C++20's modular narrowing.* Sound in C++20 and later only; the same
  source would then verify differently by mode. Requiring representability is
  sound in every mode and costs only programs that rely on wrapping narrowing.
- *Rejecting signed arithmetic in specifications.* It would refuse
  `ensures (result == x + y)`, the first contract anyone writes.

## Testing strategy

Kernel tests compare the folding of every new primitive with an evaluator built
from the host's own 64-bit division and checked 128-bit builtins, over every
pair of 8-bit values and over the edges and a fixed-seed sample of every width
to 64 bits, and check typing, malformed terms and hand-built certificates.
Automation tests pin values with order facts, so no rewriting can fold them, and
require the true statement proven and every false one refused, for every value
of small types. End-to-end tests cover every boundary at every width with a
matched twin, branches, refinements, loops, contracts, calls, promotions and
mixed signedness, and a generated property test compares the verifier's
accept/refuse decision and the program's computed value with the host on
random operands from a logged seed. Erasure tests compile the erased program
with plain Clang and require identical results. Each obligation, guard,
sequencing rule and kernel constraint is disabled in turn by
`scripts/test-mutations.sh`, and the suite must fail.

## Unresolved questions

- A product of two unknowns bounded by facts rather than by their types needs
  a product rule over established bounds; only the rule from the types' own
  ranges is added here.
- A `constexpr` call such as `std::numeric_limits<int>::max()` could be read
  as the constant Clang evaluates it to.
- Shifts and bitwise operators need their own definedness rules.
