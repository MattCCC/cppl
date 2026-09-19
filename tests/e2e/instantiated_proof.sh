#!/usr/bin/env bash
# Written evidence instantiated at a term. The instantiation is a kernel rule,
# the resulting proof term is checked by the kernel, the proof syntax is erased,
# and the remaining program is compiled and runs.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/instantiated.XXXXXX")

binary="$run/instantiated_proof"
report="$run/instantiated_proof.report"

"$CPPL" -std=c++17 "$FIXTURES/instantiated_proof.cpp" -o "$binary" --cppl-trust-report > "$report"

# Seven laws: the general ones, and the instances reached only by instantiating
# them. Every one closed by the evidence the author wrote.
grep -Eq "^Laws proven: +7$" "$report"
grep -Eq "^ +by a written proof: +7$" "$report"
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
    "$CPPL" "-std=$standard" "$FIXTURES/instantiated_proof.cpp" \
        -o "$run/instantiated_proof_$standard" --cppl-trust-report > "$run/report_$standard"
    grep -Eq "^ +by a written proof: +7$" "$run/report_$standard"
    "$run/instantiated_proof_$standard" > /dev/null
done

echo "instantiated evidence is checked by the kernel, erased, and the program runs"
