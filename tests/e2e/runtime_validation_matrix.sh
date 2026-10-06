#!/usr/bin/env bash
# SPEC: RUNTIMECHECK-011, RUNTIMECHECK-012, RUNTIMECHECK-014, RUNTIMECHECK-018, RUNTIMECHECK-019
# SPEC: RUNTIMECHECK-021, ERASE-012
# TRUST.md 26.3, 36.3, TCB-RUNTIMECHK-006
#
# The accepted twins of `negative/runtime_validation_matrix.sh`: a validation
# expression in every position RUNTIMECHECK-019 admits (the condition of an
# `if`, of a conditional operator and of a loop, an operand of `&&`, `||` and
# `!`, a `bool` local, a returned value, an argument), of a parameter, a member,
# an element's copy, under two refinements at once, and with its fact kept by a
# write, a loop or a call that leaves the tested value. Every contract is
# proven; every site is reported RUNTIME-CHECKED and every claim resting on one
# names it; and the program, run with valid, invalid and boundary input, takes
# the path each validation selects.
set -euo pipefail
CPPL="$1"
WORK="$3"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/runtime-validation-matrix.XXXXXX")
fail() {
    echo "$1" >&2
    exit 1
}
unit="$run/accepted.cpp"
calls=""
expected=""
cases=()
prelude() {
    cat <<'CPP'
#include <cstddef>
#include <cstdio>
#include <vector>
type Positive = int where (self > 0);
type Small = unsigned where (self < 10u);
type Even = unsigned where (self % 2u == 0u);
type Above = unsigned where (self > 7u);
struct Reading {
    int level;
    int raw;
};
void poke(int* p) { *p = -1; }
verified void zero(int& x) ensures (x == 0) { x = 0; }
verified int keep(int x) ensures (result == x) { return x; }
verified bool truth(bool b) ensures (true) { return b; }
CPP
}
prelude > "$unit"
# accepted <name> <calls>: the case's function follows on stdin; each call is
# '<arguments>=<result>', and the program prints the result of each.
accepted() {
    local name="$1" call arguments result
    shift
    cat >> "$unit"
    cases+=("$name")
    for call in "$@"; do
        arguments="${call%=*}"
        result="${call##*=}"
        calls="$calls    std::printf(\"%lld \", static_cast<long long>($name($arguments)));"$'\n'
        expected="$expected$result "
    done
}
accepted pos_if "7=7" "1=1" "0=1" "-1=1" "-2147483647 - 1=1" <<'CPP'
verified int pos_if(int raw)
    ensures (result > 0)
{
    if (validate<Positive>(raw)) {
        Positive p = raw;
        return p;
    }
    return 1;
}

CPP
accepted pos_if_not_else "7=7" "0=1" "-5=1" <<'CPP'
verified int pos_if_not_else(int raw)
    ensures (result > 0)
{
    if (!validate<Positive>(raw)) {
        return 1;
    } else {
        Positive p = raw;
        return p;
    }
}

CPP
accepted pos_not_not "3=3" "-3=1" <<'CPP'
verified int pos_not_not(int raw)
    ensures (result > 0)
{
    if (!!validate<Positive>(raw)) {
        Positive p = raw;
        return p;
    }
    return 1;
}

CPP
accepted pos_conditional "9=9" "0=1" <<'CPP'
verified int pos_conditional(int raw)
    ensures (result > 0)
{
    const int q = validate<Positive>(raw) ? raw : 1;
    Positive p = q;
    return p;
}

CPP
accepted pos_and_left "5=5" "999=999" "1000=1" "-5=1" <<'CPP'
verified int pos_and_left(int raw)
    ensures (result > 0)
{
    if (validate<Positive>(raw) && raw < 1000) {
        Positive p = raw;
        return p;
    }
    return 1;
}

CPP
accepted pos_and_right "5=5" "1000=1" "-5=1" <<'CPP'
verified int pos_and_right(int raw)
    ensures (result > 0)
{
    if (raw < 1000 && validate<Positive>(raw)) {
        Positive p = raw;
        return p;
    }
    return 1;
}

CPP
accepted pos_or_failure_leaves "5=5" "1001=1" "-5=1" <<'CPP'
verified int pos_or_failure_leaves(int raw)
    ensures (result > 0)
{
    if (!validate<Positive>(raw) || raw > 1000) {
        return 1;
    }
    Positive p = raw;
    return p;
}

CPP
accepted pos_bool_local "6=6" "-6=1" <<'CPP'
verified int pos_bool_local(int raw)
    ensures (result > 0)
{
    const bool ok = validate<Positive>(raw);
    if (ok) {
        Positive p = raw;
        return p;
    }
    return 1;
}

CPP
accepted pos_bool_local_negated "6=6" "-6=1" <<'CPP'
verified int pos_bool_local_negated(int raw)
    ensures (result > 0)
{
    const bool bad = !validate<Positive>(raw);
    if (bad) {
        return 1;
    }
    Positive p = raw;
    return p;
}

CPP
accepted pos_loop_condition "3=1" "-4=1" "5=1" <<'CPP'
verified int pos_loop_condition(int raw)
    ensures (result > 0)
{
    int v = raw;
    int last = 1;
    while (validate<Positive>(v))
        invariant (last > 0)
        decreases (static_cast<unsigned>(v))
    {
        Positive p = v;
        last = p;
        v = v - 1;
    }
    return last;
}

