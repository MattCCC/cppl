---
name: cppl-doc-consistency
description: Audit C++L documentation for semantic consistency. Use when SPEC.md, TRUST.md, DESIGN.md, ARCHITECTURE.md, COMPATIBILITY.md, STATUS.md, ROADMAP.md, examples, or RFCs may disagree with one another or with implementation status.
---

# C++L Documentation Consistency

Each document has one authority domain.

## Authority map

```text
SPEC.md           language semantics
TRUST.md          trust boundaries
FOUNDATIONS.md    mathematical basis
DESIGN.md         rationale
ARCHITECTURE.md   implementation structure
COMPATIBILITY.md  C++/ABI compatibility
STATUS.md         current implementation maturity
ROADMAP.md        planned sequence
SECURITY.md       vulnerability policy
```

## Workflow

1. Identify the factual or semantic claim.
2. Identify which document owns that claim.
3. Keep the authoritative definition there.
4. Replace duplicated definitions elsewhere with short references.
5. Check examples against `docs/STATUS.md`.
6. Distinguish planned syntax from implemented syntax.
7. Check terminology and verification statuses for consistency.
8. Never modify normative semantics merely to make documentation agree with implementation.

Prefer references over duplicated normative paragraphs.
