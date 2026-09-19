#!/usr/bin/env bash
# A false Law is rejected by the kernel. Nothing is produced, and the result is
# never reported as proven.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/invalid.XXXXXX")

binary="$run/false_law"
log="$run/false_law.log"

status=0
"$CPPL" -std=c++17 "$FIXTURES/false_law.cpp" -o "$binary" > "$log" 2>&1 || status=$?

if [ "$status" -eq 0 ]; then
    echo "a false Law was accepted" >&2
    cat "$log" >&2
    exit 1
fi

if [ -e "$binary" ]; then
    echo "a program was produced for a false Law" >&2
    exit 1
fi

grep -q "is not proven" "$log"
grep -q "kernel-rejection" "$log"
grep -q "definitionally equal" "$log"

if grep -q "PROVEN" "$log"; then
    echo "a rejected Law was described as proven" >&2
    exit 1
fi

echo "the false Law is rejected by the kernel and no program is produced"
