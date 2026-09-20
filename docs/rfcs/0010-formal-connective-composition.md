# Formal connective composition

Status: implemented. Semantics are SPEC.md 7.6-7.7 and GRAMMAR.md 30, 33.

Formal equalities and quantified propositions may compose through conjunction.
Equivalence means both implications and requires evidence for each direction.
It is left associative and looser than implication. This implements retained
syntax without introducing a new primitive or relaxing equality.

The projector records formal connective structure and uses an analysis-only
lambda with two statements. Clang resolves each C++ leaf and all binder types.
The bridge checks that shape; typed VIR carries a connective and its operands.
Lowering expands equivalence into the kernel's existing conjunction and
implication constructors. No logical template, type or function is installed in
a user namespace, and formal propositions cannot be passed as runtime values.
Expansion is bounded before building the derived core tree, so repeated
equivalences cannot allocate an exponentially growing unchecked proposition.

Kernel rules, assumptions, axioms and trusted mechanisms added: zero. Existing
erasure removes the containing clauses and proofs; no executable rewrite is
introduced. Correspondence tests cover mixed/nested syntax, precedence,
quantifier scope, wrong evidence, false directions, malformed operands,
written-proof failure, and standalone erased execution in C++17/20/23.

Two boundaries are pinned by tests rather than hidden. A proof body still names
a conjunctive premise whole, because conjunction elimination has no written
spelling; automation projects the sides and the kernel checks that. A premise
stated as an equivalence yields both implications, not a case analysis, so a
conclusion that needs the absent direction of a false side is unproven rather
than assumed. Disjunction remains refused.
