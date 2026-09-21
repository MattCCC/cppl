# Test matrix

Maps normative rules to the tests that verify them. A rule with no test is an
unverified claim about the implementation.

## Citing rules from tests

Tests cite rule IDs so coverage is attributable and survives specification
edits:

```cpp
// SPEC: REFINE-010
TEST(RefinementWrite, RejectsUnprovenReplacement) { ... }
```

```bash
# SPEC: REFINE-010, STORAGE-004
reject 'write-without-proof' <<'EOF'
...
EOF
```

Prefer a rule ID over a section reference. `SPEC.md 17` stops being accurate the
moment a section is inserted; `REFINE-010` does not.

Report current coverage:

```sh
cppl-spec-rules report          # rules cited by implementation and tests
cppl-spec-rules report --all    # including rules with no citation
```

## Required categories

Every feature needs all five. A feature with only the first is not implemented,
it is demonstrated.

| Category | Question it answers | Location |
| --- | --- | --- |
| Positive | Does a valid program verify? | `tests/fixtures/`, `tests/integration/` |
| Negative | Is an invalid program rejected, for the stated reason? | `tests/negative/` |
| Interaction | Does it hold in combination with other features? | `tests/integration/` |
| Adversarial | Can the feature be used to prove something false? | `tests/negative/`, `tests/kernel/` |
| Erasure | Is runtime behavior unchanged? | `tests/e2e/`, `tests/conformance/` |

A negative test must assert *why* the program was rejected. A test that only
asserts failure passes for the wrong reason as soon as an unrelated error
appears — the existing `tests/negative/refinement_types.sh` does this correctly
by requiring that no `PROVEN` status is reported and that an error is emitted.

## Per-feature matrices

### refinement-types

Manifest: `features/refinement-types.yaml`

| Required case | Category | Status |
| --- | --- | --- |
| Local initialization | positive, negative | partial — `tests/negative/refinement_types.sh` cites `REFINE-003`, `REFINE-004`, `REFINE-005` |
| Parameter crossing | positive, negative | partial |
| Return crossing | positive, negative | partial |
| Assignment and compound update | positive, negative | partial |
| Member initialization and write | positive, negative | needed |
| Array/element write | positive, negative | needed |
| Alias mutation invalidates facts | interaction, adversarial | needed |
| Verified call post-state | interaction | needed |
| Nested refinements | positive, negative | needed |
| Indexed refinements | positive, negative | needed |
| Refinement implication and conversion | positive, negative | needed |
| Overload erasure collision | erasure, negative | needed |
| ABI equivalence | erasure | needed |
| No hidden runtime validation | erasure, adversarial | needed |

Status here describes test coverage, not implementation maturity.
`docs/STATUS.md` is authoritative for the latter, and neither weakens what
`docs/SPEC.md` requires.

## Adversarial baseline

These apply to every feature and correspond to `AGENTS.md` §37:

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

A new feature must not weaken any of them. If a feature makes one of these
harder to state, that is a design problem to escalate, not a test to relax.
