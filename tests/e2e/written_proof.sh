#!/usr/bin/env bash
# A developer writes the proof. It is elaborated into explicit proof evidence,
# the kernel checks that evidence, the proof is erased, and the remaining
# program is compiled and runs.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/proof.XXXXXX")

binary="$run/written_proof"
report="$run/written_proof.report"

"$CPPL" -std=c++17 "$FIXTURES/written_proof.cpp" -o "$binary" --cppl-trust-report > "$report"

# refl, apply and exact each close a Law, and every one of them was closed by
# the evidence the author wrote rather than by the compiler's own strategy.
grep -Eq "^Laws proven: +3$" "$report"
grep -Eq "^ +by a written proof: +3$" "$report"
grep -Eq "^Unresolved obligations: +0$" "$report"
grep -Eq "^Laws trusted: +0$" "$report"
grep -Eq "^Trusted external axioms: +0$" "$report"

output=$("$binary")
if [ "$output" != "41" ]; then
    echo "expected the verified program to print 41, got '$output'" >&2
    exit 1
fi

# The same unit, under each target standard the compiler supports.
for standard in c++20 c++23; do
    "$CPPL" "-std=$standard" "$FIXTURES/written_proof.cpp" -o "$run/written_proof_$standard" \
        --cppl-trust-report > "$run/report_$standard"
    grep -Eq "^ +by a written proof: +3$" "$run/report_$standard"
    "$run/written_proof_$standard" > /dev/null
done

echo "the written proofs are checked by the kernel, erased, and the program runs"
