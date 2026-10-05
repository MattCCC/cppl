#!/usr/bin/env bash
# SPEC: FORALL-001, REFINE-003
# A quantifier over a refinement type ranges over the refinement's values: what
# holds of each of them is proven, and an instantiation is used once the term's
# membership is established. Refusals are in negative/quantified_propositions.sh.
set -euo pipefail
CPPL="$1"
FIXTURES="$2"
WORK="$3"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/refined-quantifiers.XXXXXX")
for standard in c++17 c++20 c++23; do
    "$CPPL" "-std=$standard" "$FIXTURES/refined_quantifiers.cpp" -o "$run/$standard" \
        --cppl-trust-report > "$run/$standard.log"
    grep -Eq '^Laws proven: +5$' "$run/$standard.log"
    grep -Eq '^  by a written proof: +2$' "$run/$standard.log"
    grep -Eq '^Proof declarations proven: +2$' "$run/$standard.log"
    grep -Eq '^Laws trusted: +1$' "$run/$standard.log"
    grep -Eq '^Function contracts proven: +1$' "$run/$standard.log"
    grep -Eq '^Unresolved obligations: +0$' "$run/$standard.log"
    # Only the use of the trusted law rests on it.
    grep -Eq '^Trust-dependent claims: +1$' "$run/$standard.log"
    grep -Eq '^  proof use_trusted ' "$run/$standard.log"
    test "$("$run/$standard")" = '3 8'
done
