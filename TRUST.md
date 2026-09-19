# C++L Trust Model

This document defines the **Trusted Computing Base (TCB)** and trust boundaries of C++L.

It does **not** define C++L language semantics.

Language syntax and semantics are defined in [SPEC.md](SPEC.md).

The purpose of this document is to answer:

> What must be trusted for a C++L claim marked `PROVEN` to mean what it claims to mean?

The objective is not to eliminate all trust.

The objective is to make trust:

- minimal
- explicit
- auditable
- reproducible
- machine-visible
- difficult to expand accidentally

---

# 1. Scope

`SPEC.md` defines what C++L constructs mean.

This document defines which components must behave correctly for those meanings and proofs to remain valid.

In particular, this document covers:

- the Trusted Computing Base
- proof-kernel trust
- compiler/frontend trust
- Clang/LLVM trust
- solver trust
- AI trust
- FFI trust
- trusted assumptions
- trust propagation
- erasure trust
- proof-cache trust
- reproducibility
- verification status reporting

This document intentionally does **not** redefine:

- Laws
- dependent types
- refinement types
- equality
- induction
- termination semantics
- contracts
- contextual keywords
- language grammar

Those belong in `SPEC.md`.

---

# 2. Core trust principle

A successful C++L build answers two different questions:

```text
Did the program compile?

Did the claimed Laws follow from the stated assumptions?
```

These questions MUST remain distinct.

Compilation success does not imply proof success.

Proof success does not imply absence of external trusted assumptions.

---

# 3. Trusted Computing Base

The **Trusted Computing Base (TCB)** is the set of components whose incorrect behavior could cause C++L to accept a false proposition as proven.

The TCB SHOULD be minimized aggressively.

Target architecture:

```text
┌───────────────────────────────────────┐
│ Untrusted / independently checked     │
│                                       │
│ parser                                │
│ elaborator                            │
│ tactics                               │
│ proof search                          │
│ AI-generated code                     │
│ AI-generated proofs                   │
│ SMT automation                        │
│ optimizers                            │
└──────────────────┬────────────────────┘
                   │
                   │ proof evidence
                   ▼
┌───────────────────────────────────────┐
│ Trusted proof boundary                │
│                                       │
│ small proof kernel                    │
│ primitive formal semantics            │
│ explicitly declared axioms            │
└──────────────────┬────────────────────┘
                   │
                   ▼
                PROVEN
```

The project SHOULD prefer:

```text
large untrusted producer
+
small trusted checker
```

over:

```text
large trusted verifier
```

where practical.

---

# 4. Proof kernel

The proof kernel is the final authority for logical proof validity.

It SHOULD be:

- small
- deterministic
- dependency-light
- auditable
- fuzzable
- heavily negatively tested
- formally specified
- isolated from unrelated compiler functionality

The kernel MUST reject invalid or malformed proof evidence.

The kernel SHOULD contain only the minimum logic required by the formal core defined in `SPEC.md`.

---

# 5. Kernel trust boundary

The kernel MUST NOT depend on the correctness of:

- AI reasoning
- tactic heuristics
- solver heuristics
- IDE features
- source formatting
- diagnostics
- optimization strategies
- proof search order

Those systems may generate candidate evidence.

The kernel decides whether that evidence is valid.

Conceptually:

```text
complex producer
    ↓
candidate proof
    ↓
kernel
    ├── valid   → accept
    └── invalid → reject
```

---

# 6. Frontend and elaborator

The C++L frontend and elaborator are complex components and SHOULD NOT automatically belong to the logical TCB.

Preferred architecture:

```text
source syntax
    ↓
frontend
    ↓
explicit core representation
    ↓
kernel validation
```

The frontend may be wrong.

The elaborator may be wrong.

Their output MUST still satisfy the kernel.

Where frontend correctness cannot yet be independently checked, that dependency MUST be documented explicitly.

---

# 7. Separation of logical trust and runtime trust

C++L has two distinct trust layers.

## Logical trust

Logical trust determines:

```text
Is this proposition actually established?
```

This is primarily the responsibility of the C++L proof system and kernel.

## Runtime trust

Runtime trust determines:

```text
Does the executable preserve the behavior that was reasoned about?
```

This includes lowering, erasure, C++ compilation, ABI behavior, and execution.

These two forms of trust MUST NOT be conflated.

---

# 8. Clang and LLVM

C++L relies on Clang and LLVM for ordinary C++ compilation and native code generation.

They are not intended to define theorem validity.

Conceptually:

```text
C++L kernel
    decides logical validity

Clang / LLVM
    compile runtime semantics
```

Clang/LLVM therefore belong primarily to the **runtime trust chain**, not the logical proof kernel.

Any guarantee about the final executable depends on the backend preserving the semantics of the generated C++.

---

# 9. C++ semantic dependency

C++L proofs about executable behavior depend on the C++ semantic model used by the verifier corresponding to actual runtime behavior.

The project MUST document which parts of C++ semantics are modeled sufficiently for verified claims.

Where runtime semantics are not modeled, the relevant code MUST remain:

```text
UNVERIFIED
```

or:

```text
UNSAFE
```

or depend on an explicit:

```text
TRUSTED
```

boundary.

The verifier MUST NOT silently claim semantics it does not model.

---

# 10. Erasure trust

Proof erasure is correctness-critical.

Conceptually:

```text
verified C++L
    ↓
proof / ghost erasure
    ↓
ordinary C++
```

The erasure implementation must preserve the runtime behavior defined by the verified program.

The erasure pass MUST NOT silently:

- remove required runtime validation
- introduce undefined behavior
- alter runtime values
- change observable control flow
- invalidate lifetime assumptions
- change promised ABI behavior
- make ghost state observable

Erasure SHOULD eventually have independent equivalence validation.

---

# 11. Erasure verification goal

The long-term target is to establish a property of the form:

```text
runtime_behavior(C++L_program)
=
runtime_behavior(erased_C++_program)
```

for the executable semantics relevant to the verified program.

The exact formal statement belongs in the formal semantics and erasure documentation, not in this trust document.

This document only establishes that **erasure correctness is part of the runtime trust chain**.

---

# 12. SMT solvers

SMT solvers are automation engines.

They SHOULD NOT automatically define mathematical truth.

Preferred model:

```text
proof obligation
    ↓
SMT solver
    ↓
proof / certificate
    ↓
independent checker
    ↓
kernel
```

If a solver result must be trusted directly because independently checkable evidence is unavailable, that fact MUST appear in the trust report.

Example:

```text
Trusted automation:
  cvc5: yes
  Z3: no
```

The long-term direction SHOULD be to reduce direct solver trust where practical.

---

# 13. Tactics

Proof tactics SHOULD be treated as untrusted proof producers.

Examples:

```text
simplifier
rewriter
arithmetic tactic
induction tactic
proof search
```

A bug in a tactic should ideally produce:

```text
invalid proof
    ↓
kernel rejects
```

rather than:

```text
invalid proof
    ↓
false theorem accepted
```

---

# 14. AI systems

AI systems are never formal authorities.

AI-generated:

- code
- proofs
- specifications
- refactorings
- proof repairs
- explanations

must be treated as candidate input.

Conceptually:

```text
AI output
    ↓
C++L checker
    ↓
kernel
    ↓
accepted / rejected
```

An AI's confidence or explanation has no bearing on proof validity.

This separation is a core design requirement of C++L.

---

# 15. User-declared trusted assumptions

C++L may permit explicit assumptions that cannot or should not be proven internally.

These assumptions extend the TCB of the program using them.

A trusted assumption MUST be distinguishable from a proven proposition.

Tooling MUST NOT display both simply as:

```text
verified
```

without exposing the distinction.

---

# 16. No hidden axioms

Hidden axioms are forbidden.

Any mechanism that introduces logical truth without kernel-derived proof MUST be machine-visible.

This includes assumptions introduced by:

- foreign bindings
- compiler intrinsics
- solver shortcuts
- runtime contracts
- plugins
- platform APIs
- external specifications

If an assumption exists, the trust system MUST be capable of reporting it.

---

# 17. Trust propagation

Trust is transitive.

If:

```text
Law A
    ↓ depends on
Law B
```

and `Law B` depends on trusted assumption `X`, then `Law A` also depends on `X`.

Example:

```text
Law:
  payment_conservation

Status:
  PROVEN

Depends on trusted:
  bank_api_atomicity
```

A theorem being proven does not remove the assumptions from which it was derived.

---

# 18. Assumption closure

Tooling SHOULD compute the transitive assumption closure of every Law.

Conceptually:

```text
Law
    ↓
proof dependencies
    ↓
proof dependencies
    ↓
trusted assumptions
```

For any Law, users should eventually be able to ask:

```bash
cppl trust-report <law>
```

and see every trust dependency reachable from it.

---

# 19. Foreign code

Foreign code is not automatically verified.

Examples include:

- C libraries
- Objective-C++
- JNI
- N-API
- operating-system APIs
- device drivers
- GPU APIs
- inline assembly
- external native libraries

Foreign interfaces MUST have an explicit trust classification.

Possible classifications include:

```text
VERIFIED
TRUSTED
RUNTIME-CHECKED
UNSAFE
UNVERIFIED
```

The exact semantics of those classifications belong in `SPEC.md`.

This document requires that the classification remain visible.

---

# 20. Verified wrappers

An external implementation may be exposed through a formally specified wrapper.

Conceptually:

```text
external implementation
        ↓
formal contract
        ↓
verified wrapper
        ↓
verified C++L
```

A wrapper does not prove the external implementation correct.

Its contract defines the trust boundary unless the implementation itself has been independently verified.

---

# 21. Standard library trust

Using the C++ standard library does not imply that its implementation is formally verified.

C++L may provide formal specifications for library abstractions while relying on an external implementation.

Such a model introduces a relationship:

```text
formal specification
    ↕ assumed correspondence
runtime library implementation
```

Any required assumption MUST remain visible in the trust model.

---

# 22. Runtime validation

Runtime validation is not proof-kernel execution.

A dynamic check can establish a fact about a runtime value.

Example:

```text
external input
    ↓
runtime validation
    ↓
value admitted into verified domain
```

The correctness of that validation mechanism is part of the runtime trust chain.

Runtime validation sites SHOULD be reportable separately from purely static proofs.

---

# 23. Proof cache

Proof caching is trust-sensitive.

A stale proof must never remain accepted after a semantically relevant dependency changes.

Cache invalidation MUST account for all inputs relevant to proof validity.

Examples include:

- proposition
- implementation
- imported proofs
- imported Laws
- formal type definitions
- compiler semantics
- kernel version
- trusted assumptions
- relevant target semantics
- solver assumptions

Content-addressed proof artifacts are preferred.

---

# 24. Incremental verification

Incremental checking MAY reuse previous proof results only when semantic dependencies remain valid.

Performance optimization MUST NOT weaken proof dependency tracking.

If dependency validity cannot be established confidently, the proof MUST be recomputed.

---

# 25. Determinism

Kernel checking SHOULD be deterministic.

Given identical:

```text
formal input
kernel version
configuration
```

the kernel should produce the same accept/reject result.

Automation MAY use nondeterministic search internally, but accepted proof evidence must validate deterministically.

---

# 26. Reproducibility

Verification SHOULD be reproducible.

A verification artifact SHOULD eventually record enough information to recreate the result, including:

- C++L compiler version
- proof-kernel version
- target architecture
- selected C++ standard
- solver versions
- trusted assumptions
- verification configuration
- proof artifact hashes

Reproducibility is important for auditing old releases.

---

# 27. Build statuses

C++L tooling MUST distinguish different levels of assurance.

At minimum:

```text
PROVEN
TRUSTED
RUNTIME-CHECKED
UNSAFE
UNVERIFIED
UNRESOLVED
```

