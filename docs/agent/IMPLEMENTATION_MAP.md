# Implementation map

Maps normative rules to the components that must realize them. Component paths
are where the work lands; `docs/ARCHITECTURE.md` explains why the boundaries sit
where they do.

An agent should never be told to read all of `docs/SPEC.md` and implement a
feature. It should be given a feature's rules, its components, and its required
behavior.

## Component vocabulary

```text
frontend        compiler/frontend/          recognition, projection, syntax
elaboration     compiler/elaboration/       formal elaboration
obligations     compiler/obligations/       obligation generation, contracts, status
analysis        compiler/analysis/          semantic analysis
automation      compiler/automation/        proof search, evidence
refutation      compiler/refutation/        arithmetic refutation search, untrusted
decomposition   compiler/decomposition/     case and product decomposition
erasure         compiler/erasure/           lowering to ordinary C++
diagnostics     compiler/diagnostics/       failure explanation
kernel          kernel/                     trusted proof checking
vir             vir/                        verification IR
```

---

## refinement-types

Manifest: `features/refinement-types.yaml`

Normative sources: `REFINE-*` (SPEC §17), `REFINEOBL-*` (Annex I), `DEP-*`
(SPEC §18), plus `STORAGE-*`, `CALL-*`, `ERASE-*` and `ABI-*` where they apply.

### Components

| Component | Responsibility | Paths |
| --- | --- | --- |
| frontend | Recognize refinement declarations, `self`, indexed refinements. Keep the words contextual. | `compiler/frontend/src/recognizer.cpp`, `compiler/frontend/src/projection.cpp`, `compiler/frontend/include/cppl/frontend/syntax.hpp` |
| places | Designate storage: a root plus a path of projections. One access resolver, one read, one write. | `vir/include/cppl/vir/place.hpp`, `clang/src/bridge.cpp` (`resolve_access`, `read_place`, `BodyLowering::write`) |
| capabilities | Carry `readable`/`writable` as context hypotheses, never as kernel propositions. Gate every dereference. | `vir/include/cppl/vir/capability.hpp`, `compiler/frontend/src/formal_projection.cpp` (`capability_form`), `clang/src/bridge.cpp` (`resolve_storage`) |
| elaboration | Elaborate the predicate into a formal proposition over the base type. | `compiler/elaboration/src/elaborate.cpp` |
| obligations | Emit a membership obligation at every semantic crossing. | `compiler/obligations/src/generate.cpp`, `compiler/obligations/src/contracts.cpp` |
| analysis | Track logical versions; invalidate facts on possible-alias mutation. | `compiler/analysis/src/analyze.cpp` |
| automation | Discharge predicates from path facts where possible. | `compiler/automation/src/evidence.cpp` |
| erasure | Erase to the ultimate C++ base representation. | `compiler/erasure/src/erase.cpp` |
| diagnostics | Explain which crossing failed and which predicate was unproven. | `compiler/diagnostics/src/diagnostic.cpp` |

### Required behavior

```text
base -> refinement            requires evidence for the predicate
refinement -> base            requires no proof
refinement -> refinement      requires implication over the same base type
every write                   creates a new logical version and re-establishes
                              the predicate
possible-alias mutation       invalidates facts about earlier versions
refined return                creates a membership obligation on every normal
                              return
refined member or element     preserves the predicate on every construction and
                              write path, at the member's own place, owed where
                              the value enters rather than at the next read
semantic validity             is recursive: a record is valid when its
                              refinement-bearing subobjects are
refined parameter             supplies the predicate as an entry premise, not an
                              ABI check, recursively for its subobjects
disjointness                  is proved from Clang's resolution only, never from
                              a type-based aliasing argument
erasure                       uses the ultimate C++ base representation
no path                       inserts hidden runtime validation
```

### Crossings that owe an obligation

Every one of these is a crossing; missing any of them is an incomplete
implementation, not a partial one.

```text
local initialization
parameter entry into verified reasoning
function argument binding
return
assignment and compound update
member initialization and member write
array/element write
construction, copy and move
verified call post-state
```

### Interactions

