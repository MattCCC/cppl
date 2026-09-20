#!/usr/bin/env bash
set -euo pipefail
CPPL="$1"
FIXTURES="$2"
WORK="$3"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/quantified-propositions.XXXXXX")
for standard in c++17 c++20 c++23; do
    "$CPPL" "-std=$standard" "$FIXTURES/quantified_propositions.cpp" -o "$run/$standard" \
        --cppl-trust-report > "$run/$standard.log"
    grep -Eq '^Laws proven: +8$' "$run/$standard.log"
    grep -Eq '^  by a written proof: +6$' "$run/$standard.log"
    grep -Eq '^Proof declarations proven: +1$' "$run/$standard.log"
    grep -Eq '^Function contracts proven: +1$' "$run/$standard.log"
    grep -Eq '^Laws trusted: +0$' "$run/$standard.log"
    grep -Eq '^Unresolved obligations: +0$' "$run/$standard.log"
    test "$("$run/$standard")" = '40 1 3'
done