These states MUST NOT be silently collapsed into a generic:

```text
verified
```

The exact language semantics of each category belong in `SPEC.md`.

The trust layer is responsible for reporting them accurately.

---

# 28. Strict verification mode

The toolchain SHOULD eventually provide a strict mode.

Example:

```bash
cppl build --require-fully-verified
```

Such a mode may reject builds containing:

- trusted assumptions
- unsafe regions
- unverified FFI
- directly trusted solver results
- unresolved obligations

Strict policy is a tooling decision.

It does not change theorem semantics.

---

# 29. Trust report

C++L MUST eventually provide a machine-readable and human-readable trust report.

Example:

```text
C++L Trust Report

Laws:
  proven:                      241
  trusted:                       0
  unresolved:                    0

Boundaries:
  unsafe regions:                3
  runtime validation sites:      7
  unverified FFI:                0

Trusted components:
  external axioms:               0
  direct solver trust:           0

Kernel:
  version: ...

Compiler:
  version: ...

Target:
  arm64

C++ mode:
  c++23
```

---

# 30. Per-Law trust report

Users should eventually be able to inspect a single theorem.

Example:

```text
Law:
  settlement_closes

Status:
  PROVEN

Proof dependencies:
  arithmetic_conservation        PROVEN
  item_partition                 PROVEN
  payment_provider_contract      TRUSTED

Runtime checks:
  0

Unsafe dependencies:
  0

Trusted closure:
  payment_provider_contract
```

This is more useful than:

```text
✓ verified
```

---

# 31. Kernel versioning

Proof validity may depend on kernel semantics.

Proof artifacts SHOULD therefore identify the kernel version under which they were accepted.

A kernel change that alters proof semantics MUST invalidate incompatible cached proof artifacts.

---

# 32. Core calculus versioning

If the formal core calculus changes, proof artifacts MUST record the relevant calculus version.

A proof accepted under one calculus MUST NOT automatically be assumed valid under a semantically different one.

---

# 33. Mechanized meta-theory

The long-term goal SHOULD include mechanized reasoning about the C++L formal core.

This may establish properties such as:

- consistency
- substitution
- preservation
- normalization of proof-relevant fragments
- termination
- soundness of erasure

The specific mathematics belongs in `FOUNDATIONS.md`, `SPEC.md`, or dedicated formal-semantics documents.

Its relevance here is:

> Mechanization can reduce the amount of core semantics that must be trusted informally.

---

# 34. Compiler verification

A fully verified compiler is not required for C++L to provide value.

However, compiler trust SHOULD be reduced progressively.

Possible progression:

```text
Stage 1
small trusted kernel

Stage 2
mechanized kernel semantics

Stage 3
verified erasure

Stage 4
verified critical lowering passes

Stage 5
stronger end-to-end compiler refinement
```

Each stage reduces the gap between:

```text
source theorem
```

and:

```text
runtime behavior
```

---

# 35. Security-sensitive trust failures

A bug is security-sensitive if it causes C++L to:

- accept invalid proof evidence
- hide a trusted assumption
- incorrectly classify unsafe code as proven
- reuse an invalid cached proof
- accept malformed proof certificates
- remove required runtime checks during erasure
- misreport theorem trust status
- allow backend behavior to invalidate a claimed guarantee without disclosure

Such bugs belong under the policy in `SECURITY.md`.

---

# 36. TCB growth policy

Any change that enlarges the Trusted Computing Base SHOULD require explicit review.

A change from:

```text
independently checked
```

to:

```text
trusted
```

must be justified.

The preferred direction is always:

```text
trusted
    ↓
independently checked
```

not the reverse.

---

# 37. Target mature architecture

The intended mature trust architecture is:

