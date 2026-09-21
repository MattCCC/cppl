# cppl-spec-rules

`cppl-spec-rules` assigns and validates stable normative rule IDs in
`docs/SPEC.md`, the canonical specification for **C++L - C++ with Laws**.

`docs/SPEC.md` is roughly 20,000 lines. It is exhaustive by design and stays
that way: this tool never splits it, summarizes it, or competes with it for
authority. It makes the specification *addressable*, so an implementation task
can be given the 30 rules it needs instead of the whole document.

---

## Rule IDs

A normative statement carries a stable anchor:

```text
[REFINE-010] A write to refined storage creates a new logical version and MUST
establish the refinement predicate for the new value.
```

Code, tests, diagnostics and commits cite that ID:

```cpp
// SPEC: REFINE-010
TEST(RefinementWrite, RejectsUnprovenReplacement) { ... }
```

Rule IDs are preferred over section references because sections move whenever
the specification grows; `SPEC.md 17` silently stops meaning what it meant, and
`REFINE-010` does not.

The family comes from the chapter or annex (`REFINE` for §17 refinement types,
`ERASE` for §36 erasure, `CONSTRUCT` for Annex X). The number is sequential
within its family and is never reused.

---

## Commands

```sh
cppl-spec-rules assign [--dry-run]     # tag unlabelled normative statements
cppl-spec-rules check [--allow-gaps]   # uniqueness, gaps, orphan citations
cppl-spec-rules report [--all]         # rule -> implementation -> test coverage
cppl-spec-rules extract REFINE-010     # print a rule's normative text
cppl-spec-rules extract --feature=refinement-types
```

Options: `--spec=PATH` (default `docs/SPEC.md`), `--repo=PATH` (default `.`).

### assign

Inserts `[FAMILY-NNN]` anchors into normative statements that lack one.

It is **idempotent** — running it twice changes nothing — and it inserts only
anchors. It never edits specification text. Verify that directly:

```sh
diff <(sed -E 's/\[[A-Z][A-Z0-9-]*-[0-9]{3}\] ?//g' docs/SPEC.md) SPEC.orig.md
```

The main body states most rules as declarative prose rather than with `MUST`, so
statements are recognized structurally, not by keyword alone. Annexes N, X and Y
are mechanical per-construct catalogues that restate the same dimensions for
every construct; tagging each bullet would produce thousands of near-duplicate
IDs, so those annexes get one anchor per construct entry.

### check

Fails if any rule ID is duplicated, if a family has gaps (a retired rule; pass
`--allow-gaps` when that is intended), or if code cites a rule that does not
exist in the specification.

### report

Shows which rules are cited by implementation and by tests. A rule cited by
nothing is an unverified claim about the implementation.

### extract

Emits a self-contained task packet — the normative text of the requested rules,
each with its source location. `--feature` reads the `rule_families` of a
manifest in `docs/agent/features/` and extracts every rule in those families,
so a rule added to the specification is picked up without editing the manifest.

```sh
$ cppl-spec-rules extract --feature=refinement-types | head -5
## REFINE-001

Source: docs/SPEC.md:1902 — 17. Refinement types

[REFINE-001] A refinement type restricts values of an underlying type with a proposition.
```

---

## Non-goals

- It does not define, interpret or weaken any language semantics.
- It does not split `docs/SPEC.md`.
- It does not decide whether a statement *should* be normative; it finds the
  statements that are and makes them addressable.
- It does not track implementation maturity. `docs/STATUS.md` owns that, and
  never weakens what `docs/SPEC.md` requires.

See `docs/agent/README.md` for how the index layer above this tool fits
together.
