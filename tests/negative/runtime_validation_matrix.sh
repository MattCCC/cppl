#!/usr/bin/env bash
# SPEC: RUNTIMECHECK-007, RUNTIMECHECK-011, RUNTIMECHECK-012, RUNTIMECHECK-013, RUNTIMECHECK-018
# SPEC: RUNTIMECHECK-019, RUNTIMECHECK-020, WORD-013
# TRUST.md 26.3, 36.3, TCB-RUNTIMECHK-006
#
# Runtime validation (`validate<R>(e)`), exhaustively at its edges. A
# validation's fact holds only on the path where it yielded true, of the value
# it tested, until that value's storage changes: each case below uses it on the
# failure path, through a route a disjunction or a failed conjunction takes,
# after a write, a call, an unsafe block, an alias write or a loop that writes
# it, for another value, member, element or refinement, or for a loop measure
# the failed test was to bound. Each is refused for that crossing, never made a
# site. So is a validation where no program runs it, and one of a refinement
# whose test is a formal proposition, could have undefined behavior or layers
# another refinement. The accepted twins are in
# `e2e/runtime_validation_matrix.sh`, run with valid, invalid and boundary
# input.
set -euo pipefail
CPPL="$1"
WORK="$3"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/runtime-validation-matrix.XXXXXX")
fail() {
    echo "$1" >&2
    exit 1
}

# A unit gathers refused cases, each a verified function named for its case.
# The unit must be refused and write no object; every error must carry a
# location inside a refused case, so none is collateral damage of another; and
# each case must be refused for its stated reason.
unit=""
cases=()
begin() {
    unit="$1"
    cases=()
    prelude > "$run/$unit.cpp"
}
# refused <name> <reason>: the case's function follows on stdin.
refused() {
    local first
    first=$(($(wc -l < "$run/$unit.cpp") + 1))
    cat >> "$run/$unit.cpp"
    cases+=("$1|$first|$(wc -l < "$run/$unit.cpp")|$2")
}
check() {
    local log="$run/$unit.log" status=0 entry name first last reason line owned
    echo 'int main() { return 0; }' >> "$run/$unit.cpp"
    (cd "$run" && "$CPPL" -std=c++20 -c "$unit.cpp" -o "$unit.o") > "$log" 2>&1 || status=$?
    [ "$status" -ne 0 ] || fail "unit '$unit' was accepted"
    # A refusal is an ordinary failure; a crash or a missing tool is not one.
    [ "$status" -eq 1 ] || { cat "$log" >&2; fail "unit '$unit' ended with status $status, not a refusal"; }
    [ ! -e "$run/$unit.o" ] || fail "an object was written for unit '$unit'"
    if grep -qE 'PROVEN|C\+\+L Trust Report' "$log"; then
        cat "$log" >&2
        fail "unit '$unit' was described as proven"
    fi
    if grep 'error \[' "$log" | grep -qv "^$unit\.cpp:[0-9]*:[0-9]*: error \["; then
        cat "$log" >&2
        fail "unit '$unit' has an error without a source location"
    fi
    for line in $(awk -F: -v file="$unit.cpp" '$1 == file && $4 ~ /^ error \[/ { print $2 }' "$log"); do
        owned=0
        for entry in "${cases[@]}"; do
            IFS='|' read -r name first last reason <<< "$entry"
            if [ "$line" -ge "$first" ] && [ "$line" -le "$last" ]; then
                owned=1
                break
            fi
        done
        [ "$owned" -eq 1 ] || { cat "$log" >&2; fail "unit '$unit' has an error at line $line, in no refused case"; }
    done
    for entry in "${cases[@]}"; do
        IFS='|' read -r name first last reason <<< "$entry"
        if ! awk -F: -v file="$unit.cpp" -v from="$first" -v to="$last" -v reason="$reason" '
                $1 == file && $2 >= from && $2 <= to && index($0, reason) { found = 1 }
                END { exit !found }' "$log"; then
            cat "$log" >&2
            fail "case '$name' of unit '$unit' was not refused for its stated reason: $reason"
        fi
    done
    echo "unit '$unit': ${#cases[@]} cases refused, each for its stated reason"
}

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

