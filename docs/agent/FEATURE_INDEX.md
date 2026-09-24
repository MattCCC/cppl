# Feature index

First lookup for any implementation task. Find the feature, then extract its
rules from the canonical specification:

```sh
cppl-spec-rules extract --feature refinement-types
```

Rule families are the `[FAMILY-NNN]` anchors inside `docs/SPEC.md`. Sections are
given as a navigation aid; the rule IDs are what code and tests cite, because
section numbers move and rule IDs do not.

## Core language

| Feature | Rule families | SPEC | Grammar | Foundations | Trust |
| --- | --- | --- | --- | --- | --- |
| Normative terminology | `TERM-*` | §1 | — | — | — |
| C++ superset relationship | `CXX-*`, `SRCCOMPAT-*` | §2, §60 | §1 | — | §9 |
| Contextual words | `WORD-*` | §3 | §1 | — | — |
| Semantic domains | `DOMAIN-*` | §4 | — | Propositions as types | §7 |
| Propositions | `PROP-*`, `BOOL-*` | §5, §6 | — | Propositions as types | — |
| Logical equality | `EQ-*`, `REFL-*` | §7, §16 | — | Equality | — |
| Quantification | `FORALL-*`, `EXISTS-*` | §8, §9 | §28 | Universal/Existential | — |

## Specification and proof

| Feature | Rule families | SPEC | Grammar | Foundations | Trust |
| --- | --- | --- | --- | --- | --- |
| Laws | `LAW-*`, `LAWIMPL-*` | §10, §47 | §3 | Propositions as types | §15, §16 |
| Contracts | `CONTRACT-*`, `CONTRACTCOMP-*` | §11, Annex R | §6, §10–13 | Hoare logic | §6 |
| `verified` | `VERIFIED-*` | §12 | §8 | Weakest preconditions | §6, §27 |
| `pure` | `PURE-*` | §13 | §9 | — | — |
| Specification expressions | `SPECEXPR-*`, `ADMISSIBLE-*` | §14, Annex U | §6 | — | — |
| Proof declarations | `PROOF-*`, `PROOFSRC-*` | §15, Annex H | §4, §5 | Curry–Howard | §4, §13 |
| Case analysis | `CASE-*` | §20 | §5.7, §5.9, §18 | Abstract nominal values, structural case analysis, product decomposition | §4, §19 |
| Checked contradiction, case omission and impossible runtime paths | `CASE-004`, `CASE-005`, `CASE-011`–`CASE-016`, `VERIFIED-023`, `VERIFIED-045`, `WORD-002`, `WORD-010`, `WORD-011`, `ERASE-016` | §20.2, §20.6, §12.7, §3, §36 | §1, §5.6, §5.7 | Contradiction and explosion | §5.1, §6, §19 |
| Induction | `INDUCT-*` | §21 | — | Induction | §4 |
| Ghost state | `GHOST-*`, `ERASE-011` | §25, §36.4 | §21 | — | §29 |

## Types

| Feature | Rule families | SPEC | Grammar | Foundations | Trust |
| --- | --- | --- | --- | --- | --- |
| Refinement types | `REFINE-*`, `REFINEOBL-*` | §17, Annex I | §14–16 | Refinement typing | §10 |
| Dependent/indexed types | `DEP-*` | §18 | §16 | Dependent type theory | — |
| Reasoning over C++ types | `CXXTYPE-*` | §19 | §17, §20 | — | — |

## Execution semantics

| Feature | Rule families | SPEC | Grammar | Foundations | Trust |
| --- | --- | --- | --- | --- | --- |
| Termination, recursion and totality | `TERMINATION-*`, `CORRECT-*`, `LOOP-001`, `LOOP-003`, `LOOP-006` | §22, §23, §24.3 | §25–§27 | Termination and consistency | §12.1 |
| Loop invariants | `LOOP-*` | §24 | §25, §26 | Hoare logic | — |
| Machine arithmetic | `ARITH-*` | §29 | — | — | — |
| Floating point | `FLOAT-*` | §30 | — | — | — |
| Undefined behavior | `UB-*`, `DEFINEDBEHAVIOR-*` | §31, Annex T | — | — | §9 |
| Memory and lifetime | `MEM-*`, `STORAGE-*` | §32, Annex E | — | Storage, capabilities and framing | §10 |
| Exceptions | `EXCEPT-*`, `EXCEPTCONCUR-*` | §33, Annex K | — | — | — |
| Concurrency | `CONCUR-*` | §34 | — | — | — |

## Trust boundaries

| Feature | Rule families | SPEC | Grammar | Foundations | Trust |
| --- | --- | --- | --- | --- | --- |
| `unsafe` boundaries and unsafe dependencies | `UNSAFE-*`, `BOUNDARYEX-010`, `INTERACT-018` | §26, Annex O.12 | §22, §23 | — | §26, §35, §36 |
| `trusted` | `TRUSTED-*` | §27 | §24 | — | §25, §35, §36 |
| Trust propagation and assumption closure | `TRUSTED-*`, `PROOFSRC-005`, `STATUS-002` | §27.1, §27.4, §38, Annex H.12 | §5, §24 | §7.3, §98, §130 | §3.2, §25, §35, §36 |
| Runtime validation | `RUNTIMECHECK-*` | §28 | — | — | §22 |
| FFI and foreign code | `FFI-*` | §35 | — | — | §19, §20 |
| Verification statuses | `STATUS-*`, `STATUSPROMO-*` | §38, §39 | — | — | §27 |
| Fail-closed conformance | `FAILCLOSED-*`, `PROOFFAIL-*`, `SPECFAIL-*` | §52–54 | — | — | §28 |
| Soundness requirement | `SOUND-*`, `LANGVERIFY-*` | §58, §59 | — | Trusted proof kernel | §2, §4 |

## Lowering

| Feature | Rule families | SPEC | Grammar | Foundations | Trust |
| --- | --- | --- | --- | --- | --- |
| Erasure | `ERASE-*`, `ERASEMATRIX-*`, `IRRELEVANCE-*` | §36, §55, Annex M | — | — | §8.1, §29 |
| ABI semantics | `ABI-*` | §37 | — | — | §30, §31 |
| Calls from verified code | `CALL-*`, `BOUNDARY-*` | §40, §41 | — | — | §17 |
| Templates | `TEMPLATE-*` | §42, Annex G | — | — | — |
| Translation units and modules | `TU-*`, `MODULE-*`, `TUBOUND-*` | §44, §45, Annex L | — | — | §18 |

## Normative catalogues

These annexes are mechanical per-construct coverage requirements. Each entry
carries one anchor ID; consult the annex directly when implementing a specific
construct.

| Catalogue | Rule family | SPEC |
| --- | --- | --- |
| Verification coverage requirements | `COVERAGE-*` | Annex N |
| Exhaustive semantics by source construct | `CONSTRUCT-*` | Annex X |
| Standard-library model obligations | `STDOBL-*`, `STDMODEL-*` | Annex Y, Annex J |
| Declaration coverage | `DECLCOVER-*` | Annex S |
| Conformance corpus requirements | `CORPUS-*` | Annex W |
| Cross-feature interaction rules | `INTERACT-*` | Annex V |
| Semantic edge cases | `EDGECASE-*` | Annex Z |

`INTERACT-*` (Annex V) is mandatory reading for any feature work. Most defects
in a verifier are cross-feature, not single-feature.
