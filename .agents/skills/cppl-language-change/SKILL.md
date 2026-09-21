---
name: cppl-language-change
description: Design or implement a C++L language change. Use for syntax, Laws, proof constructs, types, contracts, equality, termination, unsafe/trusted semantics, contextual keywords, or any change to observable language meaning.
---

# C++L Language Change

Use this skill when a change affects C++L language semantics.

## Read first

Read the relevant parts of:

- `AGENTS.md`
- `docs/SPEC.md`
- `docs/DESIGN.md`
- `docs/COMPATIBILITY.md`
- `docs/STATUS.md`
- relevant files under `docs/rfcs/`

Also read `docs/TRUST.md` if proof authority, assumptions, erasure, or the TCB may change.

## Workflow

1. State the semantic change independently of its implementation.
2. Identify the existing rule in `docs/SPEC.md`.
3. Define the new rule precisely.
4. Check that valid supported C++ remains valid C++L.
5. Check contextual-keyword, parsing, template, macro, and module interactions.
6. Determine whether the formal core or VIR must change.
7. Determine whether proof soundness or trust changes.
8. Determine whether erasure or runtime semantics change.
9. Create or update an RFC for substantial semantic changes.
10. Update the normative specification before treating implementation behavior as authoritative.
11. Implement the smallest coherent architecture.
12. Add positive and negative conformance tests.
13. Update `docs/STATUS.md`.

## Mandatory outcome

The implementation, tests, and documentation must agree on one semantic rule.

Do not:

- infer semantics from existing implementation accidents;
- weaken a Law to accommodate implementation;
- introduce globally reserved C++L words unnecessarily;
- create semantics that depend on one particular frontend representation;
- merge substantial semantics without an RFC.

If the intended semantics cannot be stated precisely, stop the semantic change rather than inventing behavior.