# --- Where a validation's fact holds and where it does not: its failure path, a route a disjunction or a failed conjunction takes, a later write, call, unsafe block, alias or loop, another value or refinement (RUNTIMECHECK-007, RUNTIMECHECK-011 to RUNTIMECHECK-013)
begin paths
refused neg_loop_condition_and "'&&' states a proposition and is not modeled as a value: this position requires a value, such as a condition a path is taken on or a loop invariant, so state each side separately" <<'CPP'
verified int neg_loop_condition_and(int raw)
    ensures (result > 0)
{
    int v = raw;
    int last = 1;
    while (validate<Positive>(v) && v > 1)
        decreases (static_cast<unsigned>(v))
    {
        Positive p = v;
        last = p;
        v = v - 1;
    }
    return last;
}

CPP
refused neg_loop_failed_validation_measure "loop measure 'neg_loop_failed_validation_measure loop at line 35 measure' is not shown to decrease on every iteration" <<'CPP'
verified int neg_loop_failed_validation_measure(int raw)
    ensures (result > 0)
{
    int v = raw;
    while (!validate<Positive>(v))
        decreases (v < 1 ? 1u : 0u)
    {
        v = 1;
    }
    Positive p = v;
    return p;
}

CPP
refused neg_failure_branch "this value is not shown to satisfy refinement type 'Positive'" <<'CPP'
verified int neg_failure_branch(int raw)
    ensures (result > 0)
{
    if (validate<Positive>(raw)) {
        return 1;
    }
    Positive p = raw;
    return p;
}

CPP
refused neg_not_true_branch "this value is not shown to satisfy refinement type 'Positive'" <<'CPP'
verified int neg_not_true_branch(int raw)
    ensures (result > 0)
{
    if (!validate<Positive>(raw)) {
        Positive p = raw;
        return p;
    }
    return 1;
}

CPP
refused neg_or_route "this value is not shown to satisfy refinement type 'Positive'" <<'CPP'
verified int neg_or_route(int raw)
    ensures (result > 0)
{
    if (validate<Positive>(raw) || raw == 0) {
        Positive p = raw;
        return p;
    }
    return 1;
}

CPP
refused neg_or_both_fail_leave "this value is not shown to satisfy refinement type 'Positive'" <<'CPP'
verified int neg_or_both_fail_leave(int raw)
    ensures (result > 0)
{
    if (validate<Positive>(raw) || raw < -5) {
        return 1;
    }
    Positive p = raw;
    return p;
}

CPP
refused neg_and_false_route "this value is not shown to satisfy refinement type 'Positive'" <<'CPP'
verified int neg_and_false_route(int raw)
    ensures (result > 0)
{
    if (validate<Positive>(raw) && raw < 1000) {
        return 1;
    }
    Positive p = raw;
    return p;
}

CPP
refused neg_conditional_false_arm "this value is not shown to satisfy refinement type 'Positive'" <<'CPP'
verified int neg_conditional_false_arm(int raw)
    ensures (result > 0)
{
    const int q = validate<Positive>(raw) ? 1 : raw;
    Positive p = q;
    return p;
}

CPP
refused neg_bool_local_reassigned "this value is not shown to satisfy refinement type 'Positive'" <<'CPP'
verified int neg_bool_local_reassigned(int raw)
    ensures (result > 0)
{
    bool ok = validate<Positive>(raw);
    ok = true;
    if (ok) {
        Positive p = raw;
        return p;
    }
    return 1;
}

CPP
refused neg_bool_local_other_value "this value is not shown to satisfy refinement type 'Positive'" <<'CPP'
verified int neg_bool_local_other_value(int raw, int other)
    ensures (result > 0)
{
    const bool ok = validate<Positive>(raw);
    if (ok) {
        Positive p = other;
        return p;
    }
    return 1;
}

