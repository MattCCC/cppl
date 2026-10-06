#!/usr/bin/env bash
# SPEC: EXPR-016, SPECEXPR-002, LOOP-004
# Conditions that must be refused.
#
# Each program below is a twin of a function `fixtures/conditions.cpp` proves,
# the same body with one thing changed so that it would be proven only if the
# lowering got the meaning of `&&` and `||` wrong (C++ [expr.log.and],
# [expr.log.or]): an invariant whose cases do not cover the loop, one operand
# of a conjunction not stated, a claim that holds of the other connective, a
# case of a callee's disjunction left out, or a case split that would grant
# what neither case shows.
set -euo pipefail
CPPL="$1"
WORK="$3"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/conditions-negative.XXXXXX")

# refused <name> <diagnostic>: the program read from stdin is refused, writes no
# executable, and says why with the diagnostic named (an extended regular
# expression).
refused() {
    local name="$1" diagnostic="$2"
    cat > "$run/$name.cpp"
    if "$CPPL" -std=c++20 "$run/$name.cpp" -o "$run/$name" > "$run/$name.log" 2>&1; then
        echo "$name was accepted" >&2
        cat "$run/$name.log" >&2
        exit 1
    fi
    if [ -e "$run/$name" ]; then
        echo "$name was refused, but wrote an executable" >&2
        exit 1
    fi
    if ! grep -Eq -- "$diagnostic" "$run/$name.log"; then
        echo "$name was refused, but not with: $diagnostic" >&2
        cat "$run/$name.log" >&2
        exit 1
    fi
}

false_claim='does not satisfy its contract'

# --- `&&` and `||` in loop invariants ----------------------------------------

# Neither case holds at entry, where `i == n`.
refused invariant_cases_miss_entry "does not hold on entry" <<'CPP'
verified unsigned countdown(unsigned n, unsigned mark)
    ensures (result <= 1u)
{
    unsigned i = n;
    unsigned seen = 0u;
    while (i > 0u)
        invariant ((seen == 1u && i < n) || (seen == 0u && i < n))
        decreases (i)
    {
        if (i == mark) {
            seen = 1u;
        }
        i = i - 1u;
    }
    return seen;
}
int main() { return 0; }
CPP

# Both operands of the first case are stated: the flag is set where `i` is the
# mark, and `i` is not 0 after the iteration that sets it unless the mark is 1.
refused invariant_conjunction_states_both "is not preserved by an iteration" <<'CPP'
verified unsigned countdown(unsigned n, unsigned mark)
    ensures (result <= 1u)
{
    unsigned i = n;
    unsigned seen = 0u;
    while (i > 0u)
        invariant ((seen == 1u && i == 0u) || (seen == 0u && i <= n))
        decreases (i)
    {
        if (i == mark) {
            seen = 1u;
        }
        i = i - 1u;
    }
    return seen;
}
int main() { return 0; }
CPP

# A position below the limit is remembered, so the result is not always `n`.
refused remembered_claimed_unset "$false_claim" <<'CPP'
verified unsigned last_below(unsigned n, unsigned limit)
    ensures (result == n)
{
    unsigned last = n;
    for (unsigned i = 0u; i < n; ++i)
        invariant (i <= n && (last == n || last < limit))
        decreases (n - i)
    {
        if (i < limit) {
            last = i;
        }
    }
    return last;
}
int main() { return 0; }
CPP

# The invariant's disjunction is not a conjunction: what is remembered is not
# also `n`.
refused invariant_disjunction_claimed_both "is not preserved by an iteration" <<'CPP'
verified unsigned last_below(unsigned n, unsigned limit)
    ensures (true)
{
    unsigned last = n;
    for (unsigned i = 0u; i < n; ++i)
        invariant (i <= n && (last == n && last <= n))
        decreases (n - i)
    {
        if (i < limit) {
            last = i;
        }
    }
    return last;
}
int main() { return 0; }
CPP

# --- disjunctions taken apart, and decided case by case ---------------------

# The callee's disjunction has two cases each; `v` may be neither `lo` nor
# what the claim allows.
refused clamp_omits_a_case "$false_claim" <<'CPP'
template <typename T>
verified T min_of(T a, T b)
    ensures (result <= a && result <= b && (result == a || result == b))
{
    return b < a ? b : a;
}
template <typename T>
verified T max_of(T a, T b)
    ensures (result >= a && result >= b && (result == a || result == b))
{
    return a < b ? b : a;
}
verified unsigned clamp(unsigned v, unsigned lo, unsigned hi)
    expects (lo <= hi)
    ensures (lo <= result && result <= hi && (result == v || result == lo))
{
    return min_of(max_of(v, lo), hi);
}
int main() { return 0; }
CPP

refused median_of_two "$false_claim" <<'CPP'
template <typename T>
verified T min_of(T a, T b)
    ensures (result <= a && result <= b && (result == a || result == b))
{
    return b < a ? b : a;
}
template <typename T>
verified T max_of(T a, T b)
    ensures (result >= a && result >= b && (result == a || result == b))
{
    return a < b ? b : a;
}
verified int median(int a, int b, int c)
    ensures (result == a || result == b)
{
    return max_of(min_of(a, b), min_of(max_of(a, b), c));
}
int main() { return 0; }
CPP

# Where `b0 > 0u` fails the loop has not run, but `a` need not be 0.
refused snapshot_case_false_on_entry "does not hold on entry" <<'CPP'
verified unsigned gcd(unsigned a, unsigned b)
    ensures (true)
{
    ghost unsigned a0 = a;
    ghost unsigned b0 = b;
    while (b != 0u)
        invariant (b0 > 0u || (a == 0u && b == 0u))
        decreases (b)
    {
        const unsigned t = a % b;
        a = b;
        b = t;
    }
    return a;
}
int main() { return 0; }
CPP

# Splitting on the first side's order is no excluded middle: where `b0 > 0u`
# fails, the rest still has to hold, and `b0 == 1u` does not.
refused snapshot_case_rest_false "does not hold on entry" <<'CPP'
verified unsigned gcd(unsigned a, unsigned b)
    ensures (true)
{
    ghost unsigned b0 = b;
    while (b != 0u)
        invariant (b0 > 0u || b0 == 1u)
        decreases (b)
    {
        const unsigned t = a % b;
        a = b;
        b = t;
    }
    return a;
}
int main() { return 0; }
CPP

# `any` holds for `y > 0u` alone.
refused flag_on_one_operand "$false_claim" <<'CPP'
verified unsigned either(unsigned x, unsigned y)
    ensures (result == 0u || x > 0u)
{
    const bool any = x > 0u ? true : y > 0u;
    if (any) {
        return 1u;
    }
    return 0u;
}
int main() { return 0; }
CPP

# Selected the other way, the flag is 0 where only one operand holds.
refused flag_selected_as_both "$false_claim" <<'CPP'
verified unsigned either(unsigned x, unsigned y)
    ensures (result == 1u || (x == 0u && y == 0u))
{
    const bool any = x > 0u ? y > 0u : false;
    if (any) {
        return 1u;
    }
    return 0u;
}
int main() { return 0; }
CPP

# `x == 1u` adds one too.
refused bump_claims_strict "$false_claim" <<'CPP'
verified unsigned bump(unsigned count, unsigned x)
    expects (count < 100u)
    ensures (result == count || (x > 1u && result == count + 1u))
{
    return count + (x > 0u ? 1u : 0u);
}
int main() { return 0; }
CPP

echo 'every false twin of the conditions fixture is refused, each with its reason'
