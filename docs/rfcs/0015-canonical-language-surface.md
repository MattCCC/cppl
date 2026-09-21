# RFC 0015: Canonical language surface

Status: Accepted (owner-directed syntax standardization)

This RFC supersedes surface spellings and formatting examples in RFCs 0001–0014.
[SPEC.md](../SPEC.md) owns semantics; [the grammar](../GRAMMAR.md) owns concrete
syntax. It does not introduce a new proof rule, axiom, mathematical domain or
runtime representation.

Functions ensure. Laws prove. A Law has one optional `expects (P)` and exactly
one `proves (Q)`. Its terminator is `;` for checked automation or a proof body
for explicit evidence. Only `trusted law` introduces an assumption.

Specification clauses require parentheses and one space before `(`. Functions
order `expects`, `ensures`, `decreases`; Laws order `expects`, `proves`; loops
order `invariant`, `decreases`. Each kind occurs at most once. Clauses occupy
continuation lines. Refinement `where (P)` stays attached to its declaration.
Ordinary C++ specifiers precede `verified pure`. `result` belongs only to
non-void function postconditions. `old(expression)` denotes pre-state values
only in function postconditions. `self` denotes the candidate in a refinement;
member contracts use ordinary C++ `this` and member lookup.

Indexed refinements declare typed indices with parentheses and apply them with
angle brackets. Proof commands remain statements: `refl;`, `exact evidence;`,
`apply evidence;`, `rewrite equality;`, `assume name : proposition;`. The latter
names a context-supplied premise; it never invents one. `cases`, `decompose` and
`induction` use one `label(binders) => { statements }` arm grammar. C++ `case`
retains its C++ meaning. Residual states remain explicit; `_` is not a catch-all.

A function entity has one logical contract. Put it on the public declaration;
the matching definition inherits it without repetition. Conflicting
redeclarations are errors. Templates preserve the contract at instantiation.
No syntax change permits an unproved declaration or unverified callee summary
to act as evidence.

Migration is explicit: replace Law `ensures` with `proves` only when no invalid
`result` reference is present, parenthesize clauses with unambiguous boundaries,
and combine repeated Boolean predicates in source order with `&&`. Never merge
termination measures as conjunction, discard comments, or fix a proposition by
changing its meaning. Compiler acceptance is strict; migration is an editor
operation, not a compatibility dialect.

The shared frontend and formatter serve the CLI, LSP and CI. Unsupported
verification semantics still fail closed. Syntax standardization does not
promote backend maturity or turn a failed proof into successful compilation.
Validation covers accepted/rejected syntax, contextual C++ names, format
idempotence, source locations, editor edits, proof failure and erasure.
