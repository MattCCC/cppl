#!/usr/bin/env bash
# Uses of a trusted law that must be refused (SPEC.md PROOFSRC-005, TRUSTED-002,
# TRUSTED-004; TRUST.md 25, 35).
#
# A trusted law may be applied like a proven law, and what is derived from it is
# proven relative to it. None of these derives anything: each would either
# assume more than the law states, pick an assumption by preference, use one
# where it was not named, or rest on an assumption no one can read. The
# accepted uses, and the report of what each proven claim rests on, are in
# `fixtures/trust_closure.cpp`, driven by `e2e/trust_closure.sh`.
set -euo pipefail

CPPL="$1"
FIXTURES="$2/negative"
WORK="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/trusted_dependencies.XXXXXX")

# Compiles a fixture that must fail, then checks it failed for the stated
# reason rather than for some unrelated mismatch.
refuse() {
    local name="$1"
    local diagnostic="$2"
    local status=0
    "$CPPL" -std=c++20 --cppl-trust-report "$FIXTURES/$name.cpp" -o "$run/$name" > "$run/$name.log" 2>&1 ||
        status=$?

    if [ "$status" -eq 0 ]; then
        echo "$name was accepted" >&2
        cat "$run/$name.log" >&2
        exit 1
    fi
    if [ -e "$run/$name" ]; then
        echo "a program was produced for $name" >&2
        exit 1
    fi
    # A refused build reports nothing as proven, relative to trust or otherwise.
    if grep -qE "PROVEN|C\+\+L Trust Report|rests on" "$run/$name.log"; then
        echo "$name was described as proven" >&2
        cat "$run/$name.log" >&2
        exit 1
    fi
    if ! grep -qF "$diagnostic" "$run/$name.log"; then
        echo "$name did not fail for the stated reason" >&2
        cat "$run/$name.log" >&2
        exit 1
    fi
}

# SPEC: PROOFSRC-005, TRUSTED-001, TRUSTED-007
# Trust in a law does not extend to its premise, which is still owed.
refuse trusted_premise_still_owed "proof 'beyond_the_bound' does not establish what it claims"

# SPEC: PROOFSRC-005
# A trusted law is evidence for what it states and for nothing stronger.
refuse trusted_law_misapplied "'sensor_identity' does not prove what proof 'stronger_than_assumed' claims"

# SPEC: PROOFSRC-005, CASE-011
# The refused half of a matched pair whose accepted half is `only_where_named`
# in `fixtures/trust_closure.cpp`: an assumption named elsewhere in a proof is
# not a premise standing where it is not named.
refuse trusted_law_only_where_named "'truth' does not state a contradiction"
grep -qF "trusted_law_only_where_named.cpp:32:13" "$run/trusted_law_only_where_named.log"

# SPEC: PROOFSRC-005, PROOFSRC-006, TRUSTED-009
# Which assumption a proof rests on is never chosen by preference.
refuse trusted_law_and_proof_share_a_name "'shared' names more than one proof or trusted law"
refuse trusted_law_overloaded "'overloaded' names more than one proof or trusted law"

# SPEC: TRUSTED-005 (TRUST.md 2.10)
# An assumption the formal core cannot state is not supposed in its place.
refuse trusted_law_unstated "trusted law 'signed_growth' has no stated proposition"

# SPEC: PROOFSRC-007 (TRUST.md TCB-PROV-005)
# A cycle of proofs has no evidence, whatever else it names.
refuse trusted_law_in_a_cycle "proof 'going_round' depends on itself through the proofs it uses"

# SPEC: VERIFIED-045, TRUSTED-006
# A runtime path claim names a proof declaration, and a trusted law is not one.
refuse trusted_law_named_on_a_runtime_path "no proof named 'broken_counter' is in scope here"

# SPEC: TRUSTED-003, TRUSTED-008, VERIFIED-044
# A memory proposition is admitted only as an explicit assumption, and no proof
# statement can use one: it is not a proposition a goal or a premise can be.
refuse trusted_memory_law_named "trusted law 'device_window' admits a memory proposition, which no proof statement can use"
refuse memory_law_untrusted "law 'device_window' states a memory proposition, which no proof can establish"
refuse memory_proposition_proof \
    "proof 'claims_a_capability' states a memory proposition, which no proof can establish"

echo 'a trusted law is used only as stated, only where named, and never in place of evidence'
