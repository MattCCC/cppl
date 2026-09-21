# AGENTS.md

This file defines mandatory rules for AI agents and automated coding tools working on C++L.

These rules are repository invariants.

They apply to:

- code generation
- refactoring
- bug fixing
- tests
- compiler changes
- proof-system changes
- language changes
- architecture changes
- documentation changes
- dependency changes
- performance work
- automated migrations

When these rules conflict with convenience, speed, local test success, or milestone pressure, these rules win.

---

# 1. Project mission

C++L is a source-compatible C++ superset for expressing formal intent as machine-checkable Laws, proving that implementations satisfy those Laws, and erasing proof-only information before ordinary native compilation through Clang/LLVM.

Existing valid supported C++ should compile under C++L with zero source changes unless the user explicitly opts into verification features.

The target pipeline is:

```text
formal intent
    ↓
C++L source
    ↓
formal elaboration
    ↓
verification IR
    ↓
proof obligations
    ↓
automation / explicit proofs
    ↓
small trusted proof kernel
    ↓
PROVEN
    ↓
proof erasure
    ↓
ordinary C++
    ↓
Clang / LLVM
    ↓
native binary
```

Do not introduce architecture that undermines this model.

---

# 2. C++ superset invariant

C++L is a genuine C++ superset.

```text
C++ ⊂ C++L
```

Supported ordinary C++ must remain valid C++L.

Mandatory rules:

- C++L-specific words should be contextual, not globally reserved, unless `docs/SPEC.md` explicitly requires otherwise.
- Existing C++ keywords take precedence.
- Do not repurpose existing C++ keywords for unrelated C++L semantics.
- Do not reinterpret valid supported C++ syntax.
- Do not create a second incompatible C++ template system.
- Do not create a new mandatory native ABI without explicit architectural approval.
- Prefer Clang as the authority for ordinary C++ semantics.

Before adding syntax, check:

- current C++ keywords
- future/draft C++ keywords where known
- declaration ambiguity
- template ambiguity
- macro interaction
- module interaction

Preserve:

```text
valid supported C++
    remains
valid C++L
```

---

# 3. Laws are specifications

A `law` is formal intent.

A Law is not:

- a test
- a fixture expectation
- documentation
- an assertion
- a heuristic
- a Boolean helper
- a solver hint

Never weaken, remove, bypass, or reinterpret a Law merely because an implementation cannot prove it.

If implementation and Law disagree:

```text
1. determine intended semantics
2. determine whether implementation is wrong
3. determine whether Law is wrong
4. fix the incorrect layer
```

Never change theorem meaning merely to make tests pass.

Tests encode semantics.

They do not define semantics.

Fixture majority does not define language behavior.

---

# 4. Verification status invariant

C++L must preserve these distinctions:

```text
PROVEN
TRUSTED
RUNTIME-CHECKED
UNSAFE
UNVERIFIED
UNRESOLVED
```

Never silently convert:

```text
UNRESOLVED → PROVEN
TRUSTED    → PROVEN
UNSAFE     → PROVEN
UNVERIFIED → PROVEN
```

Never treat:

```text
proof failure
```

as:

```text
warning
runtime assertion
automatic trust
implicit skip
successful verification
```

`PROVEN` must mean proven according to the current formal system.

---

# 5. Single proof authority

There must be one final authority for theorem validity:

```text
the trusted proof kernel
```

Not:

```text
kernel
OR solver
OR frontend
OR AI
OR runtime assertion
OR passing test
```

Automation may propose evidence.

Only the kernel decides whether that evidence is valid.

This principle applies to:

- SMT solvers
- tactics
- proof search
- simplifiers
- rewriters
- AI-generated proofs
- frontend elaboration

Preferred model:

```text
complex producer
    ↓
candidate proof / certificate
    ↓
small trusted checker
    ↓
accept / reject
```

---

# 6. Trusted Computing Base invariant

The Trusted Computing Base must remain as small as practical.

Any change that enlarges the TCB must:

1. be explicit;
2. update `TRUST.md`;
3. include rationale;
4. include negative tests;
5. be called out in review.

Preferred direction:

```text
trusted
    ↓
independently checked
```

not the reverse.

No component may silently introduce logical truth.

Any axiom or trusted assumption must be explicit and machine-visible.

