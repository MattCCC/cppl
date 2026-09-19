#!/usr/bin/env bash
set -euo pipefail
CPPL="$1"
FIXTURES="$2"
WORK="$3"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/formal-equality.XXXXXX")
for standard in c++17 c++20 c++23; do
    "$CPPL" "-std=$standard" "$FIXTURES/formal_equality.cpp" -o "$run/$standard" \
        --cppl-trust-report > "$run/$standard.log"
    grep -Eq '^Laws proven: +2$' "$run/$standard.log"
    grep -Eq '^Proof declarations proven: +6$' "$run/$standard.log"
    grep -Eq '^Function contracts proven: +1$' "$run/$standard.log"
    grep -Eq '^Unresolved obligations: +0$' "$run/$standard.log"
    test "$("$run/$standard")" = '2 7'
done
