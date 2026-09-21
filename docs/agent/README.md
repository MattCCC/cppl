# Agent execution layer

This directory does not define language semantics.

`docs/SPEC.md` is the single canonical normative specification. It is not split,
and nothing here overrides it. These files exist so that an agent can find the
*relevant slice* of a 20,000-line specification instead of reading all of it, and
so that "done" is decidable rather than a matter of opinion.

```text
docs/SPEC.md            canonical normative source of truth
        ↓
stable rule IDs         [FAMILY-NNN] anchors inside SPEC.md
        ↓
FEATURE_INDEX.md        feature -> rules, across SPEC/GRAMMAR/FOUNDATIONS/TRUST
        ↓
features/*.yaml         machine-readable manifests and dependencies
        ↓
IMPLEMENTATION_MAP.md   rules -> implementation components
        ↓
TEST_MATRIX.md          rules -> tests
```

## Files

| File | Purpose |
| --- | --- |
| `FEATURE_INDEX.md` | First lookup. Feature to normative rules and documents. |
| `IMPLEMENTATION_MAP.md` | Which components must change for a feature, and the behavior each must realize. |
| `INVARIANTS.md` | The cross-cutting rules that hold for every feature. |
| `TEST_MATRIX.md` | Required test categories per feature, and rule-to-test coverage. |
| `VERIFICATION_CHECKLIST.md` | The completion contract an implementation must satisfy. |
| `features/*.yaml` | Machine-readable manifest per feature. |

## Rule IDs

Normative statements in `docs/SPEC.md` carry stable anchors:

```text
[REFINE-010] A write to refined storage creates a new logical version and MUST
establish the refinement predicate for the new value.
```

Cite these from code, tests, diagnostics and commits. They are stable across
edits; section and line numbers are not.

```cpp
// SPEC: REFINE-010
TEST(RefinementWrite, RejectsUnprovenReplacement) { ... }
```

Rule IDs are assigned and validated by `tools/cppl-spec-rules`:

```sh
cppl-spec-rules assign            # tag unlabelled normative statements
cppl-spec-rules check             # uniqueness, gaps, orphan citations
cppl-spec-rules report            # rule -> implementation -> test coverage
cppl-spec-rules extract REFINE-010 ERASE-001
```

`assign` is idempotent and inserts only `[FAMILY-NNN]` anchors; it never alters
specification text.

## How an agent uses this layer

1. Find the feature in `FEATURE_INDEX.md`.
2. Extract its rules: `cppl-spec-rules extract --feature <name>`.
3. Read `INVARIANTS.md` — these apply regardless of feature.
4. Read the feature's row in `IMPLEMENTATION_MAP.md`.
5. Implement the whole rule set, not the motivating example.
6. Satisfy every category in `TEST_MATRIX.md`, citing rule IDs in tests.
7. Check the completion contract in `VERIFICATION_CHECKLIST.md`.
8. Update `docs/STATUS.md` only after the implementation passes.

`docs/STATUS.md` records what is implemented. It never weakens what
`docs/SPEC.md` requires.
