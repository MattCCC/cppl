#!/usr/bin/env bash
# Laws stated under a precondition. The implication is a kernel proposition, the
# premise reaches the proof only through implication introduction, the resulting
# proof term is checked by the kernel, the proof syntax is erased, and the
# remaining program is compiled and runs.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/conditional.XXXXXX")

binary="$run/conditional_proof"
report="$run/conditional_proof.report"

"$CPPL" -std=c++17 "$FIXTURES/conditional_proof.cpp" -o "$binary" --cppl-trust-report > "$report"

# Six laws, four of them conditional, every one closed by the evidence the
# author wrote. A supposed premise is never a trusted one.
grep -Eq "^Laws proven: +6$" "$report"
grep -Eq "^ +by a written proof: +6$" "$report"
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
    "$CPPL" "-std=$standard" "$FIXTURES/conditional_proof.cpp" \
        -o "$run/conditional_proof_$standard" --cppl-trust-report > "$run/report_$standard"
    grep -Eq "^ +by a written proof: +6$" "$run/report_$standard"
    "$run/conditional_proof_$standard" > /dev/null
done

echo "conditional laws are proven under their premise, checked, erased, and run"