```text
Human / AI source
        ↓
┌─────────────────────────────┐
│ C++L frontend               │
│ elaborator                  │
│ tactics                     │
│ SMT                         │
│ proof search                │
│ AI proof generation         │
└─────────────┬───────────────┘
              │
              │ proof evidence
              ▼
┌─────────────────────────────┐
│ Small trusted proof kernel  │
└─────────────┬───────────────┘
              │
              ▼
            PROVEN
              │
              ▼
┌─────────────────────────────┐
│ Verified / trusted erasure  │
└─────────────┬───────────────┘
              │
              ▼
        ordinary C++
              │
              ▼
        Clang / LLVM
              │
              ▼
      native executable
```

The upper trust chain answers:

```text
Is the proposition logically established?
```

The lower trust chain answers:

```text
Does the native executable preserve the semantics that were established?
```

Both are necessary.

---

# 38. Relationship to other documents

The documentation responsibilities are intentionally separated.

```text
SPEC.md
    what the language means

TRUST.md
    what must be trusted

FOUNDATIONS.md
    mathematical basis

DESIGN.md
    language/compiler design decisions

ARCHITECTURE.md
    implementation components and data flow

COMPATIBILITY.md
    C++ / ABI / toolchain compatibility

SECURITY.md
    vulnerability and disclosure policy

STATUS.md
    what actually exists today
```

This separation is intentional.

`TRUST.md` SHOULD reference these documents instead of duplicating their contents.

---

# 39. Fundamental question

For every Law reported as proven, C++L should eventually be able to answer:

> Why should I believe this?

The answer should reduce to:

```text
formal proposition
+
explicit assumptions
+
kernel-checked proof evidence
+
known trust boundary
+
sound connection to runtime execution
```

and never merely:

```text
because the compiler said so
```

---

# 40. Trust philosophy

C++L does not pursue zero trust.

C++L pursues **explicit, minimal trust**.

Over time:

```text
trusted core
    should shrink

visible assumptions
    should become more precise

automation
    should become more independently checked

runtime semantics
    should become more formally connected

verified C++ coverage
    should grow
```

But the meaning of:

```text
PROVEN
```

must never be weakened to make implementation easier.

---

# 41. Trusted Computing Base as implemented

This section states what must currently behave correctly for a C++L `PROVEN`
result to mean what it claims. It describes the implementation that exists, not
the target; `STATUS.md` records maturity.

## 41.1 Logical trust

For a false proposition to be accepted, one of these would have to be wrong:

```text
kernel/   the proof checker, the normalizer (including the polynomial normal
          form of machine arithmetic and the canonical comparisons), the type
          checker of core terms, the capture-safe substitution used by
          universal elimination, by equality substitution and by the hypothesis
          context, the admission rules of the definition context, and the
          linear-arithmetic constraint builder and certificate checker
```

That is the whole logical TCB. It links no other component, includes no header
outside itself, and holds no global state. `tests/architecture` checks both
properties on every run.

The core has nine rules:

```text
1. Reflexivity
2. Equality substitution
3. Universal introduction
4. Universal elimination
5. Implication introduction
6. Implication elimination
7. Hypothesis use
8. Conditional elimination
9. Linear arithmetic
```

Each is a capability, not an assumption.

**Machine arithmetic** (`SPEC.md` 7.1.1, 7.5; RFC 0006) enlarges the logical
TCB explicitly, and the kernel and core versions are 0.3.0. Two parts must be
right for a `PROVEN` result to mean what it says:

- *The polynomial normal form.* Wrapping addition, subtraction and
  multiplication are read as polynomials modulo `2^width` and rendered in one
  canonical form, and comparisons are rewritten only by identities of the
  machine type. An error here would make reflexivity accept an equality that
  some assignment falsifies. It is checked by a differential test that
  evaluates random terms and their normal forms with an evaluator written
  independently of the kernel, over every assignment of small types and at the
  edges of 64-bit ones.
- *The linear-arithmetic rule.* The kernel checks each fact's evidence, states
  the facts and the goal's negation as integer constraints itself — each
  wrapped value as its polynomial minus a fresh multiple of `2^width`, bounded
  by its type — and checks a certificate of Farkas sums, integer splits and
  case splits against them with overflow-checked 128-bit arithmetic. The
  producer supplies neither the translation nor any bound. An error in the
  translation would let a certificate refute a system that does not say what
  the machine does; negative tests cover wrapping at 32 and 64 bits, overflow,
  misapplied, one-sided and malformed certificates, and unchecked facts.