CPP
refused neg_stale_after_write "this value is not shown to satisfy refinement type 'Positive'" <<'CPP'
verified int neg_stale_after_write(int raw)
    ensures (result > 0)
{
    int v = raw;
    if (validate<Positive>(v)) {
        v = v - 1;
        Positive p = v;
        return p;
    }
    return 1;
}

CPP
refused neg_stale_after_call "this value is not shown to satisfy refinement type 'Positive'" <<'CPP'
verified int neg_stale_after_call(int raw)
    ensures (result > 0)
{
    int v = raw;
    if (validate<Positive>(v)) {
        zero(v);
        Positive p = v;
        return p;
    }
    return 1;
}

CPP
refused neg_stale_after_unsafe "this value is not shown to satisfy refinement type 'Positive'" <<'CPP'
verified int neg_stale_after_unsafe(int raw)
    ensures (result > 0)
{
    int v = raw;
    if (validate<Positive>(v)) {
        unsafe {
            poke(&v);
        }
        Positive p = v;
        return p;
    }
    return 1;
}

CPP
refused neg_stale_after_alias_write "this value is not shown to satisfy refinement type 'Positive'" <<'CPP'
verified int neg_stale_after_alias_write(int raw)
    ensures (result > 0)
{
    int v = raw;
    int& alias = v;
    if (validate<Positive>(v)) {
        alias = 0;
        Positive p = v;
        return p;
    }
    return 1;
}

CPP
refused neg_stale_after_loop_writing "this value is not shown to satisfy refinement type 'Positive'" <<'CPP'
verified int neg_stale_after_loop_writing(int raw)
    ensures (result > 0)
{
    int v = raw;
    if (validate<Positive>(v)) {
        unsigned k = 0u;
        while (k < 3u)
            invariant (k <= 3u)
            decreases (3u - k)
        {
            v = 0;
            ++k;
        }
        Positive p = v;
        return p;
    }
    return 1;
}

CPP
refused neg_stale_reference_param "this value is not shown to satisfy refinement type 'Positive'" <<'CPP'
verified int neg_stale_reference_param(int& v)
    ensures (result > 0)
{
    if (validate<Positive>(v)) {
        zero(v);
        Positive p = v;
        return p;
    }
    return 1;
}

CPP
refused neg_member_other "this value is not shown to satisfy refinement type 'Positive'" <<'CPP'
verified int neg_member_other(Reading r)
    ensures (result > 0)
{
    if (validate<Positive>(r.level)) {
        Positive p = r.raw;
        return p;
    }
    return 1;
}

CPP
refused neg_element_validated_directly "this element access is not one the statement holding it formed" <<'CPP'
verified int neg_element_validated_directly(const std::vector<int>& v, std::size_t i)
    ensures (result > 0)
{
    if (i < v.size() && validate<Positive>(v[i])) {
        Positive p = v[i];
        return p;
    }
    return 1;
}

CPP
refused neg_element_other_index "this value is not shown to satisfy refinement type 'Positive'" <<'CPP'
verified int neg_element_other_index(const std::vector<int>& v, std::size_t i, std::size_t j)
    ensures (result > 0)
{
    if (i < v.size() && j < v.size()) {
        const int a = v[i];
        const int b = v[j];
        if (validate<Positive>(a)) {
            Positive p = b;
            return p;
        }
    }
    return 1;
}

CPP
refused neg_wrong_refinement "this value is not shown to satisfy refinement type 'Above'" <<'CPP'
verified unsigned neg_wrong_refinement(unsigned x)
    ensures (result > 7u)
{
    if (validate<Small>(x)) {
        Above a = x;
        return a;
    }
    return 8u;
}

CPP
refused neg_loop_exit_failed "this value is not shown to satisfy refinement type 'Positive'" <<'CPP'
verified int neg_loop_exit_failed(int raw)
    ensures (result > 0)
{
    int v = raw;
    while (validate<Positive>(v))
        decreases (static_cast<unsigned>(v))
    {
        v = v - 1;
    }
    Positive p = v;
    return p;
}

