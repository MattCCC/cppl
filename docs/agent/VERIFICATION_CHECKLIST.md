# Completion contract

A feature is not complete because its syntax parses.

Recognizing `decreases` is not termination checking. Termination is complete when
recursive and loop obligations are generated, well-foundedness is checked on every
path, call edges preserve the measure, failure is fail-closed, the construct
erases, diagnostics explain failures, and tests cover all of it.

A feature is COMPLETE only when every item below holds.

## Syntax

1. Canonical syntax from `docs/GRAMMAR.md` parses.
2. Invalid syntax is rejected with a diagnostic, not silently accepted.
3. The syntax is contextual and does not break supported valid C++ (`CXX-*`).

## Semantics

4. Names, types and overloads resolve through the ordinary C++ entity (`CXXTYPE-*`).
5. Every normative rule for the feature is implemented, not only the motivating case.
6. All required proof obligations are generated, on every path that requires them.
7. Proof evidence is checked by the kernel; no path admits a claim without it.
8. Failure is fail-closed: unproven, unsupported and unparseable all mean failure.

## Interaction

9. Storage versioning and aliasing are handled (`STORAGE-*`, `MEM-*`).
10. Call boundaries and call effects are handled (`CALL-*`, `CONTRACTCOMP-*`).
11. Control flow, including exceptional continuations, is handled (`STMT-*`, `EXCEPT-*`).
12. Templates use instantiated semantics (`TEMPLATE-*`).
13. Applicable Annex V interaction rules hold (`INTERACT-*`).

## Lowering

14. The feature erases with no runtime trace (`ERASE-*`, `IRRELEVANCE-*`).
15. ABI and layout are unchanged by verification metadata (`ABI-*`).
16. No hidden runtime validation was introduced (`RUNTIMECHECK-*`).

## Evidence

17. Positive tests pass: valid programs verify.
18. Negative tests reject: invalid programs fail, for the stated reason.
19. Interaction tests pass: the combinations in items 9–13.
20. Adversarial tests pass: the feature cannot be used to prove a false claim.
21. Erasure tests pass: runtime behavior is equivalent before and after.
22. Tests cite rule IDs, so coverage is attributable:

    ```cpp
    // SPEC: REFINE-010
    TEST(RefinementWrite, RejectsUnprovenReplacement) { ... }
    ```

23. `cppl-spec-rules report` shows the feature's rules cited by tests.

## Documentation

24. Documentation examples compile and verify as specified.
25. `docs/STATUS.md` reflects actual coverage, updated only after the work passes.

## Status

A feature's status comes from implementation evidence and tests. It never changes
what `docs/SPEC.md` requires.

```text
NOT_STARTED   no implementation
PARTIAL       some rules implemented; the rest are still required
COMPLETE      every item above holds
BLOCKED       cannot proceed soundly; the blocker is recorded, not worked around
```

`PARTIAL` is an honest state. Recording `COMPLETE` for a feature that satisfies
only its motivating example is not, and `docs/STATUS.md` must never be used to
weaken a requirement in `docs/SPEC.md`.