Certificates are found by Fourier-Motzkin elimination with case splitting in
`compiler/automation`, outside the TCB. The rule adds no axiom: it derives
nothing a model of machine integers does not satisfy.

Conditional elimination was added in the path slice. It combines checked cases into a
proposition about `select(condition, true_value, false_value)`. The kernel
type-checks the complete conditional and the proposition context, derives each
arm's required predicate from the condition, checks both implications, and
computes the resulting proposition by capture-safe substitution. Producers
cannot supply their own branch premises or omit a case. Comparisons are typed
total primitives; concrete evaluation respects integer width and signedness.
Boolean terms use unsigned one-bit integers, and a declared `bool` is modeled as
that type: its value set is the same, and every promotion out of it is a
conversion the bridge refuses. These computations and that rule enlarged the
logical TCB explicitly and moved the kernel/core versions to 0.2.0. Symbolic
order reasoning came later, with the linear-arithmetic rule above; no logical
assumption was added by either.

Equality substitution is a distinct logical
capability rather than sugar over the others: without it, evidence for `a = b`
can close a goal that already is `a = b` and can do nothing else. It transports
evidence through a proposition context, and **the kernel performs the
substitution itself**. The context is given to it as part of the proof term,
with its hole type-checked against the type the equality is stated at; the
kernel checks the equality, checks what is transported, and derives the result
by its own capture-safe substitution. The elaborator decides which occurrences
a context abstracts, and may not manufacture the resulting proposition and ask
the kernel to accept it. A mistaken choice of occurrences can therefore only
fail to prove something.

Symmetry needs no rule of its own: it is equality substitution at the context
`b = -`, whose transported case is `b = b`. Rewriting in the other direction is
derivable the same way, and is never inferred.

Eliminating a quantifier requires evidence for a proposition the kernel checks
for itself, an argument whose type the kernel derives for itself, and a
resulting proposition the kernel obtains by substituting for itself. The proof
term it is given restates the proposition being eliminated from, and that
restatement is checked, never believed: evidence for a false statement is
refused before any instance of it can be taken. Discharging a premise works the
same way — the implication is restated, checked, and the conclusion is the
kernel's own.

**A premise is supposed, never granted.** `expects(P) ensures(Q)` does not mean
that `P` is trusted; it means that `Q` is to be proved under the supposition
`P`, and what is established is `P -> Q`. The hypothesis exists for exactly as
long as the implication introduction that placed it in the context, the kernel
holds that context itself, and evidence can never name a premise that is not
standing in it. Nothing anywhere admits `P` on its own.

There are **no axioms**. The core has no rule that introduces a proposition
without evidence, and no `trusted` mechanism is implemented, so no proposition
can currently enter the system as an assumption. A declaration that would state
one, `trusted law`, is refused rather than accepted (`SPEC.md` 27, 52).

## 41.2 Correspondence trust

A kernel-checked proof is a proof about the proposition it was given. That the
proposition says what the C++ program means is a separate question, and the
components that answer it are trusted for that correspondence:

```text
compiler/frontend/      which spans are formal syntax and where they came from
clang/                  the resolved C++ semantics C++L reads from Clang
compiler/elaboration/   the VIR built from those semantics
compiler/obligations/   the lowering of VIR into core terms and propositions
```

Declaration linkage uses physical offsets in the analysis buffer. The projector
records the generated Law name tokens and maps retained pure/verified name
tokens; the Clang bridge obtains their offsets from libclang. Repeated presumed
file/line/column labels cannot select another declaration. Ambiguous generated
helper lookups fail closed, and a unit missing declaration obligations is
rejected. These checks harden the existing correspondence boundary; they add no
logical rule or trusted mechanism.

