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

## checked-contradiction

Manifest: `features/checked-contradiction.yaml`

Normative sources: `CASE-004`, `CASE-005`, `CASE-011`–`CASE-016` (SPEC §20.2,
§20.6), `VERIFIED-023` (SPEC §12.7), `WORD-002`, `WORD-010` (SPEC §3).

### Components

| Component | Responsibility | Paths |
| --- | --- | --- |
| frontend | Recognize `contradiction evidence;` at a statement's start in a proof body, and `omit label by contradiction evidence;` inside `cases` only where a label followed by `by` comes after `omit`. Read the statement after `by` with the ordinary statement parser. | `compiler/frontend/src/recognizer.cpp`, `compiler/frontend/include/cppl/frontend/syntax.hpp` |
| elaboration | Resolve an omission's label through the same path as an arm's, so a case is accounted for exactly once. | `compiler/elaboration/src/elaborate.cpp` |
| vir | Carry `ContradictionStep` and mark an omitted `CaseArm` explicitly, never by its shape. | `vir/include/cppl/vir/module.hpp` |
| kernel | `False` with no introduction rule; falsity elimination closes any goal from evidence for it; linear arithmetic concludes `False` from facts alone. | `kernel/include/cppl/kernel/proposition.hpp`, `kernel/include/cppl/kernel/proof.hpp`, `kernel/src/check.cpp`, `kernel/src/linear.cpp` |
| obligations | State the named evidence and every standing premise, refute them into `False`, eliminate that into the goal, and record each omission as an obligation of its own with an origin-bearing identity. Keep every standing premise at the current depth. | `compiler/obligations/src/contradiction.cpp`, `compiler/obligations/src/generate.cpp`, `compiler/obligations/include/cppl/obligations/obligation.hpp` |
| refutation | Propose certificates; never decide. | `compiler/refutation/src/refute.cpp` |
| automation | Submit an omission's own evidence and nothing else; name each origin in its own diagnostic. | `compiler/automation/src/evidence.cpp` |
| driver | Count omitted cases and impossible paths apart from laws and from each other. | `compiler/driver/src/pipeline.cpp`, `compiler/driver/src/driver.cpp` |
| formatter, lsp, editors | Lay out an omission as one line; present it as omitted, not as an arm; color only the whole omission form. | `compiler/formatter/src/format.cpp`, `src/lsp/src/decomposition_view.cpp`, `editors/shared/cppl.tmLanguage.json`, `editors/neovim/syntax/cppl.vim` |

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
an impossible path       would record under ImpossiblePath; the two never
                         share an origin, identity, diagnostic or report line
omit, by, contradiction  stay ordinary names outside their grammar positions
the construct            erases completely
```

### Interactions

```text
omission x every decomposition provider    CASE-*, TCB-DECOMP-*
contradiction x quantified goals           FORALL-*
contradiction x structured goals           CASE-014, TCB-CORE-017 (any goal, by falsity elimination)
premises x quantifiers introduced later    PROOF-*
words x ordinary C++ identifiers           WORD-*, CXX-*
omission x erasure                         ERASE-*
```

### Existing surface

```text
tests/fixtures/omitted_case.cpp           accepted omissions, and the accepted half of the matched pair
tests/fixtures/contradiction.cpp          the statement under flat and structured goals
tests/negative/contradictions.sh          every rejection, written out in tests/fixtures/negative/
tests/unit/contradiction_test.cpp         evidence shape, corruption, and the two origins
tests/kernel/adversarial_kernel_test.cpp  falsity elimination and how False may be established
```

Not built: a source form for an unreachable runtime path (`VERIFIED-023`).

---

## Adding a feature to this map

1. Add the feature to `FEATURE_INDEX.md`.
2. Write `features/<name>.yaml` with rules, dependencies, components and tests.
3. Add a section here: components, required behavior, interactions.
4. Add the feature's required test categories to `TEST_MATRIX.md`.

Keep required behavior as a statement of *what must hold*, not a description of
current code. This file maps rules to where they are realized; it does not
restate the rules, which live in `docs/SPEC.md`.