---

# 7. Proof kernel rules

Kernel code must prioritize:

- simplicity
- determinism
- explicitness
- auditability
- exhaustive handling
- dependency minimization
- negative testing

Do not add to the kernel:

- hidden global state
- heuristic acceptance
- parser recovery
- solver-specific shortcuts
- unrelated compiler functionality
- approximate equality
- silent fallbacks
- complex framework machinery

Malformed proof input must fail closed.

Unknown proof forms must fail closed.

Ambiguous proof states must not be guessed.

Inside the kernel, boring code is preferred over clever code.

---

# 8. Soundness invariant

Nothing may allow false propositions to become accepted proof evidence.

In particular, protect against:

- proof forgery
- nontermination
- hidden axioms
- unsafe memory
- stale proof caches
- unchecked solver results
- malformed proof artifacts
- invalid equality
- invalid substitution
- invalid induction
- unsound normalization

The following must remain impossible:

```text
Proof<False>
```

without a contradiction already present in explicit trusted assumptions.

Soundness bugs are security bugs.

Follow `SECURITY.md`.

---

# 9. Termination invariant

Proof-producing computation must not derive evidence through divergence.

Never permit:

```text
nonterminating computation
    ↓
arbitrary proof
```

Any change affecting:

- recursion
- normalization
- proof evaluation
- induction
- well-founded recursion
- termination measures

requires soundness review.

Termination-checker bypasses are security-sensitive.

---

# 10. Equality invariant

Do not blur:

```text
definitional equality
```

and:

```text
propositional equality
```

Definitional equality must follow the specified normalization/conversion rules.

Do not make equality more permissive merely to discharge failing proofs.

Never silently treat:

- textual equality
- AST-shape similarity
- approximation
- solver convenience
- heuristic equivalence

as logical equality.

Changes to equality semantics require:

- `docs/SPEC.md` update
- RFC when substantial
- positive tests
- negative tests
- soundness review

---

# 11. Runtime semantics must match proof semantics

A proof about executable code is meaningful only if the proof model corresponds to actual runtime behavior.

Do not prove using idealized semantics and emit materially different C++ semantics.

Pay particular attention to:

- integer overflow
- signedness
- conversions
- shifts
- floating point
- object lifetime
- pointer provenance
- references
- aliasing
- mutation
- moves
- destruction
- exceptions
- atomics
- data races
- FFI

If behavior is not soundly modeled:

```text
reject
or
UNSAFE
or
UNVERIFIED
or
explicitly TRUSTED
```

Do not guess.

---

# 12. Undefined behavior and memory invariants

Verified C++L must not silently rely on undefined behavior.

Relevant classes include:

- signed overflow
- invalid shifts
- division by zero
- out-of-bounds access
- null dereference
- invalid pointer arithmetic
- use-after-free
- lifetime violations
- invalid references
- invalid casts
- uninitialized reads
- aliasing violations
- data races

Do not introduce a simplified memory model that contradicts C++ object semantics.

Remember:

```text
pointer != integer
reference != non-null pointer
move != copy
lifetime != optional metadata
destruction != irrelevant cleanup
```

Never claim memory guarantees beyond the current verified model.

---

# 13. Machine arithmetic invariant

Never silently switch between:

```text
mathematical integer
```

and:

```text
machine integer
```

Proof semantics must match execution semantics.

Overflow policy must be explicit.

Bitvectors and unbounded integers are not interchangeable.

Floating-point reasoning must also remain explicit.

Do not reason about IEEE floating point as real-number arithmetic unless formally justified.

---

# 14. Clang integration invariant

Do not unnecessarily reimplement C++ semantics.

Prefer Clang for:

- parsing ordinary C++
- name lookup
- overload resolution
- template instantiation
- standard conversions
- declarations
- constexpr behavior
- source locations
- ABI-relevant information

Default architecture:

```text
C++L extension layer
+
Clang semantic information
+
C++L verification pipeline
```

Do not fork Clang merely because integration is inconvenient.

A Clang fork requires explicit architectural justification.

---

# 15. Verification IR invariant

The Verification IR must separate:

```text
surface syntax
```

from:

```text
formal meaning
```

The VIR should be:

- deterministic
- typed
- explicit
- serializable
- hashable
- independent of irrelevant syntax
- suitable for proof generation
- suitable for caching
- suitable for diagnostics

