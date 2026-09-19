#!/usr/bin/env bash
# A true Law is proven by the kernel, erased, and the program runs natively.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/valid.XXXXXX")

binary="$run/identity_law"
report="$run/identity_law.report"
"$CPPL" -std=c++17 "$FIXTURES/identity_law.cpp" -o "$binary" --cppl-trust-report > "$report"

grep -Eq "^Laws proven: +1$" "$report"
grep -Eq "^Unresolved obligations: +0$" "$report"
grep -Eq "^Laws trusted: +0$" "$report"
grep -Eq "^Trusted external axioms: +0$" "$report"

output=$("$binary")
if [ "$output" != "41" ]; then
    echo "expected the verified program to print 41, got '$output'" >&2
    exit 1
fi

# The same pipeline over an arithmetic Law that is true by computation.
arithmetic="$run/arithmetic_law"
arithmetic_report="$run/arithmetic_law.report"
"$CPPL" -std=c++20 "$FIXTURES/true_arithmetic_law.cpp" -o "$arithmetic" --cppl-trust-report \
    > "$arithmetic_report"

grep -Eq "^Laws proven: +1$" "$arithmetic_report"
grep -Eq "^Unresolved obligations: +0$" "$arithmetic_report"
"$arithmetic"

echo "the identity Law and the arithmetic Law are PROVEN, erased and executable"
