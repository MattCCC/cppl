---
name: cppl-architecture-change
description: Design or implement a substantial C++L compiler architecture change. Use for new compiler stages, component boundaries, IR layers, frontend/kernel separation, solver orchestration, caching architecture, erasure pipeline, or major dependency direction changes.
---

# C++L Architecture Change

Architecture must preserve a clear direction of authority.

## Read first

- `AGENTS.md`
- `ARCHITECTURE.md`
- `DESIGN.md`
- `TRUST.md`
- `ROADMAP.md`
- relevant RFCs

## Preserve the direction

```text
source / Clang semantics
        ↓
formal elaboration
        ↓
VIR
        ↓
proof obligations
        ↓
automation
        ↓
kernel
        ↓
verified result
        ↓
erasure
        ↓
ordinary C++
```

## Review

Determine:

- What responsibility moves?
- What is the new source of truth?
- Are semantic responsibilities duplicated?
- Does the TCB change?
- Does dependency direction remain acyclic and clear?
- Does the kernel gain frontend concerns?
- Does the frontend gain proof authority?
- Does the design introduce a temporary bridge?
- Can that bridge become accidental permanent architecture?
- Does `ARCHITECTURE.md` need new diagrams?

Prefer one authoritative path over parallel implementations.

Update `ARCHITECTURE.md` with the implementation change.
