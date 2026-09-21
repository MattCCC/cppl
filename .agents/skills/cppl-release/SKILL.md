---
name: cppl-release
description: Prepare or review a C++L release. Use for release candidates, version bumps, publishing compiler binaries, language releases, compatibility declarations, proof artifact format changes, or release-readiness audits.
---

# C++L Release

A release must accurately state what C++L actually guarantees.

## Read first

- `AGENTS.md`
- `docs/SPEC.md`
- `docs/TRUST.md`
- `docs/STATUS.md`
- `docs/COMPATIBILITY.md`
- `SECURITY.md`
- `CHANGELOG.md` when present

## Release checks

Confirm:

- specification matches implementation;
- TCB documentation matches reality;
- `docs/STATUS.md` is current;
- supported C++ modes are accurate;
- platform support is accurate;
- negative proof tests pass;
- soundness regressions pass;
- C++ conformance tests pass;
- artifact formats are versioned;
- cache compatibility is known;
- kernel/core versions are correct;
- known unsupported semantics are documented;
- trusted solver/backend/FFI dependencies are documented;
- no known soundness issue is hidden by wording.

## Release metadata

Record where applicable:

```text
C++L version:
kernel version:
formal-core version:
artifact-format version:
supported C++ modes:
supported targets:
trusted external components:
known unsupported semantics:
known compatibility breaks:
```

## Rule

Never present:

```text
IMPLEMENTED
```

as:

```text
VERIFIED
```

and never describe a partially modeled C++ feature as fully formally verified.
