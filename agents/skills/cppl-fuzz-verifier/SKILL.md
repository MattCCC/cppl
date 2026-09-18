---
name: cppl-fuzz-verifier
description: Fuzz C++L proof, parser, VIR, kernel, artifact, or verifier boundaries. Use for adversarial generation, malformed proof terms, differential checking, deserialization fuzzing, crash discovery, or searching for unsound proof acceptance.
---

# C++L Verifier Fuzzing

Fuzzing should search for both crashes and incorrect acceptance.

## Targets

Prioritize:

1. proof artifact deserialization;
2. kernel proof checking;
3. normalization/conversion;
4. elaboration-to-core boundaries;
5. VIR serialization;
6. solver certificate handling;
7. erasure boundaries.

## Oracles

Useful outcomes include:

```text
must reject malformed evidence
must not crash
must be deterministic
must preserve round-trip semantics
must not turn unsupported input into PROVEN
```

Where possible use differential oracles against:

- independently generated evidence;
- reference normalization;
- round-trip serialization;
- debug invariant checking.

## Every interesting failure

- minimize the input;
- classify crash vs soundness issue;
- add permanent regression coverage;
- follow `SECURITY.md` for soundness vulnerabilities.

Never automatically bless fuzz-discovered behavior as intended semantics.
