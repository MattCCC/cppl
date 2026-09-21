# RFC 0002: Compositional verified calls

> Surface syntax and formatting in this historical RFC are superseded by
> [RFC 0015](0015-canonical-language-surface.md) and the
> [normative grammar](../GRAMMAR.md). Semantic rationale remains applicable.

## Status

Implemented initial fragment. Extends RFC 0001 before control-flow verification.

## Decision

Resolve calls through Clang and instantiate each verified callee's contract at
the actual arguments. Generate a separate obligation for every `expects` clause.
Only after that obligation and the callee contract pass the kernel may the
caller use the callee's `ensures` clause. Calls retain their original runtime
arguments and body; erasure adds no checks or wrappers. See `SPEC.md` 12.6.

## Proof construction

Preserve the caller's body-derived goal `forall params. P -> Q[R/result]`.
For candidate reasoning, bind a fresh logical result for each verified call and
suppose its instantiated postcondition. A call's precondition can depend only
on the caller's precondition and preceding call summaries. Nested argument calls
precede enclosing calls; all expressions are pure, so this imposes no runtime
evaluation order.

Prove the abstract reasoning goal with existing introduction, hypothesis,
equality-rewrite, and reflexivity evidence. Instantiate its result binders at
the actual call terms, then discharge each summary premise using an already
proven callee theorem and the call's proven precondition. Check the completed
proof against the body-derived goal.

Export a callee theorem only after its own contract passes. Equality elimination
transports its body proof along `g(params) == R`, checked by reflexivity against
the actual lowered definition. No assumed summary or new logical rule connects
the contract to the code. Kernel rules added: 0. Logical assumptions added: 0.

## Boundaries

Only the existing single-return, pure integer fragment is accepted. Contract
predicates are equalities; modeled arithmetic is unsigned addition, without
algebraic reassociation. Ordering and subtraction in bounded-increment examples
require a later expression slice. Definitions must be available in the current
translation unit, including headers. Verified-call dependencies must be acyclic.

Caller candidate generation cannot unfold a verified callee, even one also
declared `pure`. Specifications and ordinary pure helpers retain their existing
definition model. Preconditions hidden behind uncontracted pure helpers cannot
be discharged. Ordinary C++ callers remain permitted and receive no checks.

## Validation

Positive tests compile and execute nested calls, dependent preconditions,
multiple parameters, overloads, forward declarations, and unsigned wrapping in
C++17, C++20, and C++23. Erased native C++ retains the original calls. Rejection
tests cover missing or false preconditions, ignored results, weak summaries,
failed callees, argument capture and effects, wrong overloads, future-summary
use, recursion, and unsupported ordering. Unit tests exercise binder scope,
forged summaries, absent summary evidence, and broken body-to-contract linkage.

## Follow-up

Conditionals and path-sensitive obligations; locals and richer expressions;
loops with invariants; recursion with termination/induction; references and
state; automation and SMT.
