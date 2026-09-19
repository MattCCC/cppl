#!/usr/bin/env bash
# The implementation is changed so it no longer satisfies the Law. The Law is
# kept. Verification must fail, and it must fail for a stated reason.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/broken.XXXXXX")

binary="$run/broken_identity"
log="$run/broken_identity.log"

status=0
"$CPPL" -std=c++17 "$FIXTURES/broken_identity.cpp" -o "$binary" > "$log" 2>&1 || status=$?

if [ "$status" -eq 0 ]; then
    echo "an implementation that violates its Law was accepted" >&2
    cat "$log" >&2
    exit 1
fi

if [ -e "$binary" ]; then
    echo "a program was produced for a violated Law" >&2
    exit 1
fi

# The reason is stated: signed overflow is undefined in C++ and the obligation
# that it does not occur is not part of this formal core.
grep -q "identity_returns_input" "$log"
grep -q "signed overflow" "$log"

# The failure points back at the user's own source, not at a projection.
grep -q "broken_identity.cpp:" "$log"

if grep -q "PROVEN" "$log"; then
    echo "a failed verification was described as proven" >&2
    exit 1
fi

# It also did not silently become a warning.
if ! grep -q "error" "$log"; then
    echo "verification failure was not reported as an error" >&2
    exit 1
fi

echo "the violated Law fails closed with a stated reason"
