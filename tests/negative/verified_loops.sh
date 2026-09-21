#!/usr/bin/env bash
set -euo pipefail
CPPL="$1"
WORK="$3"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/rejected-loops.XXXXXX")
reject() {
    local name="$1" pattern="$2" source="$3"
    printf '%s\n' "$source" > "$run/$name.cpp"
    if "$CPPL" -std=c++17 -c "$run/$name.cpp" -o "$run/$name.o" \
        > "$run/$name.out" 2> "$run/$name.err"; then
        echo "accepted invalid loop reasoning: $name" >&2
        exit 1
    fi
    test ! -e "$run/$name.o"
    if ! grep -Eq "$pattern" "$run/$name.err"; then
        tail -30 "$run/$name.err" >&2
        exit 1
    fi
}
count='verified unsigned f(unsigned n) ensures (result == n) { unsigned i = 0u;'
# An invariant is established on entry and re-established by every iteration.
reject not_on_entry 'does not hold on entry' \
    "$count while (i < n) invariant (i == 1u) { ++i; } return i; }"
reject not_preserved 'is not preserved by an iteration' \
    "$count while (i < n) invariant (i == 0u) { ++i; } return 0u; }"
reject not_preserved_by_continue 'is not preserved by an iteration' \
    'verified unsigned f(unsigned n, bool b) ensures (result <= n) { unsigned seen = 0u; for (unsigned i = 0u; i < n; ++i) invariant (seen == i) { if (b) continue; ++seen; } return seen; }'
# A caller of a loop function proves every precondition, not only the first.
reject second_expects_unestablished 'call.site precondition|precondition.*not' \
    'verified unsigned g(unsigned s, unsigned n) expects (s == 0u) expects (n == 3u) ensures (result == 3u) { unsigned i = s; while (i < n) invariant (i <= n) { i = i + 1u; } return i; } verified unsigned f(unsigned n) ensures (result == 3u) { return g(0u, n); }'
# After the loop only the invariants and the failed condition are known.
reject head_value_leak 'return path.*does not satisfy' \
    "$count while (i < n) invariant (i <= n) { ++i; } if (i == 0u) return n; return 0u; }"
reject entry_value_leak 'return path.*does not satisfy' \
    'verified unsigned f(unsigned n) ensures (result == 0u) { unsigned i = 0u; while (i < n) invariant (i <= n) { ++i; } return i; }'
reject condition_after_exit 'return path.*does not satisfy' \
    "$count while (i < n) invariant (i <= n) { ++i; } if (i < n) return n; return 0u; }"
reject invariant_too_weak 'return path.*does not satisfy' \
    "$count while (i < n) invariant (i >= 0u) { ++i; } return i; }"
reject break_skips_the_exit_knowledge 'return path.*does not satisfy' \
    "$count while (i < n) invariant (i <= n) { if (i == 5u) break; ++i; } return i; }"
reject return_inside_loop 'return path.*does not satisfy' \
    "$count while (i < n) invariant (i <= n) { if (i == 5u) return i; ++i; } return i; }"
# A call inside the loop proves its precondition where the loop makes it.
reject call_in_body 'call-site precondition' \
    'verified unsigned step(unsigned x) expects (x < 10u) ensures (result == x + 1u) { return x + 1u; } verified unsigned f() ensures (result == 20u) { unsigned i = 0u; while (i < 20u) invariant (i <= 20u) { i = step(i); } return i; }'
reject call_in_condition 'call-site precondition' \
    'verified unsigned below(unsigned x) expects (x < 10u) ensures (result == x) { return x; } verified unsigned f(unsigned n) ensures (result == n) { unsigned i = 0u; while (below(i) < n) invariant (i <= n) { ++i; } return n; }'
# Nested loops: the inner invariant does not see through the outer head.
reject nested_invariant 'is not preserved|does not hold on entry' \
    'verified unsigned f(unsigned n) ensures (result == 0u) { unsigned t = 0u; for (unsigned r = 0u; r < n; ++r) invariant (t == 0u) { for (unsigned c = 0u; c < n; ++c) invariant (t == c) { ++t; } } return t; }'
# A loop's contract is partial correctness: it is never a total function the
# core could unfold, so nontermination cannot reach a Law or a specification.
spin='verified unsigned spin(unsigned x) ensures (result == 1u) { unsigned y = 0u; while (y == y) { ++y; } return y; }'
reject spin_in_law 'not available to the formal core' \
    "$spin law bad(unsigned x) proves (spin(x) == 1u);"
reject spin_in_contract 'not available to the formal core|cannot be stated' \
    "$spin verified unsigned g(unsigned x) ensures (result == spin(x)) { return 1u; }"
reject spin_in_pure 'single return expression|not declared pure' \
    "$spin pure unsigned h(unsigned x) { return spin(x); } law bad(unsigned x) proves (h(x) == 1u);"
reject loop_in_pure 'single return expression' \
    'pure unsigned h(unsigned n) { unsigned i = 0u; while (i < n) { ++i; } return i; } law bad(unsigned n) proves (h(n) == n);'
# Syntax and constructs outside the modeled subset fail closed.
reject invariant_outside_verified 'would not be checked' \
    'unsigned f(unsigned n) { unsigned i = 0u; while (i < n) invariant (i <= n) { ++i; } return i; }'
reject decreases_refused 'termination is not verified' \
    "$count while (i < n) invariant (i <= n) decreases (n - i) { ++i; } return i; }"
reject do_while 'do-while loops are not modeled' \
    "$count do { ++i; } while (i < n); return n; }"
reject range_for 'range-based for loops are not modeled' \
    'verified unsigned f(unsigned n) ensures (result == n) { for (char c : "ab") { } return n; }'
reject for_without_condition 'without a condition' \
    "$count for (;;) { ++i; if (i == n) return i; } }"
reject condition_declaration 'declares a variable' \
    "$count while (unsigned k = n - i) { ++i; } return n; }"
reject signed_counter 'signed type' \
    'verified int f(int n) ensures (result == n) { int i = 0; while (i < n) invariant (i <= n) { ++i; } return i; }'
reject converted_invariant 'not modeled' \
    "$count while (i < n) invariant (i) { ++i; } return n; }"
reject effect_in_invariant 'not modeled' \
    "$count while (i < n) invariant (i++ <= n) { ++i; } return n; }"
reject effect_in_condition 'not modeled' \
    "$count while (i++ < n) invariant (i <= n) { } return n; }"
reject falls_off_the_end 'every path must return' \
    "$count while (i < n) invariant (i <= n) { ++i; } }"
reject unreachable_after_break 'unreachable trailing statements' \
    "$count while (i < n) invariant (i <= n) { break; ++i; } return n; }"
echo 'false invariants, leaked loop values, and unmodeled loops fail closed'
