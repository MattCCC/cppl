#!/usr/bin/env bash
# The verifier's decision on each operation against the host's (SPEC.md
# ARITH-006 to ARITH-008, DEFINEDBEHAVIOR-001 to DEFINEDBEHAVIOR-003).
#
# tests/property/arithmetic_oracle.cpp writes one function per operation, over
# the edges of every integer type and values drawn from a seed, and decides from
# the host compiler's own promotions, checked builtins and division whether the
# operation is defined and what it computes. Every function it calls defined
# must verify, and the program must compute, at runtime and after erasure, the
# value each contract states; every function it calls undefined must be refused
# for its definedness, and for nothing else. The seed is printed; set
# CPPL_ARITHMETIC_SEED to replay one.
set -euo pipefail
CPPL="$1"
WORK="$3"
CLANG="$4"
ORACLE="$5"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/arithmetic-properties.XXXXXX")
seed="${CPPL_ARITHMETIC_SEED:-20260926}"

"$ORACLE" "$seed" "$run/defined.cpp" "$run/undefined.cpp" "$run/undefined.names"

fail() {
    echo "seed $seed: $1" >&2
    shift
    for log in "$@"; do
        grep -E 'error' "$log" | head -20 >&2 || true
    done
    exit 1
}

# SPEC: ARITH-006, ARITH-007, ARITH-008
if ! "$CPPL" -std=c++20 "$run/defined.cpp" -o "$run/defined" --cppl-trust-report \
    "--cppl-emit-projection=$run/defined.runtime.cpp" > "$run/defined.report" 2> "$run/defined.log"; then
    fail "an operation the host defines was refused" "$run/defined.log"
fi
grep -Eq '^Unresolved obligations: +0$' "$run/defined.report" || fail "an obligation was left unresolved"
"$run/defined" || fail "a verified function computed, at runtime, another value than its contract states"
"$CLANG" -std=c++20 -x c++-cpp-output "$run/defined.runtime.cpp" -o "$run/erased"
"$run/erased" || fail "the erased program computed another value"

# SPEC: DEFINEDBEHAVIOR-001, DEFINEDBEHAVIOR-002, DEFINEDBEHAVIOR-003
status=0
"$CPPL" -std=c++20 "$run/undefined.cpp" -o "$run/undefined" > "$run/undefined.log" 2>&1 || status=$?
if [ "$status" -eq 0 ] || [ -e "$run/undefined" ]; then
    fail "an operation the host leaves undefined was accepted"
fi
while IFS= read -r name; do
    if ! grep -qF "an operation in verified function '$name' is not shown to have defined behavior" \
        "$run/undefined.log"; then
        fail "'$name' was not refused for its definedness" "$run/undefined.log"
    fi
done < "$run/undefined.names"
if grep -E 'error' "$run/undefined.log" | grep -vqF 'is not shown to have defined behavior'; then
    fail "an undefined operation was refused for another reason" "$run/undefined.log"
fi
echo "seed $seed: $(grep -c . "$run/undefined.names") undefined operations refused, and every defined one verified with its value"
