#!/usr/bin/env bash
set -euo pipefail
CPPL="$1"
WORK="$3"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/rejected-calls.XXXXXX")
reject() {
    local name="$1" pattern="$2" source="$3"
    printf '%s\n' "$source" > "$run/$name.cpp"
    if "$CPPL" -std=c++17 -c "$run/$name.cpp" -o "$run/$name.o" \
        > "$run/$name.out" 2> "$run/$name.err"; then
        echo "accepted invalid call composition: $name" >&2
        exit 1
    fi
    test ! -e "$run/$name.o"
    if ! grep -Eq "$pattern" "$run/$name.err"; then
        tail -30 "$run/$name.err" >&2
        exit 1
    fi
}
reject missing_precondition 'call-site precondition' \
    'verified unsigned g(unsigned x) expects (x == 0u) ensures (result == x) { return x; } verified unsigned f(unsigned x) ensures (result == x) { return g(x); }'
reject false_precondition 'call-site precondition' \
    'verified unsigned g(unsigned x) expects (x == 0u) ensures (result == x) { return x; } verified unsigned f() ensures (result == 1u) { return g(1u); }'
reject ignored_result 'call-site precondition' \
    'verified unsigned g(unsigned x) expects (x == 0u) ensures (result == x) { return x; } verified unsigned f(unsigned x) ensures (x == x) { return g(x); }'
reject outer_precondition 'call-site precondition' \
    'verified unsigned g(unsigned x) expects (x == 0u) ensures (result == 1u) { return x + 1u; } verified unsigned f(unsigned x) expects (x == 0u) ensures (result == 2u) { return g(g(x)); }'
reject weak_contract 'does not satisfy its contract' \
    'verified unsigned g(unsigned x) ensures (x == x) { return x; } verified unsigned f(unsigned x) ensures (result == x) { return g(x); }'
reject weak_pure_contract 'does not satisfy its contract' \
    'verified pure unsigned g(unsigned x) ensures (x == x) { return x; } verified unsigned f(unsigned x) ensures (result == x) { return g(x); }'
reject false_callee 'callee.*not proven' \
    'verified unsigned g(unsigned x) ensures (result == 0u) { return x; } verified unsigned f(unsigned x) ensures (x == x) { return g(x); }'
reject argument_capture 'call-site precondition' \
    'verified unsigned g(unsigned a, unsigned b) expects (a == b) ensures (result == a) { return a; } verified unsigned f(unsigned x, unsigned y) expects (x == 0u) ensures (result == x) { return g(x, y); }'
reject wrong_overload 'call-site precondition' \
    'verified unsigned g(unsigned x) expects (x == 0u) ensures (result == x) { return x; } verified int g(int x) ensures (result == x) { return x; } verified unsigned f() ensures (result == 1u) { return g(1u); }'
# SPEC: TERMINATION-007
# Recursion without a measure would suppose the contract it is proving.
reject recursion 'it calls itself and states no measure' \
    'verified unsigned f(unsigned x) ensures (result == x) { return f(x); }'
reject mutual_recursion 'which reaches it again, and it states no measure' \
    'unsigned g(unsigned); verified unsigned f(unsigned x) ensures (result == x) { return g(x); } verified unsigned g(unsigned x) ensures (result == x) { return f(x); }'
reject argument_effect 'not modeled' \
    'verified unsigned g(unsigned x) ensures (result == x) { return x; } verified unsigned f(unsigned x) ensures (result == x) { return g(x++); }'
reject future_summary 'call-site precondition' \
    'verified unsigned g(unsigned x) expects (x == 0u) ensures (result == 0u) { return x; } verified unsigned f(unsigned x) ensures (result == 0u) { return g(g(x)); }'

# blocked <name> <count> <callee>: in the refused case <name>, each of the
# <count> obligations composed after a call to <callee> whose precondition is
# unproven is refused for that reason, naming <callee>. That is the call
# composition's gate. Without it the refusals remain but name an obligation
# number instead, and the diagnostic is part of the gate's contract
# (docs/MUTATION_TESTING.md 5).
blocked() {
    local name="$1" count="$2" callee="$3" found
    found=$(grep -cE "note: the kernel did not accept the evidence: call-site precondition for '$callee' is not proven\$" \
        "$run/$name.err" || true)
    if [ "$found" -ne "$count" ] || grep -q 'cannot make progress' "$run/$name.err"; then
        cat "$run/$name.err" >&2
        echo "$name: $found obligations, not $count, are refused for the unproven precondition of $callee" >&2
        exit 1
    fi
}

# accept <name> <source>: the twin of a refused case, which differs from it only
# in establishing the precondition it left unproven, compiles.
accept() {
    local name="$1" source="$2"
    printf '%s\n' "$source" > "$run/$name.cpp"
    if ! "$CPPL" -std=c++17 -c "$run/$name.cpp" -o "$run/$name.o" > "$run/$name.out" 2> "$run/$name.err"; then
        tail -30 "$run/$name.err" >&2
        echo "refused valid call composition: $name" >&2
        exit 1
    fi
    test -s "$run/$name.o"
}

# The outer call's precondition and the contract both rest on the inner call's.
blocked future_summary 2 g
accept future_summary_twin \
    'verified unsigned g(unsigned x) expects (x == 0u) ensures (result == 0u) { return x; } verified unsigned f(unsigned x) expects (x == 0u) ensures (result == 0u) { return g(g(x)); }'
# Every stage above the innermost call names it, not the call being composed.
reject chained_summary 'call-site precondition' \
    'verified unsigned g(unsigned x) expects (x == 0u) ensures (result == 0u) { return x; } verified unsigned f(unsigned x) ensures (result == 0u) { return g(g(g(x))); }'
blocked chained_summary 3 g
accept chained_summary_twin \
    'verified unsigned g(unsigned x) expects (x == 0u) ensures (result == 0u) { return x; } verified unsigned f(unsigned x) expects (x == 0u) ensures (result == 0u) { return g(g(g(x))); }'
# A later argument's unproven call blocks the outer call, whose own refusal
# names the argument's callee, after an earlier argument's proven one.
reject later_argument 'call-site precondition' \
    'verified unsigned g(unsigned x) expects (x == 0u) ensures (result == 0u) { return x; } verified unsigned h(unsigned a, unsigned b) expects (a == b) ensures (result == a) { return a; } verified unsigned f(unsigned x) ensures (result == 0u) { return h(g(0u), g(x)); }'
blocked later_argument 2 g
accept later_argument_twin \
    'verified unsigned g(unsigned x) expects (x == 0u) ensures (result == 0u) { return x; } verified unsigned h(unsigned a, unsigned b) expects (a == b) ensures (result == a) { return a; } verified unsigned f(unsigned x) expects (x == 0u) ensures (result == 0u) { return h(g(0u), g(x)); }'
# x <= 7 does not give x < 7: arithmetic proves order consequences, never
# strengthenings.
reject ordering_strengthening 'does not satisfy its contract' \
    'verified unsigned g(unsigned x) expects (x <= 7u) ensures (result < 7u) { return x; }'
echo 'unproved preconditions, weak summaries, and cyclic dependencies fail closed'