Do not leak arbitrary frontend representation details into the logical core.

Avoid duplicated semantic implementations.

There should be one authoritative logical representation.

---

# 16. Proof erasure invariant

Proof-only information must normally have zero runtime cost.

Erasure may remove:

- proofs
- ghost values
- theorem-only values
- compile-time-only indices
- proof-search artifacts

Erasure must not remove:

- required runtime validation
- runtime data
- observable effects
- ABI-relevant data
- safety checks needed for dynamic input

Required invariant:

```text
runtime behavior before erasure
=
runtime behavior after erasure
```

for the executable semantics being modeled.

Any erasure change requires dedicated regression tests.

Do not solve compile-time theorem problems by introducing a mandatory theorem runtime.

---

# 17. Runtime validation invariant

Dynamic validation is allowed when values cannot be known statically.

Example:

```text
external input
    ↓
runtime validation
    ↓
verified refined value
```

Do not represent this as compile-time proof.

Preserve its status as:

```text
RUNTIME-CHECKED
```

where relevant.

Runtime validation is not a theorem runtime.

---

# 18. Unsafe, trusted, and FFI boundaries

Foreign and unsafe code is not automatically verified.

This includes:

- C
- Objective-C++
- JNI
- N-API
- OS APIs
- inline assembly
- device drivers
- GPU APIs
- external native libraries

Verified code may rely on such code only through:

- verified specification
- runtime validation
- explicit trusted contract
- explicit unsafe boundary

Unsafe code must not manufacture proof evidence.

Trusted assumptions must remain visible transitively through every dependent Law.

---

# 19. Automation invariant

SMT solvers, tactics, search, rewriters, simplifiers, and AI are proof producers.

They are not automatically trusted authorities.

Do not treat:

```text
solver returned SAT/UNSAT
```

as:

```text
kernel-checked theorem
```

unless `TRUST.md` explicitly says that solver is trusted.

Counterexamples may disprove universal claims.

Failure to find a counterexample does not prove a theorem.

Never implement:

```text
no counterexample found
→
PROVEN
```

---

# 20. AI invariant

AI-generated:

- code
- proofs
- Laws
- refactors
- architecture
- documentation
- migrations

must be independently checked.

An AI's explanation, confidence, or chain of reasoning has no formal authority.

This applies to all agents modifying this repository.

Never treat your own reasoning as proof.

---

# 21. Caching and determinism invariant

Verification must be reproducible and cache-safe.

Do not introduce proof results dependent on:

- hash-map iteration order
- filesystem ordering
- unstable addresses
- thread timing
- wall-clock time
- unrecorded random solver seeds

Cached proof results must be invalidated by every semantic dependency, including where relevant:

- theorem statement
- implementation
- imported Laws
- imported proofs
- type definitions
- formal-core version
- kernel version
- solver trust mode
- target semantics
- C++ standard mode

Prefer content-addressed proof artifacts.

Stale proof acceptance is a soundness bug.

---

# 22. Provenance invariant

Formal facts should retain provenance such as:

```text
definitionally derived
kernel-proven
solver-derived
runtime-validated
trusted
foreign
unsafe
```

Do not flatten provenance prematurely.

Trust reports, diagnostics, and assumption closure depend on it.

---

# 23. Fail-closed invariant

When verification encounters:

- unsupported semantics
- unknown proof construct
- corrupt cache
- malformed artifact
- unknown artifact version
- solver failure
- incomplete semantics
- internal inconsistency

default to:

```text
reject
or
UNRESOLVED
```

Never:

```text
assume valid
```

---

# 24. Tests are not proofs

Tests are valuable for:

- implementation correctness
- regression prevention
- compatibility
- performance
- integration

But passing tests do not establish universal Laws.

Assertions are not proofs.

Fixtures are not proofs.

No test result may be promoted directly to `PROVEN`.

For proof features, both positive and negative tests are mandatory.

Every soundness regression must receive a permanent regression test.

---

# 25. Concurrency and exceptions

Do not apply sequential proof rules blindly to concurrent code.

Until concurrency semantics are implemented:

```text
reject
isolate
or mark UNSAFE/TRUSTED
```

Likewise, do not assume every function returns normally.

