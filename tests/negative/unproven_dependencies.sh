#!/usr/bin/env bash
# A Law over a function whose purity was never established must not be proven:
# an ordinary C++ function is not a mathematical function as far as C++L knows.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/unmarked.XXXXXX")

binary="$run/unmarked_law"
log="$run/unmarked_law.log"

status=0
"$CPPL" -std=c++17 "$FIXTURES/unmarked_law.cpp" -o "$binary" > "$log" 2>&1 || status=$?

if [ "$status" -eq 0 ]; then
    echo "a Law over an unverified function was accepted" >&2
    cat "$log" >&2
    exit 1
fi

grep -q "identity_returns_input" "$log"
grep -q "not marked pure" "$log"

if grep -q "PROVEN" "$log"; then
    echo "an unresolved Law was described as proven" >&2
    exit 1
fi

echo "a Law over an unverified function stays unresolved"
