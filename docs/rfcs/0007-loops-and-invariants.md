# Loops against explicit invariants

> Surface syntax and formatting in this historical RFC are superseded by
> [RFC 0015](0015-canonical-language-surface.md) and the
> [normative grammar](../GRAMMAR.md). Semantic rationale remains applicable.

Status: implemented by this slice; normative rules are SPEC.md 23 and 24.3.

## Meaning

`while (c) invariant (I) { body }` and `for (init; c; step) invariant (I) { body }`
are verified by the partial-correctness loop rule. Each local the loop writes
is carried: at the head it is a fresh value of which only the invariants and
the condition are known. The body must be verified to take any state at the
head that satisfies the invariants and the condition to a state satisfying the
invariants again, at every end of an iteration: the body's end followed by the
step, and `continue` followed by the step. The invariants must hold on entry.
What follows the loop is verified supposing the invariants and the negated
condition; a `break` continues with what follows the loop from the state at the
`break`. Several invariants conjoin by being proven one by one and supposed one
by one, so no conjunction connective is needed.

Nothing here proves termination. A function with a loop therefore has a
partial-correctness contract: "if it is called in a state satisfying its
precondition and returns, its postcondition holds".

## Why a loop never becomes a core definition

Until now a verified body was one total core term, admitted as a definition,
and its contract was a kernel theorem about that definition. A loop has no
total term. Treating a loop function as an opaque total symbol whose contract
is a theorem would be unsound: a loop that never exits satisfies every
postcondition vacuously, and `forall x. spin(x) != spin(x)` would then be a
kernel theorem from which anything follows. So a function with a loop is never
admitted to the kernel context, its contract is never a theorem about its
value, and no Law or specification can mention it. A verified caller uses its
contract as it uses any contract - the result is a fresh value of which the
postcondition is supposed - and becomes partial itself.

## How it is checked

The contract is established from verification conditions, each an ordinary
proposition over total terms that the kernel checks: an invariant on entry, an
invariant at the end of an iteration path, a call precondition, and a return's
postcondition. A path binds each verified call's result and each loop head as a
fresh variable followed by what is supposed of it. The invariant at the end of
an iteration is stated with the head values abstracted and instantiated at the
next values by the kernel's substitution, so the value an iteration started
from and the value it ends with are never confused. A condition is offered to
the kernel only once every contract it supposes is established.

## What is trusted

No kernel rule and no logical assumption are added. The loop and call rules
that decide _which_ conditions a body needs are applied in `compiler/obligations`
and are correspondence trust (TRUST.md 41.2), as is the bridge's decision of
which locals a loop carries; every iteration end checks that each uncarried
local still holds its head version, so a write the scan missed rejects the body.
Replacement path: with induction over a proof-only natural-number domain, loop
partial correctness can be stated with an iteration term and the loop rule
derived in the kernel.

## Surface and projection

The clauses are C++L only inside a verified function and before a block body.
The projector blanks them from both texts and inserts one generated `bool`
declaration per invariant just inside the body's `{`, so Clang resolves the
invariant in the scope of the loop head; the bridge reads those declarations
back as invariants and never as statements, and an unconsumed one rejects the
body. `decreases`, `do`/`while`, range-based `for`, `for` without a condition,
conditions that declare variables, and invariants outside verified functions
are refused.

`+=`, `-=`, `*=`, `++` and `--` on locals landed just before this slice, so that
idiomatic loop steps are modeled; they are the assignments they abbreviate.

## Erasure

The invariant clauses are the only text removed. Every loop, condition, step,
statement, `break` and `continue` is preserved byte for byte, and the erased
program compiles as C++17, C++20 and C++23 on its own.

## Validation

Positive: counting loops, `for` loops with several invariants, related carried
locals, untouched locals, count-down, `return` and `break` inside the body,
`continue`, nested loops with a multiplicative invariant, verified calls inside
the body, and a verified caller of a loop function. Negative: invariants false
on entry or not preserved (including through `continue`), head- and entry-value
leaks past the loop, the condition misused after exit, weak invariants, `break`
and `return` paths, call preconditions in the body and in the condition, nested
invariants, a nonterminating loop reaching a Law, a contract or a `pure`
function, loops in `pure` functions, and every refused construct. Unit tests
cover the generated conditions, that no definition is admitted, a false
invariant blocking a caller, and malformed VIR (an iteration outside its loop,
a head read before its loop, a rebound version).
