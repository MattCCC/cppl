# Cross-cutting invariants

These hold for every feature, in every task, regardless of what is being
implemented. They are not a feature checklist; they are the properties that make
a C++L result mean anything.

`AGENTS.md` §38 and §39 state the repository invariants in full. This file maps
them to the normative rules that require them, so an implementation can cite the
authority rather than an unattributed convention.

## Fail closed

| Invariant | Rules |
| --- | --- |
| An unproven obligation never becomes a verified claim. | `FAILCLOSED-*`, `PROOFFAIL-*` |
| An unsupported construct never becomes an assumption. | `FAILCLOSED-*`, `COVERAGE-*` |
| Solver failure, timeout or resource exhaustion is failure, not success. | `PROOFFAIL-*` |
| A malformed or unparseable specification is failure, not an empty obligation. | `SPECFAIL-*` |

Where verification cannot proceed soundly, the result is a diagnostic, never a
weaker interpretation of the program.

## Status separation

| Invariant | Rules |
| --- | --- |
| `UNRESOLVED`, `TRUSTED`, `UNSAFE` and `UNVERIFIED` never silently become `PROVEN`. | `STATUS-*`, `STATUSPROMO-*` |
| Trusted assumptions stay visible in the trust report. | `TRUSTED-*` |
| `unsafe` does not manufacture proof evidence. | `UNSAFE-*`, `SOUND-*` |
| `trusted` and `unsafe` remain orthogonal. | `ORTHOTRUST-*` |
| Verification and runtime checking remain orthogonal. | `ORTHOCHECK-*` |

## Proof integrity

| Invariant | Rules |
| --- | --- |
| The kernel is the only authority that admits a proof. | `SOUND-*`, `LANGVERIFY-*` |
| Proof objects cannot be forged; evidence is independently checkable. | `SOUND-*`, `PROOF-*` |
| Nontermination cannot prove an arbitrary proposition. | `TERMINATION-*`, `CORRECT-*` |
| A Law is a specification, never a test, assertion or solver hint. | `LAW-*`, `LAWIMPL-*` |
| Definitional and propositional equality are never conflated. | `EQ-*`, `REFL-*` |

## Runtime equivalence

| Invariant | Rules |
| --- | --- |
| Proof-only constructs have no runtime effect. | `IRRELEVANCE-*`, `ERASE-*` |
| Ghost state cannot affect runtime behavior. | `GHOST-*`, `ERASE-*` |
| Erasure preserves execution, lifetime, exceptions, layout and calling convention. | `ERASE-*`, `ERASEMATRIX-*` |
| No hidden runtime validation is inserted to make a failed proof succeed. | `RUNTIMECHECK-*`, `ERASE-*` |
| Verification metadata does not change the ABI. | `ABI-*` |

## C++ superset

| Invariant | Rules |
| --- | --- |
| Supported valid C++ remains valid C++L with no source change. | `CXX-*`, `SRCCOMPAT-*` |
| Ordinary C++ semantics are Clang's authority, not a reimplementation. | `CXX-*`, `CXXTYPE-*` |
| C++L words are contextual, not globally reserved. | `WORD-*` |
| Template instantiation verifies the instantiated semantics, never a reused specialization. | `TEMPLATE-*` |

## Interaction

Most verifier defects are cross-feature. Annex V (`INTERACT-*`) is normative and
applies to every feature task. Before declaring a feature complete, check its
interactions with:

```text
aliasing and storage versioning
calls and call effects
templates and instantiation
class members and virtual dispatch
exceptions and alternate continuations
concurrency and interference
erasure and ABI
trusted and unsafe boundaries
```

A feature implemented against its motivating example only, without its
interactions, is not implemented.
