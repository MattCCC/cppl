#!/usr/bin/env bash
# SPEC: VERIFIED-013, VERIFIED-037, VERIFIED-038, VERIFIED-043
# Memory capabilities owed at verified calls.
#
# Each call in the fixture passes on a capability its caller holds, whole or in
# part, and the part is a value obligation the kernel proves. Every refused
# counterpart is in `negative/memory_capabilities.sh`.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/memory_capabilities.XXXXXX")

"$CPPL" -std=c++20 "$FIXTURES/memory_capabilities.cpp" -o "$run/program" --cppl-trust-report > "$run/report"

expect() {
    if ! grep -Eq "$1" "$run/report"; then
        echo "the trust report does not state: $1" >&2
        cat "$run/report" >&2
        exit 1
    fi
}

expect '^Function contracts proven: +5$'
expect '^Unresolved obligations: +0$'
# `m <= m`, `m - 1u <= m` and `1u <= m`: one obligation for each sized
# capability passed on, and none where both sides state one object.
expect '^Call preconditions proven: +3$'

output=$("$run/program")
if [ "$output" != "7 5" ]; then
    echo "expected the verified program to print '7 5', got '$output'" >&2
    exit 1
fi

echo 'every verified call passes on only a memory capability its caller holds'