```text
refinement x aliasing            STORAGE-*, MEM-*
refinement x calls               CALL-*, CONTRACTCOMP-*
refinement x members             CLASS-*
refinement x elements            STORAGE-*
refinement x templates           TEMPLATE-*
refinement x virtual dispatch    CLASS-*
refinement x exceptions          EXCEPT-*
refinement x erasure             ERASE-*, ERASEMATRIX-*
refinement x ABI                 ABI-*
refinement x overload erasure    ABI-*, ERASE-*
```

### Existing surface

```text
tests/negative/refinement_types.sh       refinements that must be refused
tests/negative/verified_storage.sh       storage crossings
tests/fixtures/verified_storage.cpp
```

---

## case-analysis

Manifest: `features/case-analysis.yaml`

Normative sources: `CASE-001`–`CASE-010` and `CASE-017`–`CASE-020` (SPEC §20),
`WORD-012` (SPEC §3), `ERASE-016` (SPEC §36), `TCB-DECOMP-*` (TRUST §19).

### Components

| Component | Responsibility | Paths |
| --- | --- | --- |
| frontend | Recognize `cases` and `decompose` with their arms; a label is an id-expression whose parts may carry template arguments. Project each subject and label as an expression Clang resolves, and request completion of a subject's type so a specialization reached through a reference is instantiated. | `compiler/frontend/src/recognizer.cpp`, `compiler/frontend/src/projection.cpp` |
| bridge | Model each resolved type's representation: identity, components and access, template arguments, enumerators at the underlying type's width and signedness. Refuse bases, unions, reference and inaccessible members, and incomplete types by name. | `clang/src/bridge.cpp` |
| decomposition | Providers state the partition, each case's discriminator and binders, and which case a Clang-resolved label denotes; nothing else. | `compiler/decomposition/src/scoped_enum.cpp`, `compiler/decomposition/src/structural.cpp`, `compiler/decomposition/src/registry.cpp` |
| elaboration | The one engine: match arms to cases, check exhaustiveness, derive the residual, scope binders, and nest. | `compiler/elaboration/src/elaborate.cpp` |
| analysis | Resolve binder types to a fixpoint for nested statements. | `compiler/analysis/src/analyze.cpp` |
| obligations | Split the goal on each discriminator in turn; the residual branch holds when none does. | `compiler/obligations/src/generate.cpp` |
| kernel | Check projections of abstract nominal values; no representation-specific rule. | `kernel/src/term.cpp`, `kernel/src/types.cpp` |
| erasure | Remove every construct, binder and arm; a split on a runtime path becomes an empty statement, checked as a lowering. | `compiler/erasure/src/erase.cpp` |
| formatter, lsp | Lay arms out canonically; complete labels and binders and show case sites from the compiler's own elaboration. | `compiler/formatter/src/format.cpp`, `src/lsp/src/decomposition_view.cpp` |

A split on a runtime path runs through the same components with these
additions:

| Component | Responsibility | Paths |
| --- | --- | --- |
| frontend | Recognize `cases`/`decompose` at a statement's start in a verified body, word by word C++-first; admit only nested splits and claims in arms; record each claim and omission in its arms as the split's. Project a block per split: marker, subject, labels, and per arm its marker, its binders declared from a never-defined function, and its nested blocks. | `compiler/frontend/src/recognizer.cpp`, `compiler/frontend/src/projection.cpp` |
| bridge | Read the subject and labels through the ordinary reads at the versions current there; lower each arm as the path continued through the rest of the body; read an arm binder as the value it names, never as storage. | `clang/src/bridge.cpp` (`lower_split`) |
| analysis | Resolve a split's binder types from the subject type the body lowering recorded. | `compiler/analysis/src/analyze.cpp` |
| elaboration | Match arms by the proof side's rules, substitute each binder by its case's value, and refuse a binder read at a type its case does not bind. | `compiler/elaboration/src/elaborate.cpp` (`convert_split`) |
| obligations | Walk each arm under exactly the proof-side split's facts for its case, after checking every state has exactly one arm; an omission is an omitted-case obligation checked against the path. | `compiler/obligations/src/contracts.cpp` (`split_path`), `compiler/obligations/src/generate.cpp` |

### Required behavior