Written proof declarations are part of this layer and **do not enlarge the
logical TCB**. A proof statement is surface syntax that elaboration turns into a
kernel proof term; the kernel then checks that term against the goal exactly as
it checks any other. `exact` and `apply` reuse a term that was itself checked
against its own goal, and neither admits a proposition on the strength of the
author's word. A defect in this lowering can only produce a term the kernel
refuses, or a term for a goal that is not the one the Law states — and the
second is caught separately, because `Verdict::proven` compares the proposition
the kernel accepted with the proposition of the obligation being discharged.

A defect here cannot make the kernel accept an invalid derivation. It can make
the kernel check the wrong statement. The lowering rules that carry the most
weight are deliberately few and are stated explicitly in the implementation:

- a C++ equality between two built-in integer values of the same type denotes
  propositional equality of those values (`SPEC.md` 7.3);
- C++ `+`, `-` and `*` are lowered onto the core's wrapping primitives **only**
  for unsigned operands of one modeled type, where C++ arithmetic is modular
  and the two agree exactly. Operands narrower than `int` reach the bridge as a
  promotion to `int`, which it refuses as a conversion. Signed arithmetic is
  refused, because C++ leaves its overflow undefined;
- specification expressions may unfold admitted `pure` definitions whose
  bodies satisfy the purity rules; verified-function definitions additionally
  support the kernel-checked link between a proven contract and its actual body;
- a `proves` clause names a Law at arguments, and the proposition it claims is
  that Law's proposition instantiated at them and closed over the proof's own
  parameters. The claim is a statement to be proved, never a licence: a proof
  discharges the Law itself only when the two propositions coincide, and a
  proof of one instance discharges nothing;
- the terms a proof reference is instantiated at are ordinary C++ expressions,
  resolved by Clang from the projected text like every other expression, and
  lowered by the same rules as any other value;
- a Law's `expects` clause is its premise and its `ensures` clause its
  conclusion, and the Law is the implication from the one to the other, under
  its parameters. A precondition asserts nothing on its own, and a Law that
  states one is proven only when that implication is;
- `assume h : P;` names a premise the goal already supposes. It introduces
  nothing: the proposition written there is compared with the goal's own
  premise, and the hypothesis the kernel then holds is the goal's premise, not
  the written text. A goal that supposes no premise has none to name, and the
  statement is refused there;
- `rewrite e;` chooses which occurrences of a term the goal's context
  abstracts. That choice is this layer's, and it is all this layer does: the
  context goes to the kernel, which checks the equality, checks what is
  transported through it, and derives the resulting proposition itself.

Anything outside those rules is reported as unsupported and yields no
obligation. No construct is approximated.

A Law that an author wrote a proof for is never closed by the compiler's own
strategy if that proof was refused, nor if the proofs that name it establish
only instances of it. Writing a proof narrows how a Law may be established; it
can never widen it.

Every written proof reaches the kernel. The one that discharges a Law is
submitted as that Law's evidence and checked there; a proof of an instance has
no obligation of its own and is checked against its own claim where it is
lowered. Neither is left standing on the author's word.

A premise an `apply` leaves behind is a goal like any other. It is closed by the
statements that follow, by evidence the kernel checks; a body that ends with one
still open is refused, and no strategy of the compiler's own is offered for it.

Single-return verified-function contracts add no kernel rule, axiom, or logical authority.
The correspondence layer now substitutes the actual elaborated return term
for the postcondition's specification-only `result` binder and closes the goal
over the parameters and optional precondition. A defect in body selection or
substitution could state the wrong obligation; body-change, parameter-capture,
unsupported-body, and erasure regressions exercise this boundary.

The body must lower even when its result is absent from the postcondition.
Every verified call must discharge its instantiated precondition, even when its
result is ignored. Abstract caller reasoning binds fresh call results and uses
only the caller's premise and preceding, proven callee postconditions. A callee
cannot justify its own precondition, and a weak summary cannot be strengthened
by inspecting its body. The same restriction applies to `verified pure` calls.