CPP
accepted pos_loop_condition_and "3=2" "-4=1" "5=2" <<'CPP'
verified int pos_loop_condition_and(int raw)
    ensures (result > 0)
{
    int v = raw;
    int last = 1;
    while (validate<Positive>(v) && v > 1)
        invariant (last > 0)
        decreases (static_cast<unsigned>(v))
    {
        Positive p = v;
        last = p;
        v = v - 1;
    }
    return last;
}

CPP
accepted pos_returned "5=1" "0=0" <<'CPP'
verified bool pos_returned(int raw)
    ensures (true)
{
    return validate<Positive>(raw);
}

CPP
accepted pos_argument "5=1" "-1=0" <<'CPP'
verified bool pos_argument(int raw)
    ensures (true)
{
    return truth(validate<Positive>(raw));
}

CPP
accepted pos_twice "2=2" "-2=1" <<'CPP'
verified int pos_twice(int raw)
    ensures (result > 0)
{
    if (validate<Positive>(raw) && validate<Positive>(raw)) {
        Positive p = raw;
        return p;
    }
    return 1;
}

CPP
accepted pos_two_refinements "9u=18" "8u=16" "10u=8" "3u=8" "4294967295u=8" <<'CPP'
verified unsigned pos_two_refinements(unsigned x)
    ensures (result > 7u && result < 20u)
{
    if (validate<Small>(x) && validate<Above>(x)) {
        Small s = x;
        Above a = x;
        return s + a;
    }
    return 8u;
}

CPP
accepted pos_member "Reading{5, 0}=5" "Reading{-5, 9}=1" <<'CPP'
verified int pos_member(Reading r)
    ensures (result > 0)
{
    if (validate<Positive>(r.level)) {
        Positive p = r.level;
        return p;
    }
    return 1;
}

CPP
accepted pos_vector_element_copy "std::vector<int>{3, -1}, 0ul=3" "std::vector<int>{3, -1}, 1ul=1" "std::vector<int>{3, -1}, 7ul=1" <<'CPP'
verified int pos_vector_element_copy(const std::vector<int>& v, std::size_t i)
    ensures (result > 0)
{
    if (i < v.size()) {
        const int e = v[i];
        if (validate<Positive>(e)) {
            Positive p = e;
            return p;
        }
    }
    return 1;
}

CPP
accepted pos_fact_kept_by_other_write "4=4" "-4=1" <<'CPP'
verified int pos_fact_kept_by_other_write(int raw)
    ensures (result > 0)
{
    int other = 0;
    if (validate<Positive>(raw)) {
        other = 5;
        Positive p = raw;
        return p;
    }
    return 1;
}

CPP
accepted pos_fact_kept_by_loop_not_writing "4=4" "-4=1" <<'CPP'
verified int pos_fact_kept_by_loop_not_writing(int raw)
    ensures (result > 0)
{
    if (validate<Positive>(raw)) {
        unsigned k = 0u;
        while (k < 3u)
            invariant (k <= 3u)
            decreases (3u - k)
        {
            ++k;
        }
        Positive p = raw;
        return p;
    }
    return 1;
}

CPP
accepted pos_fact_kept_by_call_on_copy "4=4" "-4=1" <<'CPP'
verified int pos_fact_kept_by_call_on_copy(int raw)
    ensures (result > 0)
{
    if (validate<Positive>(raw)) {
        const int same = keep(raw);
        Positive p = raw;
        return p;
    }
    return 1;
}

CPP
accepted pos_unsigned_wrap_predicate "5u=5" "4294967295u=0" <<'CPP'
type Wrapped = unsigned where (self + 1u != 0u);
verified unsigned pos_unsigned_wrap_predicate(unsigned x)
    ensures (result + 1u != 0u)
{
    if (validate<Wrapped>(x)) {
        Wrapped w = x;
        return w;
    }
    return 0u;
}

CPP
accepted pos_or_both_routes_establish "3=3" "6=6" "-3=1" <<'CPP'
verified int pos_or_both_routes_establish(int raw)
    ensures (result > 0)
{
    if (validate<Positive>(raw) || raw > 5) {
        Positive p = raw;
        return p;
    }
    return 1;
}

CPP

{
    echo 'int main() {'
    printf '%s' "$calls"
    echo '    return 0;'
    echo '}'
} >> "$unit"
(cd "$run" && "$CPPL" -std=c++20 accepted.cpp -o accepted --cppl-trust-report) > "$run/report" 2>&1 ||
    { cat "$run/report" >&2; fail "the accepted twins were refused"; }
functions=$(grep -c '^verified ' "$unit")
sites=$(grep -o 'validate<' "$unit" | wc -l | tr -d ' ')
grep -Eq "^Function contracts proven: +$functions\$" "$run/report" ||
    { cat "$run/report" >&2; fail "not all $functions contracts are proven"; }
grep -Eq '^Unresolved obligations: +0$' "$run/report" || fail "an obligation is unresolved"
grep -Eq "^Runtime validation sites: +$sites\$" "$run/report" ||
    { cat "$run/report" >&2; fail "the $sites validations written are not each a runtime validation site"; }
[ "$(grep -c '^  RUNTIME-CHECKED: ' "$run/report")" = "$sites" ] || fail "a site is not reported RUNTIME-CHECKED"
dependent=$(sed -n '/^Runtime-check-dependent claims:/,/^[^ ]/p' "$run/report")
for name in "${cases[@]}"; do
    grep -q "^  contract of $name " <<< "$dependent" ||
        fail "the contract of $name does not name the validation it rests on"
done
printed=$("$run/accepted")
[ "$printed" = "$expected" ] || fail "the program printed '$printed', the validations select '$expected'"
echo "${#cases[@]} accepted twins proven, $sites sites RUNTIME-CHECKED, and each validation takes the path it states"