Exception semantics must eventually distinguish:

- normal return
- exceptional exit
- non-returning behavior

Unsupported exception behavior must not receive invented proof semantics.

---

# 26. No soundness tradeoffs for performance

Verification performance matters.

Soundness matters more.

Never introduce:

- unsound fast paths
- approximate proof checking
- incomplete cache invalidation
- solver shortcuts that bypass the kernel
- unsafe semantic simplifications

Prefer:

```text
slower and sound
```

over:

```text
faster and occasionally wrong
```

---

# 27. No permanent architecture debt

Temporary prototype bridges may exist only when explicitly labeled.

A temporary bridge must:

- be isolated
- be documented
- have a replacement path
- not become the semantic source of truth
- not expand silently into permanent architecture

Prefer production-grade architecture when feasible.

Do not duplicate proof semantics across languages or components unless one side is generated or independently checked.

---

# 28. Dependency invariant

Every dependency must justify:

- necessity
- maintenance quality
- license compatibility
- security impact
- determinism impact
- build impact
- TCB impact

Dependencies inside the proof kernel should be especially rare.

Do not add a large framework to save a small amount of trusted code.

---

# 29. Serialization invariant

Any serialized proof/core format must be versioned.

Deserialization must treat input as untrusted.

Malformed or unknown proof data must fail closed.

Do not manually patch generated proof artifacts.

Fix the generator or source of truth.

---

# 30. Finding the rules that apply

`docs/SPEC.md` is the single canonical specification. It is not split, and
nothing summarizes away its authority.

Do not read it end to end to implement a feature. Normative statements carry
stable `[FAMILY-NNN]` anchors, and `docs/agent/` indexes them:

```text
docs/agent/FEATURE_INDEX.md           feature -> rules and documents
docs/agent/IMPLEMENTATION_MAP.md      rules -> components and required behavior
docs/agent/INVARIANTS.md              what holds for every feature
docs/agent/TEST_MATRIX.md             rules -> required tests
docs/agent/VERIFICATION_CHECKLIST.md  the completion contract
docs/agent/features/*.yaml            machine-readable manifests
```

Before implementing a feature:

```text
1. locate the feature in docs/agent/FEATURE_INDEX.md
2. extract its rules:
       cppl-spec-rules extract --feature <name>
3. read docs/agent/INVARIANTS.md
4. read the feature's entry in docs/agent/IMPLEMENTATION_MAP.md
5. inspect existing implementation and tests
6. implement the complete rule set, not the motivating example
7. add positive, negative, interaction, adversarial and erasure tests
8. satisfy docs/agent/VERIFICATION_CHECKLIST.md
9. update docs/STATUS.md only after the implementation passes
```

Cite rule IDs, not section numbers. Section and line numbers move; rule IDs do
not.

```cpp
// SPEC: REFINE-010
TEST(RefinementWrite, RejectsUnprovenReplacement) { ... }
```

A feature is not complete because its syntax parses. Recognizing `decreases` is
not termination checking. The completion contract in
`docs/agent/VERIFICATION_CHECKLIST.md` defines DONE.

`docs/STATUS.md` records what is implemented. It MUST NOT be used to weaken a
requirement in `docs/SPEC.md`.

---

# 31. Documentation ownership

Use each document for one purpose:

```text
README.md
    mission and overview

docs/SPEC.md
    normative language semantics

docs/GRAMMAR.md
    normative concrete grammar referenced by docs/SPEC.md

FOUNDATIONS.md
    mathematical foundations

docs/DESIGN.md
    design rationale

ARCHITECTURE.md
    implementation components and data flow

TRUST.md
    TCB and trust boundaries

COMPATIBILITY.md
    C++ / ABI / ecosystem compatibility

ROADMAP.md
    implementation sequence

STATUS.md
    implementation maturity

docs/DEVELOPER_GUIDE.md
    how to work in the language and extend the compiler

tools/<tool>/README.md
    how to build, run and test one tool, and its own non-goals

SECURITY.md
    soundness/security policy

ACKNOWLEDGEMENTS.md
    intellectual and project credit

docs/agent/
    agent execution layer: indexes docs/SPEC.md, never overrides it

docs/rfcs/
    substantial language changes
```

Do not duplicate normative semantics unnecessarily.

Reference the authoritative document.

---