```text
a provider              states states, conditions and label meaning only
the residual            is derived by the engine as no named condition holding
an arm                  is present for every case, or its case is omitted by a
                        checked contradiction; there is no wildcard
a binder                is a projection or alias of the existing object
a label                 is resolved by Clang, so an alias, alias template or
                        class template member names the same case
a subject               is one value for the statement; its type must be
                        complete, and C++ instantiates it where it can
a sum and a product     never stand in for each other
the construct           erases completely
a split on a path       reads its subject at the versions current there and
                        continues the path once per arm, under that case's
                        facts; a later write gives the storage a version no
                        case fact describes
every state             keeps its path: exactly one arm each, checked where
                        the path is walked
```

### Interactions

```text
provider x provider, nested              CASE-006
cases x omitted cases                    CASE-004, CASE-011
cases x quantified goals and laws        FORALL-*, LAW-*
subject x templates and aliases          TEMPLATE-*, CASE-002
cases x erasure                          ERASE-*
```

---

## checked-contradiction

Manifest: `features/checked-contradiction.yaml`

Normative sources: `CASE-004`, `CASE-005`, `CASE-011`–`CASE-016` (SPEC §20.2,
§20.6), `VERIFIED-023`, `VERIFIED-045` (SPEC §12.7), `WORD-002`, `WORD-010`,
`WORD-011` (SPEC §3), `ERASE-016` (SPEC §36).

### Components

| Component | Responsibility | Paths |
| --- | --- | --- |
| frontend | Recognize `contradiction evidence;` at a statement's start in a proof body, and `omit label by contradiction evidence;` inside `cases` only where a label followed by `by` comes after `omit`. Read the statement after `by` with the ordinary statement parser. In a function body, read `contradiction name;` or `contradiction name(...);` at a statement's start as a claim only when the unit uses the word nowhere else outside laws and proofs, and only in a verified body; warn when C++ keeps it. Erase a claim's words and keep its `;`, and give Clang a block of marker declarations where it stood. | `compiler/frontend/src/recognizer.cpp`, `compiler/frontend/include/cppl/frontend/syntax.hpp`, `compiler/frontend/src/projection.cpp`, `compiler/erasure/src/erase.cpp` |
| bridge | Read a claim's block as the end of its path, its arguments at the versions current there, and nothing after it on that path. | `clang/src/bridge.cpp`, `clang/include/cppl/clang/ast.hpp` |
| elaboration | Resolve an omission's label through the same path as an arm's, so a case is accounted for exactly once. Resolve a claim's evidence name to a proof declaration, reporting a name no proof declares once, where it is written. Refuse a verified body that did not read every claim written in it. | `compiler/elaboration/src/elaborate.cpp` |
| vir | Carry `ContradictionStep` and mark an omitted `CaseArm` explicitly, never by its shape. Carry a claim as `PathContradiction`, a path end with no value. | `vir/include/cppl/vir/module.hpp`, `vir/include/cppl/vir/expr.hpp` |
| kernel | `False` with no introduction rule; falsity elimination closes any goal from evidence for it; linear arithmetic concludes `False` from facts alone. | `kernel/include/cppl/kernel/proposition.hpp`, `kernel/include/cppl/kernel/proof.hpp`, `kernel/src/check.cpp`, `kernel/src/linear.cpp` |
| obligations | State the named evidence and every standing premise, refute them into `False`, eliminate that into the goal, and record each omission as an obligation of its own with an origin-bearing identity. Keep every standing premise at the current depth. State a claim as a partial-correctness condition, the path's facts closed over `False`, and build its evidence by the same refutation once the proofs are lowered; a claim that cannot be given evidence carries its refusal. | `compiler/obligations/src/contradiction.cpp`, `compiler/obligations/src/generate.cpp`, `compiler/obligations/src/contracts.cpp`, `compiler/obligations/include/cppl/obligations/obligation.hpp` |
| refutation | Propose certificates; never decide. | `compiler/refutation/src/refute.cpp` |
| automation | Submit an impossibility's own evidence and nothing else, never a strategy's, and a claim resting on a callee only once that callee is proven; name each origin in its own diagnostic. | `compiler/automation/src/evidence.cpp`, `compiler/automation/src/composition.cpp` |
| driver | Count omitted cases and impossible paths apart from laws and from each other. | `compiler/driver/src/pipeline.cpp`, `compiler/driver/src/driver.cpp` |
| formatter, lsp, editors | Lay out an omission as one line; present it as omitted, not as an arm. The grammar colors only the whole omission form; the server reports each `contradiction` keyword the recognizer read as a semantic token, a claim only once the compile of the whole unit recognized claims. | `compiler/formatter/src/format.cpp`, `src/lsp/src/decomposition_view.cpp`, `src/lsp/src/semantic_tokens.cpp`, `src/lsp/src/server.cpp`, `editors/shared/cppl.tmLanguage.json`, `editors/neovim/syntax/cppl.vim` |

