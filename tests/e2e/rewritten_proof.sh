#!/usr/bin/env bash
# Equalities used to transform a goal. Equality substitution is a kernel rule,
# the kernel derives the transformed proposition itself, the proof syntax is
# erased, and the remaining program is compiled and runs.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/rewritten.XXXXXX")

binary="$run/rewritten_proof"
report="$run/rewritten_proof.report"

"$CPPL" -std=c++17 "$FIXTURES/rewritten_proof.cpp" -o "$binary" --cppl-trust-report > "$report"

# Five laws, every one closed by the evidence the author wrote. Four of them
# need the premise to be used rather than merely named, so none of them was
# provable before equality substitution existed.
grep -Eq "^Laws proven: +5$" "$report"
grep -Eq "^ +by a written proof: +5$" "$report"
grep -Eq "^Unresolved obligations: +0$" "$report"
grep -Eq "^Laws trusted: +0$" "$report"
grep -Eq "^Trusted external axioms: +0$" "$report"

output=$("$binary")
if [ "$output" != "41" ]; then
    echo "expected the verified program to print 41, got '$output'" >&2
    exit 1
fi

# Removing the rewrite must lose the proof: the kernel, not the elaborator, is
# what the transformed goal has to satisfy.
sed 's/^    rewrite h;$//' "$FIXTURES/rewritten_proof.cpp" > "$run/without_rewrite.cpp"
if "$CPPL" -std=c++17 "$run/without_rewrite.cpp" -o "$run/without_rewrite" \
        > "$run/without_rewrite.log" 2>&1; then
    echo "the same laws were proven without their rewrites" >&2
    exit 1
fi
grep -q "kernel-rejection" "$run/without_rewrite.log"

# The same unit, under each target standard the compiler supports.
for standard in c++20 c++23; do
    "$CPPL" "-std=$standard" "$FIXTURES/rewritten_proof.cpp" \
        -o "$run/rewritten_proof_$standard" --cppl-trust-report > "$run/report_$standard"
    grep -Eq "^ +by a written proof: +5$" "$run/report_$standard"
    "$run/rewritten_proof_$standard" > /dev/null
done

echo "equalities transform goals, the kernel derives the result, and the program runs"