# 32. Mandatory documentation updates

Update `docs/SPEC.md` when changing:

- language meaning
- proof rules
- type rules
- equality
- normalization
- termination
- contracts
- unsafe semantics
- erasure semantics

Update `TRUST.md` when changing:

- TCB size
- solver trust
- FFI trust
- backend trust
- kernel trust
- erasure trust
- trusted assumptions
- cache trust

Update `docs/ARCHITECTURE.md` when changing:

- compiler stages
- component boundaries
- major data flows
- IR boundaries
- solver architecture
- cache architecture
- frontend/backend relationships

Update `STATUS.md` when implementation maturity changes.

Update `docs/agent/` when adding a feature or a normative rule:

- assign IDs to new normative statements: `cppl-spec-rules assign`
- add or update the feature in `docs/agent/FEATURE_INDEX.md`
- add or update `docs/agent/features/<name>.yaml`
- add its components to `docs/agent/IMPLEMENTATION_MAP.md`
- add its required cases to `docs/agent/TEST_MATRIX.md`

`cppl-spec-rules check` must pass: no duplicate IDs, no citations to rules that
do not exist.

---

# 33. RFC requirement

Use an RFC for substantial changes involving:

- syntax
- proof rules
- core calculus
- type system
- equality
- normalization
- termination
- memory model
- unsafe model
- trusted model
- erasure
- ABI
- concurrency

Do not introduce foundational semantics in incidental implementation patches.

---

# 34. Status promotion rules

Do not mark a feature `IMPLEMENTED` because:

- one example works
- one test passes
- Clang accepts output
- an AI generated working code
- a solver returned success

Promote status only according to `STATUS.md`.

In particular:

```text
IMPLEMENTED != VERIFIED
```

and:

```text
tests pass != proof of soundness
```

---

# 35. Diagnostics invariant

Verification failures should explain:

```text
goal
context
known facts
source location
failed obligation
relevant assumptions
counterexample when available
```

Avoid:

```text
verification failed
```

when useful structured information exists.

Diagnostics must preserve proof/trust provenance.

---

# 36. Implementation style

Prefer:

- modern C++
- strong types
- RAII
- explicit ownership
- exhaustive enums
- immutable IR where practical
- deterministic algorithms
- narrow interfaces
- structured errors
- explicit provenance
- small trusted modules
- short commits type: title
- All commits MUST use exactly one line.

Avoid:

- stringly typed proof logic
- magic integer tags
- hidden mutable globals
- implicit fallbacks
- unchecked casts
- duplicated sources of truth
- exception-driven normal proof flow
- clever kernel abstractions

---

# 37. Required review checklist

Before completing a significant change, verify:

```text
Does this change language meaning?
Does docs/SPEC.md need updating?

Does this change the TCB?
Does TRUST.md need updating?

Can this make a false theorem pass?
Can nontermination exploit this?
Can unsafe code exploit this?

Does runtime C++ still match the proof model?
Does erasure preserve behavior?

Does this affect C++ compatibility?
Does ARCHITECTURE.md need updating?
Does STATUS.md need updating?

Are negative tests included?
Are soundness regressions covered?
```

If any answer is uncertain, investigate before merging.

---

# 38. Core adversarial invariants

The repository should eventually maintain permanent tests proving that:

```text
False is not inhabitable

1 == 2 cannot be proven

proof objects cannot be forged

nontermination cannot prove arbitrary propositions

unsafe memory cannot manufacture proof evidence

trusted assumptions remain visible

ghost state cannot affect runtime behavior

erasure preserves runtime behavior

stale caches cannot preserve invalid proofs

unsupported C++ cannot silently become verified

solver failure cannot become proof success

AI output cannot bypass the kernel
```

These invariants matter more than superficial feature count.

---

# 39. Proof decomposition invariants

Proof decomposition is representation-independent. These invariants exist
because each of them, if broken, reintroduces a class of bug the generic design
was chosen to eliminate.

