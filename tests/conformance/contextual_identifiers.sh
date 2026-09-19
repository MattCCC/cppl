#!/usr/bin/env bash
# C++L words used as ordinary identifiers stay ordinary identifiers.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/contextual.XXXXXX")

for standard in c++17 c++20 c++23; do
    binary="$run/contextual-$standard"
    "$CPPL" "-std=$standard" "$FIXTURES/contextual_identifiers.cpp" -o "$binary"
    "$binary"
done

echo "contextual words remain ordinary identifiers in c++17, c++20 and c++23"