### Required behavior

```text
contradiction            is established from the premises alone, before the
                         goal is considered; a goal that merely follows
                         establishes nothing
an omitted case          is checked under its own discriminator, residual
                         cases included
a missing arm            is non-exhaustive, never an intentional omission
a failed search          is an unproven claim, never an impossibility
an omission              is an obligation of its own: origin OmittedCase, its
                         own identity, goal and kernel-checked evidence
an impossible path       is an obligation of its own under ImpossiblePath; it
                         and an omission never share an origin, identity,
                         diagnostic or report line
a claim on a path        is checked against every fact of the path and ends it;
                         nothing after it on that path owes anything
a claim's evidence       is only the contradiction written for it, and waits
                         for every callee postcondition it rests on
omit, by, contradiction  stay ordinary names outside their grammar positions,
                         and a claim's spelling stays C++ wherever the word
                         means anything else in the unit
the construct            erases completely; a claim leaves its `;`
```

### Interactions

```text
omission x every decomposition provider    CASE-*, TCB-DECOMP-*
contradiction x quantified goals           FORALL-*
contradiction x structured goals           CASE-014, TCB-CORE-017 (any goal, by falsity elimination)
premises x quantifiers introduced later    PROOF-*
words x ordinary C++ identifiers           WORD-*, CXX-*
omission x erasure                         ERASE-*
claim x control flow and erasure           ERASE-016
claim x loops and verified calls           VERIFIED-014, LOOP-*
```

### Existing surface

```text
tests/fixtures/omitted_case.cpp           accepted omissions, and the accepted half of the matched pair
tests/fixtures/contradiction.cpp          the statement under flat and structured goals
tests/negative/contradictions.sh          every rejection, written out in tests/fixtures/negative/
tests/unit/contradiction_test.cpp         evidence shape, corruption, and the two origins
tests/kernel/adversarial_kernel_test.cpp  falsity elimination and how False may be established
tests/fixtures/impossible_path.cpp        claims across branches, loops, calls, versions and an unbraced if
tests/fixtures/contradiction_as_a_cpp_name.cpp  the claim's spelling kept as a C++ declaration
tests/fixtures/contradiction_named_by_a_header.cpp  the same, with the word named only by a header
tests/e2e/impossible_path.sh              claims counted, program run, erasure to an empty statement
tests/negative/impossible_paths.sh        every refused claim, written out in tests/fixtures/negative/
tests/unit/lsp_semantic_tokens_test.cpp   editor coloring of claims, including one a header makes C++
```

A grammar cannot tell whether the unit gives `contradiction` another meaning,
so it leaves the runtime form uncolored rather than color a C++ declaration.
The language server colors it instead, from the recognizer's positions in the
buffer as written, and only once the compile of the whole unit, which sees its
headers, has recognized claims.

---

## trust-propagation

Manifest: `features/trust-propagation.yaml`

Normative sources: `TRUSTED-001`–`TRUSTED-009` (SPEC §27), `PROOFSRC-005`,
`PROOFSRC-006` (Annex H), `STATUS-002`, `STATUSPROMO-002` (SPEC §38, §39),
`INTERACT-020` (Annex V.14); `TRUST.md` §25, §35, §36.

### Components