CPP
refused neg_no_validation_unproven "this value is not shown to satisfy refinement type 'Positive'" <<'CPP'
verified int neg_no_validation_unproven(int raw)
    ensures (result > 0)
{
    Positive p = raw;
    return p;
}

CPP
refused neg_side_effect_argument "operator '++' is not modeled" <<'CPP'
verified int neg_side_effect_argument(int raw)
    ensures (result > 0)
{
    int v = raw;
    if (validate<Positive>(v++)) {
        return 1;
    }
    return 1;
}

CPP
refused neg_base_mismatch "an operation in verified function 'neg_base_mismatch' is not shown to have defined behavior: unrepresentable conversion: 'x' of type 'long' is not shown to be a value of 'int', to which it is implicitly converted" <<'CPP'
verified int neg_base_mismatch(long x)
    ensures (result > 0)
{
    if (validate<Positive>(x)) {
        return 1;
    }
    return 1;
}

CPP
check

# --- Where a validation expression may not stand (RUNTIMECHECK-019)
begin positions
refused neg_in_ensures "a validation expression is checked only in the body of a verified function" <<'CPP'
verified int neg_in_ensures(int raw)
    ensures (validate<Positive>(raw))
{
    return 1;
}

CPP
refused neg_in_expects "a validation expression is checked only in the body of a verified function" <<'CPP'
verified int neg_in_expects(int raw)
    expects (validate<Positive>(raw))
    ensures (result > 0)
{
    return 1;
}

CPP
refused neg_in_invariant "a loop clause states a proposition, and a validation expression is runtime code" <<'CPP'
verified int neg_in_invariant(int raw)
    ensures (result > 0)
{
    int v = 1;
    while (v < 3)
        invariant (validate<Positive>(v))
        decreases (3u - static_cast<unsigned>(v))
    {
        v = v + 1;
    }
    return v;
}

CPP
refused neg_in_decreases "a loop clause states a proposition, and a validation expression is runtime code" <<'CPP'
verified int neg_in_decreases(int raw)
    ensures (result > 0)
{
    int v = 1;
    while (v < 3)
        invariant (v > 0)
        decreases (validate<Positive>(v) ? 3u - static_cast<unsigned>(v) : 0u)
    {
        v = v + 1;
    }
    return v;
}

CPP
refused neg_in_law "a validation expression is checked only in the body of a verified function" <<'CPP'
law validated(int x)
    proves (validate<Positive>(x) || !validate<Positive>(x));

{

}

CPP
refused neg_in_refinement "a validation expression is checked only in the body of a verified function" <<'CPP'
type Checked = int where (validate<Positive>(self));

{

}

CPP
refused neg_in_unverified "a validation expression is checked only in the body of a verified function" <<'CPP'
int unverified(int raw) {
    return validate<Positive>(raw) ? raw : 1;
}

{

}

CPP
refused neg_in_unsafe "a validation expression inside an unsafe block would not be checked" <<'CPP'
verified int neg_in_unsafe(int raw)
    ensures (result > 0)
{
    unsafe {
        if (validate<Positive>(raw)) {
            return raw;
        }
    }
    return 1;
}

CPP
refused neg_in_pure "a validation expression is checked only in the body of a verified function" <<'CPP'
pure bool checked(int raw) {
    return validate<Positive>(raw);
}

{

}

CPP
refused neg_global_initializer "a validation expression is checked only in the body of a verified function" <<'CPP'
const bool checked_at_start = validate<Positive>(3);

{

}

CPP
refused neg_unknown_refinement "'Unknown' does not name a refinement type this translation unit declares" <<'CPP'
verified int neg_unknown_refinement(int raw)
    ensures (result > 0)
{
    if (validate<Unknown>(raw)) {
        return raw;
    }
    return 1;
}

CPP
refused neg_not_a_refinement "'int' does not name a refinement type this translation unit declares" <<'CPP'
verified int neg_not_a_refinement(int raw)
    ensures (result > 0)
{
    if (validate<int>(raw)) {
        return 1;
    }
    return 1;
}

