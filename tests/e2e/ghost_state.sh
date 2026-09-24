#!/usr/bin/env bash
# SPEC: GHOST-001, GHOST-002, ERASE-011, WORD-002
# Ghost state (SPEC.md 25, GRAMMAR.md 21).
#
# A ghost local may take runtime values and feed loop clauses and claims, and
# the program verifies with it; then the whole declaration leaves the program,
# which runs as though it had never been written. Where the unit uses `ghost` as
# an ordinary name, the word stays ordinary C++.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/ghost_state.XXXXXX")

binary="$run/ghost_state"
report="$run/ghost_state.report"
runtime="$run/runtime.ii"

"$CPPL" -std=c++20 "$FIXTURES/ghost_state.cpp" -o "$binary" --cppl-trust-report \
    "--cppl-emit-projection=$runtime" > "$report"

expect() {
    if ! grep -Eq "$1" "$report"; then
        echo "the trust report does not state: $1" >&2
        cat "$report" >&2
        exit 1
    fi
}

# Every contract, invariant and claim that reads a ghost is proven, and nothing
# is left open or assumed.
expect '^Function contracts proven: +4$'
expect '^Loop invariants proven: +8$'
expect '^Impossible paths proven: +1$'
expect '^Unresolved obligations: +0$'
expect '^Laws trusted: +0$'
expect '^Unsafe regions: +0$'

output=$("$binary")
if [ "$output" != "4 3 6 5" ]; then
    echo "expected the program to print '4 3 6 5', got '$output'" >&2
    exit 1
fi

# The fixture's own text in the program, after the headers it includes: no ghost
# word and no ghost name survives, and neither does any analysis scaffolding.
program=$(sed -n '/ghost_state\.cpp" 2$/,$p' "$runtime")
if [ -z "$program" ]; then
    echo 'the fixture was not found in its runtime projection' >&2
    exit 1
fi
if grep -Eq '(^|[^_[:alnum:]])(ghost|bound|before|after|expected|spare|small|digit|previous)([^_[:alnum:]]|$)' \
    <<< "$program"; then
    echo 'a ghost declaration survived into the erased translation unit' >&2
    grep -En '(ghost|bound|before|after|expected|spare|small|digit|previous)' <<< "$program" >&2
    exit 1
fi
if grep -q '__cppl_' "$runtime"; then
    echo 'analysis scaffolding reached the runtime program' >&2
    exit 1
fi

# C++ first (SPEC.md WORD-002): where `ghost` names a type, `ghost x = y;`
# declares `x`, and every such declaration stays ordinary C++, with a warning.
cat > "$run/named.cpp" <<'CPP'
struct ghost {
    unsigned value;
};

verified unsigned identity(unsigned x)
    ensures (result == x)
{
    return x;
}

int main() {
    ghost g{3u};
    return static_cast<int>(identity(g.value));
}
CPP
"$CPPL" -std=c++20 "$run/named.cpp" -o "$run/named" 2> "$run/named.log"
set +e
"$run/named"
status=$?
set -e
if [ "$status" != 3 ]; then
    echo "a unit naming a type 'ghost' did not run as ordinary C++ (exit $status)" >&2
    cat "$run/named.log" >&2
    exit 1
fi

echo 'ghost state is proven with, and leaves nothing in the program'
