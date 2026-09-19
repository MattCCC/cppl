#!/usr/bin/env bash
set -euo pipefail
CPPL="$1"
WORK="$3"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/rejected-paths.XXXXXX")
reject() {
    local name="$1" pattern="$2" source="$3"
    printf '%s\n' "$source" > "$run/$name.cpp"
    if "$CPPL" -std=c++17 -c "$run/$name.cpp" -o "$run/$name.o" \
        > "$run/$name.out" 2> "$run/$name.err"; then
        echo "accepted invalid path verification: $name" >&2
        exit 1
    fi
    test ! -e "$run/$name.o"
    if ! grep -Eq "$pattern" "$run/$name.err"; then
        tail -30 "$run/$name.err" >&2
        exit 1
    fi
}
reject false_else 'return path.*does not satisfy' \
    'verified unsigned f(unsigned x) ensures(result <= 10u) { if (x <= 10u) return x; else return 11u; }'
reject false_then 'return path.*does not satisfy' \
    'verified unsigned f(unsigned x) ensures(result <= 10u) { if (x <= 10u) return 11u; return 10u; }'
reject wrong_subject 'return path.*does not satisfy' \
    'verified unsigned f(unsigned x, unsigned y) ensures(result <= 10u) { if (x <= 10u) return y; return 10u; }'
reject wrong_polarity 'return path.*does not satisfy' \
    'verified unsigned f(unsigned x) ensures(result <= 10u) { if (x <= 10u) return 10u; return x; }'
reject order_weakening 'return path.*does not satisfy' \
    'verified unsigned f(unsigned x) ensures(result <= 10u) { if (x < 10u) return x; return 10u; }'
reject order_transitivity 'return path.*does not satisfy' \
    'verified unsigned f(unsigned x) ensures(result < 20u) { if (x < 5u) return x; return 0u; }'
reject subtraction 'not modeled' \
    'verified unsigned f(unsigned x) ensures(result == x) { if (x <= 10u) return x - 0u; return x; }'
reject reassociation 'does not satisfy its contract' \
    'verified unsigned f(unsigned x, unsigned y, unsigned z) ensures(result == (x + y) + z) { return x + (y + z); }'
reject signed_add 'signed overflow' \
    'verified int f(int x) ensures(result <= 10) { if (x < 10) return x + 1; return 10; }'
reject missing_return 'every path must return' \
    'verified unsigned f(unsigned x) ensures(result <= 10u) { if (x <= 10u) return x; }'
reject effect_guard 'not modeled' \
    'verified unsigned f(unsigned x) ensures(result <= 10u) { if (++x <= 10u) return x; return 10u; }'
reject effect_arm 'assigning to parameter' \
    'verified unsigned f(unsigned x) ensures(result <= 10u) { if (x <= 10u) { x = 0u; return x; } return 10u; }'
reject conversion 'conversion.*not modeled' \
    'verified unsigned f(unsigned x) ensures(result <= 10u) { if (x <= 10) return x; return 10u; }'
reject float_comparison 'not modeled' \
    'verified unsigned f(float x) ensures(result <= 10u) { if (x <= 10.0f) return 0u; return 10u; }'
reject short_circuit 'not modeled' \
    'verified unsigned f(unsigned x) ensures(result <= 10u) { if (x <= 10u && x != 0u) return x; return 10u; }'
reject switch_statement 'only if/else' \
    'verified unsigned f(unsigned x) ensures(result <= 10u) { switch (x) { case 0: return 0u; default: return 10u; } }'
reject if_initializer 'only if/else' \
    'verified unsigned f(unsigned x) ensures(result <= 10u) { if (unsigned y = x; y <= 10u) return y; return 10u; }'
reject branch_call_precondition 'call-site precondition' \
    'verified unsigned g(unsigned x) expects(x <= 10u) ensures(result <= 10u) { return x; } verified unsigned f(unsigned x) ensures(result <= 10u) { if (x <= 10u) return 10u; else return g(x); }'
reject future_guard 'call-site precondition' \
    'verified unsigned g(unsigned x) expects(x <= 10u) ensures(result == x) { return x; } verified unsigned f(unsigned x) ensures(result <= 10u) { if (g(x) <= 10u) return x; return 10u; }'
reject boolean_false_arm 'return path.*does not satisfy' \
    'verified unsigned f(bool b) ensures(result == 1u) { if (b) return 1u; return 2u; }'
reject sibling_hypothesis 'return path.*does not satisfy' \
    'verified unsigned f(unsigned x, unsigned y) ensures(result <= 10u) { if (x <= 10u) { if (y <= 10u) return x; return y; } return 10u; }'
reject contradictory_path 'return path.*does not satisfy' \
    'verified unsigned f(unsigned x) ensures(result <= 10u) { if (x <= 10u) { if (x > 10u) return 11u; return x; } return 10u; }'
reject sibling_summary 'return path.*does not satisfy' \
    'verified unsigned g(unsigned x) ensures(result == 0u) { return 0u; } verified unsigned f(unsigned x) ensures(result == 0u) { if (x == 0u) return g(x); return x; }'
reject failed_callee_path 'callee.*not proven' \
    'verified unsigned g(unsigned x) ensures(result <= 10u) { if (x <= 10u) return x; return 11u; } verified unsigned f(unsigned x) ensures(result <= 10u) { return g(x); }'
reject pure_branch 'single return|not available' \
    'pure unsigned g(unsigned x) { if (x <= 10u) return x; return 10u; } law l(unsigned x) ensures(g(x) <= 10u);'
reject recursive_branch 'recursive|not available' \
    'verified unsigned f(unsigned x) ensures(result <= 10u) { if (x <= 10u) return x; return f(x); }'
# A guard holds only inside its arm. Each case below is rejected at the one
# path whose goal is false, so the rejection is the leak and not an
# incompleteness elsewhere in the body.
reject join_leak "return path 'f path 2'" \
    'verified unsigned f(unsigned x) ensures(result <= 10u) { if (x <= 10u) {} return x; }'
reject nested_join_leak "return path 'f path 2'" \
    'verified unsigned f(unsigned x, unsigned y) ensures(result <= 10u) { if (x <= 10u) { if (y <= 10u) {} } return y; }'
reject else_if_tail "return path 'f path 3'" \
    'verified unsigned f(unsigned x) ensures(result <= 10u) { if (x <= 10u) return x; else if (x <= 20u) return 10u; return 11u; }'
reject double_negation "return path 'f path 1'" \
    'verified unsigned f(unsigned x) ensures(result <= 10u) { if (!!(x <= 10u)) return 11u; return 0u; }'
# A call in a guard is evaluated before its guard holds, whatever the polarity.
reject negated_guard_call 'call-site precondition' \
    'verified unsigned g(unsigned x) expects(x <= 10u) ensures(result == x) { return x; } verified unsigned f(unsigned x) ensures(result <= 10u) { if (!(g(x) <= 10u)) return 10u; return 0u; }'
reject call_in_false_arm 'call-site precondition' \
    'verified unsigned g(unsigned x) expects(x == 0u) ensures(result == 0u) { return x; } verified unsigned f(unsigned x) ensures(result == 0u) { if (x == 0u) return 0u; return g(x); }'
echo 'false paths, leaked evidence, unreachable paths, and unsupported semantics fail closed'