Core definitions lower the actual verified bodies and are used to connect each
callee's body-derived proof to its exported call theorem. Universal elimination,
implication elimination, and equality elimination compose those proofs; the
kernel checks the resulting proof against the caller's original obligation.
The definitions add executable meanings, not assumed postconditions. Definitions
with preconditions remain unavailable for unrestricted use in specifications.

Call collection, argument substitution, and summary selection are correspondence
responsibilities. Regressions cover nested calls, failed and irrelevant-result
preconditions, weak contracts, overloads, capture, cycles, forged summaries, and
detached body linkage. Automatic premise rewriting still produces ordinary
proof terms; forged hypotheses remain kernel rejections. Function contracts and
call preconditions have separate counts from Laws. This slice adds zero kernel
rules, zero logical assumptions, and zero runtime checks.

Locals and assignments add no kernel rule, no logical assumption, and no
runtime check. A local is not a new kind of value: each write is a logical
version, and a read lowers to the term that version was given, so the kernel
sees the same goals it saw before and decides them the same way. What this
slice trusts is the bridge's account of the body: which declaration each read
resolves to, which version is current there, and where each call is evaluated.
A defect there can misstate the program, but it cannot grant the kernel a
proposition. Anchoring is what keeps a local from moving a call: a call bound
to a local is proven where the body makes it, under the conditions in force
there, and on every path that reaches it. The kernel and core versions do not
change, because the accepted calculus does not. Every version's value is
lowered where it is established, read or not, so a value the core cannot
state — an unread signed overflow, for instance — rejects the body instead of
vanishing from the model.

Path-sensitive verification additionally trusts the Clang bridge and lowering
to preserve every branch, fallthrough edge, condition polarity, and return.
Each return has its own implication goal. Calls in guards are checked before
their guard evidence becomes available; calls on other paths supply no evidence.
The kernel's conditional-elimination rule then checks the assembled body proof
before its call theorem can be exported. A frontend defect can misstate the
program but cannot grant a path proposition to the kernel. Kernel adversarial
tests cover altered conditions, false/missing arms, malformed types, and motive
capture; compiler regressions cover path leakage and failed branch dependencies.
There are zero new logical assumptions and zero inserted runtime checks.

## 41.3 Runtime trust

Each compiler invocation owns a fresh temporary directory until native compilation
finishes. Concurrent invocations cannot overwrite another invocation's analyzed
or emitted program. Temporary paths do not enter obligation identities.

```text
Clang / LLVM    preprocessing, C++ semantics, code generation, linking
compiler/erasure and the projector    that the program verified is the program compiled
```

Clang is trusted to implement C++ and to compile the program it is given; that
trust is the same trust any C++ project places in its compiler, and it is not
proof trust. Clang never decides whether a theorem holds.

The projector produces the analysed text and the runtime text in one pass, and
the erasure check verifies that the runtime text differs only by blanking inside
recorded formal spans, with line numbering unchanged. That check is what
connects the verified program to the compiled one; it runs on every unit that
contains C++L syntax, and a failure is an internal error, never a verification
result.

## 41.4 Not yet trusted, because not yet present

```text
solvers                 none are integrated; none are trusted
proof caches            no proof result is stored or reused
proof artifacts         no serialized proof format exists
AI systems              no privileged path exists
FFI contracts           none can be declared
```

A trust report therefore shows zero trusted solvers and zero trusted external
axioms, and says so because it is true, not because the fields are unfilled.
Unverified FFI boundaries are reported as *not analysed* rather than as zero:
C++L does not yet look for them.

## 41.5 What would enlarge the TCB

Each of these requires an explicit update to this document before it is merged:

- admitting recursive definitions (the termination argument in
  `ARCHITECTURE.md` 97.7 would no longer hold);
- any axiom, `trusted` declaration or assumed contract;
- trusting a solver result that is not independently checked;
- reusing a cached proof result;
- any lowering rule that equates a C++ operation with a core primitive whose
  behaviour differs on some input.
