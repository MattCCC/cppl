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
    'verified unsigned g(unsigned x) expects(x == 0u) ensures(result == x) { return x; } verified unsigned f(unsigned x) ensures(result == x) { return g(x); }'
reject false_precondition 'call-site precondition' \
    'verified unsigned g(unsigned x) expects(x == 0u) ensures(result == x) { return x; } verified unsigned f() ensures(result == 1u) { return g(1u); }'
reject ignored_result 'call-site precondition' \
    'verified unsigned g(unsigned x) expects(x == 0u) ensures(result == x) { return x; } verified unsigned f(unsigned x) ensures(x == x) { return g(x); }'
reject outer_precondition 'call-site precondition' \
    'verified unsigned g(unsigned x) expects(x == 0u) ensures(result == 1u) { return x + 1u; } verified unsigned f(unsigned x) expects(x == 0u) ensures(result == 2u) { return g(g(x)); }'
reject weak_contract 'does not satisfy its contract' \
    'verified unsigned g(unsigned x) ensures(x == x) { return x; } verified unsigned f(unsigned x) ensures(result == x) { return g(x); }'
reject weak_pure_contract 'does not satisfy its contract' \
    'verified pure unsigned g(unsigned x) ensures(x == x) { return x; } verified unsigned f(unsigned x) ensures(result == x) { return g(x); }'
reject false_callee 'callee.*not proven' \
    'verified unsigned g(unsigned x) ensures(result == 0u) { return x; } verified unsigned f(unsigned x) ensures(x == x) { return g(x); }'
reject argument_capture 'call-site precondition' \
    'verified unsigned g(unsigned a, unsigned b) expects(a == b) ensures(result == a) { return a; } verified unsigned f(unsigned x, unsigned y) expects(x == 0u) ensures(result == x) { return g(x, y); }'
reject wrong_overload 'call-site precondition' \
    'verified unsigned g(unsigned x) expects(x == 0u) ensures(result == x) { return x; } verified int g(int x) ensures(result == x) { return x; } verified unsigned f() ensures(result == 1u) { return g(1u); }'
reject recursion 'recursive|not available' \
    'verified unsigned f(unsigned x) ensures(result == x) { return f(x); }'
reject mutual_recursion 'recursive|not available' \
    'unsigned g(unsigned); verified unsigned f(unsigned x) ensures(result == x) { return g(x); } verified unsigned g(unsigned x) ensures(result == x) { return f(x); }'
reject argument_effect 'not modeled' \
    'verified unsigned g(unsigned x) ensures(result == x) { return x; } verified unsigned f(unsigned x) ensures(result == x) { return g(x++); }'
reject future_summary 'call-site precondition' \
    'verified unsigned g(unsigned x) expects(x == 0u) ensures(result == 0u) { return x; } verified unsigned f(unsigned x) ensures(result == 0u) { return g(g(x)); }'
reject ordering_strengthening 'does not satisfy its contract' \
    'verified unsigned g(unsigned x) expects(x < 7u) ensures(result <= 7u) { return x; }'
echo 'unproved preconditions, weak summaries, and cyclic dependencies fail closed'
