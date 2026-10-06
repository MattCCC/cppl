#!/usr/bin/env bash
# SPEC: EXPR-016, SPECEXPR-002, LOOP-004
# Conditions that must be refused.
#
# Each program below is a twin of a function `fixtures/conditions.cpp` proves,
# the same body with one thing changed so that it would be proven only if the
# lowering got the meaning of `&&` and `||` wrong (C++ [expr.log.and],
# [expr.log.or]): an invariant whose cases do not cover the loop, one operand
# of a conjunction not stated, or a claim that holds of the other connective.
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

echo 'every false twin of the conditions fixture is refused, each with its reason'
