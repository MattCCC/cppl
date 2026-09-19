# Path-sensitive verification and integer comparisons

Status: implemented by this slice; normative rules are SPEC.md 12.7.

Clang-resolved `if` statements, blocks, and returns form a finite return tree.
Sequential fallthrough becomes the continuation of each arm that falls through.
Every leaf must return a modeled integer expression. All statements are checked;
unsupported constructs, missing returns, and unreachable trailing statements fail
closed. No runtime source is rewritten.

Each leaf generates `forall params. expects -> path conditions -> ensures[R/result]`.
Call preconditions use only earlier conditions and earlier justified summaries.
Calls in conditions are checked before the condition becomes available. Summaries
from another arm never enter the path. The existing contract-only reasoning and
body-linkage checks apply independently on every path.

The core gains total typed integer comparisons, boolean negation, and selection.
Booleans are represented internally by the unsigned one-bit integer (0 or 1).
Equality predicates retain propositional equality. Other comparisons are equalities
between a comparison's boolean value and 1; negation changes the required bit.
Only concrete comparisons compute. No symbolic order weakening, transitivity,
arithmetic reassociation, subtraction, or signed addition is introduced.

One new proof rule, conditional elimination, is necessary to export a single
contract for a branching callee. The previous seven rules cannot eliminate a
conditional from two path proofs. Given a typed condition c, values a and b, and
a checked proposition context Q, the kernel requires both `predicate(c,true) ->
Q[a]` and `predicate(c,false) -> Q[b]`, and independently derives
`Q[select(c,a,b)]`. It checks the condition, both values, the motive, both premises,
and the exact resulting goal. This adds one kernel rule and zero axioms.

The definition contains the complete return tree. The assembled proof establishes
the postcondition of that tree before the existing equality transport exports the
callee theorem. A missing or forged path cannot export a summary. Kernel/core
versions change because the accepted calculus changes.

Validation includes all comparison operators, both branch outcomes, nested and
fallthrough paths, guards containing calls, branch-local call obligations, signed
comparisons, runtime erasure, and malformed conditional evidence. Negative cases
cover false arms, hypothesis leakage, future guard evidence, unsupported control
flow/conversions/effects, and deliberately unsupported arithmetic implications.

Next: straight-line locals and assignments; then arithmetic normalization; then
loops with explicit invariants. Recursion, memory reasoning, and SMT remain later.
