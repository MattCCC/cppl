#!/usr/bin/env bash
set -euo pipefail

CPPL="$1"
WORK="$3"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/rejected-contracts.XXXXXX")

reject() {
    local name="$1" pattern="$2" source="$3"
    printf '%s\n' "$source" > "$run/$name.cpp"
    if "$CPPL" -std=c++17 -c "$run/$name.cpp" -o "$run/$name.o" \
        --cppl-trust-report > "$run/$name.report" 2> "$run/$name.err"; then
        echo "accepted unsupported or false contract: $name" >&2
        exit 1
    fi
    test ! -e "$run/$name.o"
    grep -Eq "$pattern" "$run/$name.err"
}

reject wrong_body 'does not satisfy its contract' \
    'verified unsigned f(unsigned x) ensures(result == x) { return 0u; }'
reject unused_result 'does not satisfy its contract' \
    'verified unsigned f(unsigned x) ensures(x == 0u) { return x; }'
reject wrong_premise 'does not satisfy its contract' \
    'verified unsigned f(unsigned x) expects(x == 1u) ensures(result == 0u) { return x; }'
reject parameter_capture 'does not satisfy its contract' \
    'verified unsigned f(unsigned x, unsigned y) ensures(result == x) { return y; }'
reject bad_rewrite 'does not satisfy its contract' \
    'verified unsigned f(unsigned x) expects(x == 0u) ensures(result == 2u) { return x + 1u; }'
reject branch 'single return|return expression' \
    'verified unsigned f(unsigned x) ensures(result == x) { if (x == 0u) return 0u; else return x; }'
reject loop 'single return|return expression' \
    'verified unsigned f(unsigned x) ensures(result == x) { while (x) {} return x; }'
reject local 'single return|return expression' \
    'verified unsigned f(unsigned x) ensures(result == x) { unsigned y = x; return y; }'
reject multiple_returns 'single return|return expression' \
    'verified unsigned f(unsigned x) ensures(result == x) { return x; return x; }'
reject mutation 'not modeled|cannot state' \
    'verified unsigned f(unsigned x) ensures(result == x) { return ++x; }'
reject reference 'not modeled' \
    'verified unsigned f(unsigned& x) ensures(result == x) { return x; }'
reject throwing 'single return|return expression' \
    'verified unsigned f(unsigned x) ensures(result == x) { throw x; }'
reject impure_call 'not available|not declared pure' \
    'unsigned g(unsigned x) { return x; } verified unsigned f(unsigned x) ensures(result == x) { return g(x); }'
reject signed_overflow 'signed overflow' \
    'verified int f(int x) ensures(result == x + 1) { return x + 1; }'
reject conversion 'conversion.*not modeled' \
    'verified unsigned f(int x) ensures(result == 0u) { return x; }'
reject global_read 'not a parameter' \
    'unsigned g = 0u; verified unsigned f(unsigned x) ensures(result == x) { return g; }'
reject no_contract 'states no contract' \
    'verified unsigned f(unsigned x) { return x; }'
reject no_ensures 'ensures clause' \
    'verified unsigned f(unsigned x) expects(x == 0u) { return x; }'
reject duplicate_ensures 'ensures clause' \
    'verified unsigned f(unsigned x) ensures(result == x) ensures(result == 0u) { return x; }'
reject duplicate_expects 'expects clause' \
    'verified unsigned f(unsigned x) expects(x == 0u) expects(x == 1u) ensures(result == x) { return x; }'
reject declaration_only 'not defined here' \
    'verified unsigned f(unsigned x) ensures(result == x);'
reject runtime_result 'undeclared identifier.*result' \
    'verified unsigned f(unsigned x) ensures(result == x) { return result; }'
reject result_in_expects 'undeclared identifier.*result' \
    'verified unsigned f(unsigned x) expects(result == 0u) ensures(result == x) { return x; }'
reject method 'outside namespace scope' \
    'struct S { verified unsigned f(unsigned x) ensures(result == x) { return x; } };'
reject recursive 'not available|not admitted' \
    'verified pure unsigned f(unsigned x) ensures(result == x) { return f(x); }'
reject conditional_call 'call.site precondition' \
    'verified pure unsigned g(unsigned x) expects(x == 0u) ensures(result == 0u) { return x; } verified unsigned f(unsigned x) ensures(result == x) { return g(x); }'
reject unused_result_effect 'not declared pure' \
    'unsigned g(unsigned x) { return x; } verified unsigned f(unsigned x) ensures(x == x) { return g(x); }'
reject unused_result_overflow 'signed overflow' \
    'verified int f(int x) ensures(x == x) { return x + 1; }'
reject refl_cannot_rewrite 'does not establish law' \
    'law l(unsigned x) expects(x == 0u) ensures(x + 1u == 1u); proof p(unsigned x) proves(l(x)) { refl; }'
reject bool_return 'unsupported-semantics' \
    'verified pure bool f(bool x) ensures(0u == 0u) { return x; }'
reject volatile_parameter 'not modeled' \
    'verified unsigned f(volatile unsigned x) ensures(result == 0u) { return x; }'
reject false_pure_helper 'not admitted|not available' \
    'unsigned state = 0u; pure unsigned g(unsigned x) { return state; } verified unsigned f(unsigned x) ensures(x == x) { return g(x); }'
reject conditional_helper 'not admitted|not available' \
    'verified pure unsigned g(unsigned x) expects(x == 0u) ensures(result == 0u) { return x; } pure unsigned h(unsigned x) { return g(x); } verified unsigned f(unsigned x) ensures(result == x) { return h(x); }'
reject result_parameter 'redefinition of parameter.*result' \
    'verified unsigned f(unsigned result) ensures(result == 0u) { return result; }'

echo 'false contracts and unsupported verified bodies fail closed'