| Component | Responsibility | Paths |
| --- | --- | --- |
| elaboration | Resolve an evidence name to a premise first, then a proof or a trusted law; refuse a name that denotes more than one proof or trusted law. | `compiler/elaboration/src/elaborate.cpp` |
| vir | Carry a trusted law named as evidence as `TrustedLawRef`, distinct from a proof reference. | `vir/include/cppl/vir/module.hpp` |
| obligations | Collect the trusted laws a proof names and those of every proof it uses; lower each use to the hypothesis for its law; close the evidence over them; discharge a used proof's premises with the user's own hypotheses; close an omission's evidence over its proof's premises, and a runtime path claim's over those of the proof it names; refuse a law with no stated proposition. | `compiler/obligations/src/generate.cpp`, `compiler/obligations/include/cppl/obligations/obligation.hpp` |
| status | Accept an acceptance only for the goal under exactly the premises the verdict names, and keep them. | `compiler/obligations/include/cppl/obligations/status.hpp`, `compiler/obligations/src/status.cpp` |
| automation | Check written evidence relative to its premises; never suppose a trusted law in a strategy. Accept a partial contract's condition relative to its premises, since its evidence is never composed into another's, and a stage only outright. | `compiler/automation/src/evidence.cpp`, `compiler/automation/src/composition.cpp` |
| trust closure | Give every proven claim its closure; join contracts across verified calls to a fixed point; report every unattributable dependency as a fault. | `compiler/obligations/include/cppl/obligations/trust.hpp`, `compiler/obligations/src/trust.cpp` |
| driver | Fail the build on a fault or a proven claim without a closure; print closures, the split per category, and unused trusted laws. | `compiler/driver/src/pipeline.cpp`, `compiler/driver/src/driver.cpp` |

### Required behavior

```text
a trusted law named as evidence   makes the proof relative to it; the kernel
                                  checks the claim under exactly its premises
a verdict                         never names fewer premises than its evidence
                                  was checked under, nor more
a proof using a proof             rests on every law the used proof rests on
an omitted case                   rests on every law of the proof it is in
a runtime path claim              rests on every law of the proof it names
a contract                        rests on its body's obligations and on every
                                  contract it calls, to a fixed point
a trusted law's premise           is still owed where the law is applied
a trusted law                     is a premise only where a statement names it
an ambiguous evidence name        is refused, never resolved by preference
a law with no stated proposition  cannot be named as evidence
a trusted law                     is TRUSTED, never counted as proven
a dependency no claim accounts for, or a proven claim with no closure,
                                  is an internal error
a unit with no trusted law        reports none, and every existing line keeps
                                  its meaning
```

### Interactions

```text
trusted law x written proofs and proof chains     TRUSTED-006, PROOFSRC-006
trusted law x apply over an expects premise       TRUSTED-007, PROOFSRC-005
trusted law x contradiction and omitted cases     TRUSTED-008, CASE-011, CASE-012
trusted law x runtime path claims                 VERIFIED-023, CASE-016
trusted law x verified-call composition           TRUSTED-002 (TRUST.md 35)
trusted law x circular proofs                     PROOFSRC-007
trusted law x several translation units           TCB-TRUST-005
trusted law x erasure                             ERASE-*
```

### Existing surface

```text
tests/fixtures/trust_closure.cpp          every accepted use, and the accepted half of the matched pair
tests/e2e/trust_closure.sh                the whole closure section, determinism, two units, erasure
tests/negative/trusted_dependencies.sh    every rejection, written out in tests/fixtures/negative/
tests/unit/trust_closure_test.cpp         contract propagation, cycles, and every fault
tests/unit/verdict_test.cpp               the verdict gate under premises
```

Not built: a trusted law admitting a memory proposition (`TRUSTED-003`), and
carrying a closure across translation units through proof artifacts.

---

## Adding a feature to this map

1. Add the feature to `FEATURE_INDEX.md`.
2. Write `features/<name>.yaml` with rules, dependencies, components and tests.
3. Add a section here: components, required behavior, interactions.
4. Add the feature's required test categories to `TEST_MATRIX.md`.

Keep required behavior as a statement of *what must hold*, not a description of
current code. This file maps rules to where they are realized; it does not
restate the rules, which live in `docs/SPEC.md`.
