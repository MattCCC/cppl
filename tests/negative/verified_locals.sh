#!/usr/bin/env bash
set -euo pipefail
CPPL="$1"
WORK="$3"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/rejected-locals.XXXXXX")
reject() {
    local name="$1" pattern="$2" source="$3"
    printf '%s\n' "$source" > "$run/$name.cpp"
    if "$CPPL" -std=c++17 -c "$run/$name.cpp" -o "$run/$name.o" \
        > "$run/$name.out" 2> "$run/$name.err"; then
        echo "accepted invalid local reasoning: $name" >&2
        exit 1
    fi
    test ! -e "$run/$name.o"
    if ! grep -Eq "$pattern" "$run/$name.err"; then
        tail -30 "$run/$name.err" >&2
        exit 1
    fi
}
# The version a return observes is the one its path established, never an
# earlier or a sibling path's.
reject stale_version 'does not satisfy its contract' \
    'verified unsigned f(unsigned x) ensures(result == x) { unsigned y = x; y = 7u; return y; }'
reject stale_initializer 'does not satisfy its contract' \
    'verified unsigned f(unsigned x) ensures(result == 1u) { unsigned y = 1u; y = x; return y; }'
reject branch_version 'return path.*does not satisfy' \
    'verified unsigned f(unsigned x, bool b) ensures(result == x) { unsigned y = x; if (b) y = 0u; return y; }'
reject false_arm_with_locals 'return path.*does not satisfy' \
    'verified unsigned f(bool b) ensures(result == 1u) { unsigned y = 1u; if (b) return y; y = 2u; return y; }'
reject sibling_arm_version 'return path.*does not satisfy' \
    'verified unsigned f(bool a, bool b) ensures(result <= 2u) { unsigned y = 0u; if (a) { if (b) y = 1u; else y = 2u; } else { y = 3u; } return y; }'
reject inner_arm_version 'return path.*does not satisfy' \
    'verified unsigned f(bool a, bool b) ensures(result <= 1u) { unsigned y = 0u; if (a) { y = 1u; if (b) y = 5u; } return y; }'
reject flag_polarity 'return path.*does not satisfy' \
    'verified unsigned f(unsigned x) ensures(result <= 10u) { bool small = x <= 10u; if (small) return 10u; return x; }'
# C++ puts a local in scope inside its own initializer.
reject self_initialization 'holds no modeled value' \
    'verified unsigned f(unsigned x) ensures(result == 0u) { unsigned y = y; return 0u; }'
# A local is not a way to move a call away from where the program makes it.
reject initializer_call_precondition 'call-site precondition' \
    'verified unsigned g(unsigned x) expects(x <= 10u) ensures(result == x) { return x; } verified unsigned f(unsigned x) ensures(result <= 10u) { unsigned y = g(x); if (x <= 10u) return y; return 10u; }'
reject assignment_call_precondition 'call-site precondition' \
    'verified unsigned g(unsigned x) expects(x <= 10u) ensures(result == x) { return x; } verified unsigned f(unsigned x) ensures(result <= 10u) { unsigned y = 0u; y = g(x); return 10u; }'
# Declarations outside the modeled subset.
reject uninitialized 'without an initializer' \
    'verified unsigned f(unsigned x) ensures(result == x) { unsigned y; y = x; return y; }'
reject static_local 'automatic storage' \
    'verified unsigned f(unsigned x) ensures(result == x) { static unsigned y = 0u; return x; }'
reject thread_local_local 'thread-local' \
    'verified unsigned f(unsigned x) ensures(result == x) { thread_local unsigned y = 0u; return x; }'
reject reference_local 'not modeled' \
    'verified unsigned f(unsigned x) ensures(result == x) { unsigned y = x; unsigned& r = y; r = 0u; return y; }'
reject volatile_local 'not modeled' \
    'verified unsigned f(unsigned x) ensures(result == x) { volatile unsigned y = x; return y; }'
reject pointer_local 'not modeled' \
    'verified unsigned f(unsigned x) ensures(result == x) { unsigned y = x; unsigned* p = &y; return y; }'
reject widening_initializer 'not modeled' \
    'verified unsigned f(unsigned x) ensures(result == x) { unsigned long y = x; return x; }'
reject narrowing_assignment 'not modeled' \
    'verified unsigned f(unsigned x, unsigned long z) ensures(result == x) { unsigned y = x; y = z; return y; }'
reject aggregate_initializer "type 'unsigned int\[2\]', which is not modeled" \
    'verified unsigned f(unsigned x) ensures(result == x) { unsigned y[2] = {x, x}; return x; }'
reject empty_braces 'single modeled value' \
    'verified unsigned f(unsigned x) ensures(result == x) { unsigned y{}; return x; }'
reject typedef_declaration 'only variable declarations' \
    'verified unsigned f(unsigned x) ensures(result == x) { typedef unsigned number; return x; }'
