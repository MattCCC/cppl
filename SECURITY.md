# C++L Security Policy

C++L treats proof soundness bugs as security issues.

A bug that causes the compiler to accept a false theorem can invalidate every guarantee built on top of that theorem.

---

# What counts as a security issue

Please treat the following as security-sensitive:

- false proof acceptance
- proof forgery
- type-safety bypass in verified code
- trusted-assumption omission
- unsafe-boundary omission
- ghost-state leakage into runtime behavior
- unsound proof erasure
- stale proof-cache acceptance
- incorrect kernel normalization
- termination-checker bypass
- solver result incorrectly treated as certified
- UB that invalidates a verified guarantee
- memory unsoundness inside verified regions
- incorrect FFI trust classification
- trust-report falsification
- malformed proof causing kernel compromise
- compiler behavior that marks unverified code as verified

---

# Soundness vulnerabilities

Examples:

```text
Proof<False>
```

can be constructed.

Or:

```text
Proof<1 == 2>
```

is accepted.

Or a function that fails its declared postcondition is marked verified.

These are critical correctness issues even if they do not immediately lead to arbitrary code execution.

---

# Traditional security vulnerabilities

Traditional compiler vulnerabilities are also in scope, including:

- memory corruption
- arbitrary code execution
- path traversal
- malicious package execution
- command injection
- unsafe temporary files
- dependency compromise
- sandbox escape

---

# Reporting

Do not publish exploit details for a suspected proof-soundness vulnerability before maintainers have had an opportunity to investigate.

Use the project's private security-reporting mechanism when available.

A useful report should contain:

- affected version or commit;
- minimal reproducer;
- expected result;
- actual result;
- whether a false theorem is accepted;
- whether unsafe/trusted constructs are involved;
- potential scope of affected proofs.

---

# Severity

Suggested severity categories:

## Critical

- arbitrary false proofs are constructible;
- kernel compromise;
- proof checking can be bypassed broadly;
- trust-report mechanisms can hide arbitrary assumptions.

## High

- specific false theorem classes are accepted;
- termination soundness bypass;
- unsound equality or substitution;
- verified memory safety can be violated.

## Medium

- incorrect trust classification;
- unsound behavior requiring unusual unsupported constructs;
- proof-cache errors with constrained impact.

## Low

- diagnostics incorrectly describe proof status while internal status remains correct;
- non-security crashes without proof-soundness impact.

---

# Response priorities

Soundness regressions take priority over:

- new language features;
- compiler performance;
- syntax improvements;
- IDE features.

If necessary, the affected feature should be disabled until soundness is restored.

---

# Regression policy

Every fixed soundness issue must receive a permanent regression test.

Where possible, also add:

- nearby adversarial cases;
- property tests;
- fuzzing seeds;
- kernel-level tests.

---

# Fuzzing

High-value fuzzing targets include:

- proof-term decoding
- core AST
- substitution
- normalization
- equality checking
- refinement evidence
- proof certificates
- parser-to-core elaboration
- erasure
- proof-cache deserialization

The kernel should treat malformed data as invalid, never as evidence.

---

# Dependency security

Dependencies used inside the TCB require greater scrutiny than ordinary tooling dependencies.

Prefer:

- minimal dependency graphs;
- pinned versions;
- reproducible builds;
- lock files;
- dependency auditing.

A dependency that enters the proof TCB must be documented in `TRUST.md`.

---

# Release policy

Release notes should explicitly call out:

- proof-soundness fixes;
- changes to trusted assumptions;
- TCB changes;
- verifier behavior changes;
- proof-cache invalidations;
- changes to runtime safety semantics.

---

# Security principle

C++L's security model follows one rule:

> A guarantee must never appear stronger than the evidence and assumptions that justify it.