```text
one engine, many providers

    There is exactly one case engine and exactly one product-decomposition
    path. Never add a per-representation case engine, parser, arm grammar,
    exhaustiveness rule or diagnostic path. A provider answers only: which
    states exist, what condition holds in each, which case a label denotes.

no per-representation kernel rule

    No VariantRule, OptionalRule, ExpectedRule, PointerRule, TupleRule or any
    successor. A representation whose states are decidable conditions on
    modeled values needs no rule: conditional elimination already covers it.
    A representation that seems to need one is a design error to escalate,
    not a rule to add.

the provider never supplies the residual discriminator

    The engine derives it by negating the others. A provider that could state
    the residual condition could widen it and silently absorb a state.

no wildcard, no implicit catch-all

    There is no `_` arm and no internal equivalent. Every state has an arm or
    is discharged by the ordinary proof system. A newly added enumerator,
    alternative or component must break a previously exhaustive proof.

a binding is never a new object

    Bindings are aliases or logical projections onto the existing object. No
    copy, move, conversion, temporary, structured binding or default
    construction may be introduced for a proof binder. Section 11 applies.

a provider states states, nothing more

    A pointer provider states null and non-null and never lifetime,
    provenance, dereferenceability, bounds, initialization, ownership,
    uniqueness or dynamic type. A standard-library provider models public
    semantics and never a library's layout. Section 12 applies.

semantic identity, never spelling

    Recognize a type through its Clang-resolved canonical identity after
    substitution. A user type spelled like a standard one is not that type,
    and a standard type reached through an alias or dependent name is.

zero runtime behavior

    Decomposition is proof-only. Erasure tests belong to every provider, not
    only to the first one. Section 16 applies.

facts do not outlive what they describe

    Case facts are flow-sensitive. Today they cannot go stale because proof
    bodies contain no mutation. If decomposition is ever admitted over values
    that can change, its facts must participate in the same mutation and alias
    invalidation framework as every other proof fact. A provider must never be
    given an invalidation mechanism of its own.

TRUST.md states what is not inferred

    Every provider carries a TRUST.md 41.6 correspondence block naming its
    states, how exhaustiveness is derived, and what it does not infer. Report
    kernel-rule, axiom, assumption and TCB deltas accurately; do not claim
    zero TCB delta when the value model itself expanded.
```

---

# Refinement storage invariants

- Every write to refined storage, including a verified call effect, must use the
  common refinement-crossing predicate machinery.
- A refinement fact belongs to a logical value version, never to a variable name.
- Possible mutation through an alias invalidates dependent observations unless
  evidence establishes the new value's predicate. Const references are not a
  global immutability guarantee.
- Ordinary C++ binding, type identity, access and conversion semantics come from
  Clang. Storage and logical values are separate concepts.
- No cast automatically preserves semantic refinement information, and no
  reference or pointer operation manufactures proof.
- No syntax-specific refinement subsystem may bypass shared read/write/effect
  infrastructure. New writes must also participate in loop mutation discovery.
- A callee's post-state is usable only after its own contract and the call's entry
  obligations are proven. Repeated actual aliases share a post-state version.

---

# Storage and memory invariants

These govern the generic storage model (`docs/SPEC.md` 12.10, RFC 0014). It is
generic on purpose: refinement types consume it and must never define it.

- Storage is modeled as places, regions, capabilities and versions. A place is
  never a value, never an address, and never reaches the kernel as a term. A
  refinement fact belongs to a version of a place, never to a source name.
- Every read resolves a place to its current version, provenance and valid
  facts through one shared mechanism. Every write goes through one shared path:
  prove the target writable, prove the value satisfies the target storage's
  refinement, establish a new version, invalidate what may alias it. No access
  form gets its own read or write.
- Non-nullness does not imply dereference validity. Nothing about a pointer's
  value, and nothing a decomposition provider states, may establish a
  capability. A provider states states and never lifetime, provenance,
  dereferenceability, bounds, initialization, ownership or uniqueness.
- Disjointness is proved, never assumed, and only from Clang-resolved
  distinctness. Type-based aliasing must not be used to justify a proof: it
  presupposes the undefined-behavior freedom the proof has not established.
  Where distinctness is unproved, invalidate. False rejection is preferable to
  a stale unsound fact.
- A capability is established by a proven obligation or by an explicit,
  recorded `trusted` boundary, and by nothing else. A failed capability
  obligation is a diagnostic, never a silent downgrade to an assumption.
- An unverified call is never assumed pure. A fact invalidated by an effect is
  re-established only by a proven postcondition, never by a summary asserted
  without proof and never by refinement spelling alone.
