# C++L RFC Process

Major C++L design changes are proposed through RFCs.

The RFC process exists because seemingly small syntax changes can alter:

- theorem meaning;
- type-system soundness;
- C++ compatibility;
- runtime semantics;
- erasure;
- trusted computing base.

---

# When an RFC is required

Use an RFC for changes involving:

- new language syntax
- new keywords
- new proof rules
- new type constructors
- changes to equality
- changes to normalization
- changes to termination
- changes to memory semantics
- new unsafe operations
- new trusted mechanisms
- ABI-affecting language features
- changes to proof erasure
- major solver integration
- concurrency semantics

Small diagnostics, bug fixes, tests, and implementation refactors usually do not require an RFC unless they reveal a semantic ambiguity.

---

# File naming

Use:

```text
NNNN-short-title.md
```

Example:

```text
0001-law-declarations.md
0002-refinement-types.md
0003-pointer-provenance.md
```

---

# RFC template

````markdown
# RFC NNNN: Title

## Status

Draft

## Summary

One paragraph describing the proposal.

## Motivation

What problem does this solve?

## Goals

- ...

## Non-goals

- ...

## Proposed syntax

```text
...
```
````

## Static semantics

Define:

- typing rules
- proof obligations
- equality interaction
- refinement interaction
- termination interaction

## Runtime semantics

What remains after proof erasure?

What code executes?

## C++ interoperability

How does the feature interact with:

- ordinary C++
- templates
- overload resolution
- ABI
- standard library
- constexpr
- exceptions
- FFI

## Safety

What invalid programs must be rejected?

## Trust impact

Does this enlarge the trusted computing base?

If yes, why?

## Erasure

What is removed?

What remains?

Why is runtime behavior preserved?

## Diagnostics

What should errors look like?

## Alternatives considered

What other designs were considered?

## Drawbacks

What becomes harder?

## Testing strategy

Include:

- positive cases
- negative cases
- adversarial cases
- soundness regressions

## Compatibility

Does this break existing C++L?

Does this affect supported C++ versions?

## Unresolved questions

- ...

````

---

# Decision standard

An RFC should not be accepted merely because the syntax is convenient.

For proof-relevant changes, the proposal must answer:

```text
What does this mean mathematically?

How is it checked?

What is trusted?

What executes at runtime?

Can unsafe C++ forge it?

Can it be erased?

How does it interact with actual C++ semantics?
````

---

# Accepted RFCs

An accepted RFC becomes part of the intended language direction.

The normative language semantics must then be reflected in `SPEC.md`.

Implementation status should be tracked separately.

An accepted RFC does not imply that the feature already exists.

---

# Rejected RFCs

Rejected RFCs should remain in history when useful.

The purpose is to preserve design reasoning and avoid repeatedly reopening already-explored dead ends without new evidence.

---

# Guiding principle

C++L language evolution should prefer:

```text
explicit semantics
+
small trusted core
+
strong guarantees
```

over:

```text
convenient syntax
+
implicit behavior
+
unclear proof meaning
```