# Mutations outside the closed local model.
# An update is the assignment it abbreviates, and is refused wherever that
# assignment would be.
reject wrong_increment 'does not satisfy its contract' \
    'verified unsigned f(unsigned x) ensures(result == x) { unsigned y = x; ++y; return y; }'
reject wrong_compound 'does not satisfy its contract' \
    'verified unsigned f(unsigned x) ensures(result == x) { unsigned y = x; y -= 1u; return y + 2u; }'
reject division_update "compound assignment '/=' is not modeled" \
    'verified unsigned f(unsigned x) ensures(result == x) { unsigned y = x; y /= 1u; return y; }'
reject shift_update "compound assignment '<<=' is not modeled" \
    'verified unsigned f(unsigned x) ensures(result == x) { unsigned y = x; y <<= 0u; return y; }'
reject signed_increment 'signed type' \
    'verified int f(int x) ensures(result == x) { int y = x; ++y; return x; }'
reject signed_compound 'signed type' \
    'verified int f(int x) ensures(result == x) { int y = x; y += 1; return x; }'
reject narrow_increment 'after promotion' \
    'verified unsigned f(unsigned char x) ensures(result == 0u) { unsigned char y = x; ++y; return 0u; }'
reject converted_update 'not modeled' \
    'verified unsigned f(unsigned x) ensures(result == x) { unsigned y = x; y += 1; return y; }'
reject parameter_increment 'assigning to parameter' \
    'verified unsigned f(unsigned x) ensures(result == x) { x++; return x; }'
reject update_in_value 'not modeled' \
    'verified unsigned f(unsigned x) ensures(result == x) { unsigned y = x; unsigned z = ++y; return x; }'
reject negation_statement 'only if/else' \
    'verified unsigned f(unsigned x) ensures(result == x) { unsigned y = x; -y; return y; }'
reject parameter_assignment 'assigning to parameter' \
    'verified unsigned f(unsigned x) ensures(result == x) { x = 0u; return x; }'
reject chained_assignment 'not modeled' \
    'verified unsigned f(unsigned x) ensures(result == 0u) { unsigned y = 0u, z = 0u; y = z = x; return z; }'
reject assignment_in_guard 'not modeled' \
    'verified unsigned f(unsigned x) ensures(result == 0u) { unsigned y = 0u; if ((y = x) == 0u) return y; return 0u; }'
reject comma_assignments 'only if/else' \
    'verified unsigned f(unsigned x) ensures(result == 2u) { unsigned y = 0u; y = 1u, y = 2u; return y; }'
reject forwarding_reference 'not modeled' \
    'verified unsigned f(unsigned x) ensures(result == x) { unsigned y = x; auto&& r = y; return y; }'
reject lambda_capture 'not modeled' \
    'verified unsigned f(unsigned x) ensures(result == x) { unsigned y = x; auto g = [&] { y = 0u; }; g(); return y; }'
reject structured_binding 'only variable declarations' \
    'struct P { unsigned a, b; }; verified unsigned f(unsigned x) ensures(result == x) { auto [a, b] = P{x, x}; return a; }'
reject global_assignment 'not a local of this body' \
    'unsigned g = 0u; verified unsigned f(unsigned x) ensures(result == x) { g = x; return x; }'
reject global_read 'not a parameter or local' \
    'unsigned g = 0u; verified unsigned f(unsigned x) ensures(result == x) { unsigned y = g; return x; }'
# Every read repeats its local's value; a doubling chain is refused, not expanded.
chain='unsigned y0 = x;'
for i in $(seq 1 40); do chain="$chain unsigned y$i = y$((i - 1)) + y$((i - 1));"; done
reject exponential_expansion 'expands to more than' \
    "verified unsigned f(unsigned x) ensures(result == x) { $chain if (y40 == 0u) return x; return x; }"
reject sequenced_value 'not modeled' \
    'verified unsigned f(unsigned x) ensures(result == x) { unsigned y = x; unsigned z = y++ + y; return z; }'
# A value is evaluated where the local is written, read or not.
reject unread_signed_overflow 'signed type' \
    'verified int f(int x) ensures(result == x) { int y = x + 1; return x; }'
reject unread_signed_assignment 'signed type' \
    'verified int f(int x, bool b) ensures(result == x) { int y = x; if (b) y = y * 2; return x; }'
reject call_effect 'not declared pure' \
    'unsigned effect(); verified unsigned f(unsigned x) ensures(result == x) { unsigned y = effect(); return x; }'
reject out_of_scope 'undeclared identifier|not a parameter or local' \
    'verified unsigned f(unsigned x) ensures(result == x) { if (x == 0u) { unsigned y = x; } return y; }'
reject pure_local 'single return expression' \
    'pure unsigned g(unsigned x) { unsigned y = x; return y; } law l(unsigned x) ensures(g(x) == x);'
echo 'stale versions, moved calls, and unsupported declarations fail closed'