- No cast automatically preserves semantic refinement, and no reference,
  pointer, subscript or member operation manufactures proof.
- Storage, capabilities and versions are proof-only. They introduce no runtime
  check, tag, metadata, wrapper type, temporary or layout change.
- Capability tracking lives in the correspondence layer and carries a stated
  TCB delta (`TRUST.md` 41.2). Do not report it as zero, and do not move it
  into the kernel: it is a decidable flow analysis, and the kernel would grow
  without checking more.

---

# Path and value-provenance invariants

- A path-sensitive value must preserve its branch provenance through local
  bindings and through later logical-value resolution. Binding a conditional to
  a local must not collapse it into one opaque term when a later obligation
  needs the facts of the arm a route takes.
- A read of a logical version must not discard proof-relevant provenance. A read
  denotes the value its version was given, resolved transitively.
- Transitive logical-version resolution must be generic, never a fixed number of
  hops. It must respect version boundaries, so a version established after a
  mutation is never confused with the one before it, and it must terminate: a
  version's value reads only versions established before it.
- Condition elaboration must model C++ short-circuit semantics exactly. `&&`,
  `||` and `!` in a verified condition are elaborated into the routes they
  select between, recursively, never rewritten as Boolean values or flattened.
- No Boolean condition handler may introduce a fact from an operand that is not
  guaranteed to have executed on that route. The route where `A && B` fails is
  the union of `!A` and `A && !B`; it must never be represented as one route
  supposing both operands false. The route where `A || B` holds is likewise a
  union and establishes neither side on its own.
- Route splitting and proof composition derive branch structure separately and
  must agree. A route's conditions correspond to the `select` nesting of the
  body's lowered value, because that nesting is what the proof is composed over.
- Added proof power must never add a fact. Every new route split requires
  adversarial cases showing that a failing arm still rejects.

---

# 40. Final invariant

For every Law reported as `PROVEN`, the project must be able to answer:

> Why should this Law be believed?

with:

```text
because its formal proposition
was proven from explicit assumptions
using machine-checkable evidence
accepted by the trusted proof kernel
under semantics that correspond to the resulting executable
```

Never with:

```text
because the agent said so
because the solver said so
because the test passed
because the compiler needed it to be true
```

**C++L is C++ with Laws. The Laws are the specification; the implementation must satisfy them.**

<!-- code-review-graph MCP tools -->

## MCP Tools: code-review-graph

**IMPORTANT: This project has a knowledge graph. ALWAYS use the
code-review-graph MCP tools BEFORE using Grep/Glob/Read to explore
the codebase.** The graph is faster, cheaper (fewer tokens), and gives
you structural context (callers, dependents, test coverage) that file
scanning cannot.

### When to use graph tools FIRST

- **Exploring code**: `semantic_search_nodes_tool` or `query_graph_tool` instead of Grep
- **Understanding impact**: `get_impact_radius_tool` instead of manually tracing imports
- **Code review**: `detect_changes_tool` + `get_review_context_tool` instead of reading entire files
- **Finding relationships**: `query_graph_tool` with callers_of/callees_of/imports_of/tests_for
- **Architecture questions**: `get_architecture_overview_tool` + `list_communities_tool`

Fall back to Grep/Glob/Read **only** when the graph doesn't cover what you need.

### Key Tools

| Tool                             | Use when                                               |
| -------------------------------- | ------------------------------------------------------ |
| `detect_changes_tool`            | Reviewing code changes - gives risk-scored analysis    |
| `get_review_context_tool`        | Need source snippets for review - token-efficient      |
| `get_impact_radius_tool`         | Understanding blast radius of a change                 |
| `get_affected_flows_tool`        | Finding which execution paths are impacted             |
| `query_graph_tool`               | Tracing callers, callees, imports, tests, dependencies |
| `semantic_search_nodes_tool`     | Finding functions/classes by name or keyword           |
| `get_architecture_overview_tool` | Understanding high-level codebase structure            |
| `refactor_tool`                  | Planning renames, finding dead code                    |

### Workflow

1. The graph auto-updates on file changes (via hooks).
2. Use `detect_changes_tool` for code review.
3. Use `get_affected_flows_tool` to understand impact.
4. Use `query_graph_tool` pattern="tests_for" to check coverage.