CPP
refused neg_indexed "validating a value against the indexed refinement type 'Below' is not supported" <<'CPP'
type Below(unsigned n) = unsigned where (self < n);
verified unsigned neg_indexed(unsigned x)
    ensures (result == 1u)
{
    if (validate<Below>(x)) {
        return 1u;
    }
    return 1u;
}

CPP
check

# --- A refinement whose predicate is a formal proposition, which no program evaluates (RUNTIMECHECK-020)
begin formal_predicates
refused neg_predicate_implication "refinement type 'Implied' states a formal predicate, which no validation can evaluate at run time" <<'CPP'
type Implied = int where (self > 0 -> self > -1);
verified int neg_predicate_implication(int raw)
    ensures (result > 0)
{
    if (validate<Implied>(raw)) {
        return 1;
    }
    return 1;
}

CPP
refused neg_predicate_equivalence "refinement type 'Iff' states a formal predicate, which no validation can evaluate at run time" <<'CPP'
type Iff = int where (self > 0 <-> self >= 1);
verified int neg_predicate_equivalence(int raw)
    ensures (result > 0)
{
    if (validate<Iff>(raw)) {
        return 1;
    }
    return 1;
}

CPP
refused neg_predicate_eq "refinement type 'Same' states a formal predicate, which no validation can evaluate at run time" <<'CPP'
type Same = int where (Eq<int>(self, self));
verified int neg_predicate_eq(int raw)
    ensures (result > 0)
{
    if (validate<Same>(raw)) {
        return 1;
    }
    return 1;
}

CPP
refused neg_predicate_quantifier "refinement type 'All' states a formal predicate, which no validation can evaluate at run time" <<'CPP'
type All = int where (forall (int y) { y == y });
verified int neg_predicate_quantifier(int raw)
    ensures (result > 0)
{
    if (validate<All>(raw)) {
        return 1;
    }
    return 1;
}

CPP
check

# --- A refinement whose test could itself have undefined behavior, or that layers another refinement (RUNTIMECHECK-020)
begin undefined_predicates
refused neg_predicate_constant_modulo "refinement type 'Even' cannot be validated at run time: its predicate evaluates an operation C++ defines only under a condition on its operands, so testing some value would have undefined behavior" <<'CPP'
verified unsigned neg_predicate_constant_modulo(unsigned x)
    ensures (result == 1u)
{
    if (validate<Even>(x)) {
        return 1u;
    }
    return 1u;
}

CPP
refused neg_layered "refinement type 'Big' cannot be validated at run time: its base type is itself a refinement type, whose predicate a validation of 'Big' would not test" <<'CPP'
type Big = Positive where (self > 100);
verified int neg_layered(int raw)
    ensures (result > 0)
{
    if (validate<Big>(raw)) {
        return 1;
    }
    return 1;
}

CPP
refused neg_predicate_division "refinement type 'Divides' cannot be validated at run time: its predicate evaluates an operation C++ defines only under a condition on its operands, so testing some value would have undefined behavior" <<'CPP'
type Divides = int where (100 / self > 1);
verified int neg_predicate_division(int raw)
    ensures (result > 0)
{
    if (validate<Divides>(raw)) {
        return 1;
    }
    return 1;
}

CPP
refused neg_predicate_modulo "refinement type 'Mod' cannot be validated at run time: its predicate evaluates an operation C++ defines only under a condition on its operands, so testing some value would have undefined behavior" <<'CPP'
type Mod = int where (100 % self == 0);
verified int neg_predicate_modulo(int raw)
    ensures (result > 0)
{
    if (validate<Mod>(raw)) {
        return 1;
    }
    return 1;
}

CPP
refused neg_predicate_signed_overflow "refinement type 'Next' cannot be validated at run time: its predicate evaluates an operation C++ defines only under a condition on its operands, so testing some value would have undefined behavior" <<'CPP'
type Next = int where (self + 1 > 0);
verified int neg_predicate_signed_overflow(int raw)
    ensures (result > 0)
{
    if (validate<Next>(raw)) {
        return 1;
    }
    return 1;
}

CPP
check

echo 'every validation fact is refused where it does not hold, and every validation where it cannot run'
