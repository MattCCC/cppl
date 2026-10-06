#!/usr/bin/env bash
# SPEC: STMT-003, CONSTRUCT-091, CONSTRUCT-052, CONSTRUCT-096, CONSTRUCT-097
# `switch` statements and statement-level commas that must be refused.
#
# Each false claim below is a twin of a function `fixtures/switch_statements.cpp`
# proves, the same body with a contract that would hold only if the lowering got
# C++'s semantics wrong (C++ [stmt.switch], [expr.comma]): if fall-through,
# `default:`, a missing `default:`, a `break` in an inner loop or switch, a
# `continue` in a switch, a condition with an effect, or an operand of a comma
# were modeled otherwise than C++ runs them, one of these would be proven. Each
# form this implementation does not model is refused by name.
set -euo pipefail
CPPL="$1"
WORK="$3"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/switch-negative.XXXXXX")

# refused <name> <diagnostic> [standard]: the program read from stdin is
# refused, writes no executable, and says why with the diagnostic named (an
# extended regular expression).
refused() {
    local name="$1" diagnostic="$2" standard="${3:-c++20}"
    cat > "$run/$name.cpp"
    if "$CPPL" "-std=$standard" "$run/$name.cpp" -o "$run/$name" > "$run/$name.log" 2>&1; then
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

false_claim='does not satisfy its contract|is not preserved by an iteration|does not hold on entry'

# --- claims that hold only under the wrong semantics ------------------------

# 1 falls into 2, so it ends at 11, not 1.
refused ignores_fall_through "$false_claim" <<'CPP'
verified unsigned fall_through(unsigned x)
    ensures (x != 1u || result == 1u)
{
    unsigned y = 0u;
    switch (x) {
    case 1u:
        y = y + 1u;
        [[fallthrough]];
    case 2u:
        y = y + 10u;
        break;
    case 3u:
        y = y + 100u;
    }
    return y;
}
int main() { return 0; }
CPP

# `default:` written first is not taken for a value a case matches.
refused default_taken_for_a_matched_case "$false_claim" <<'CPP'
verified unsigned default_first(unsigned x)
    ensures (x != 0u || result == 7u)
{
    unsigned y = 0u;
    switch (x) {
    default:
        y = 7u;
        break;
    case 0u:
        y = 1u;
        break;
    }
    return y;
}
int main() { return 0; }
CPP

# `default:` in the middle falls into the case after it.
refused default_does_not_fall_through "$false_claim" <<'CPP'
verified unsigned default_middle(unsigned x)
    ensures (x == 1u || x == 9u || result == 2u)
{
    unsigned y = 0u;
    switch (x) {
    case 1u:
        y = 1u;
        break;
    default:
        y = 2u;
        [[fallthrough]];
    case 9u:
        y = y + 3u;
    }
    return y;
}
int main() { return 0; }
CPP

# Without `default:`, a value no case matches goes on after the switch.
refused unmatched_without_default "$false_claim" <<'CPP'
verified unsigned no_default(unsigned x)
    ensures (x == 4u || result == 0u)
{
    switch (x) {
    case 4u:
        return 40u;
    }
    return x;
}
int main() { return 0; }
CPP

# A `break` in a loop inside the switch leaves that loop, so `y = 5u` runs.
refused inner_loop_break_leaves_the_switch "$false_claim" <<'CPP'
verified unsigned loop_in_switch(unsigned x)
    ensures (x != 1u || result == 0u)
{
    unsigned y = 0u;
    switch (x) {
    case 1u: {
        unsigned k = 0u;
        while (k < 3u)
            invariant (k <= 3u)
            decreases (3u - k)
        {
            k = k + 1u;
            break;
        }
        y = 5u;
        break;
    }
    default:
        break;
    }
    return y;
}
int main() { return 0; }
CPP

# The inner switch's `break` leaves only the inner switch.
refused inner_switch_break_leaves_the_outer "$false_claim" <<'CPP'
verified unsigned nested(unsigned x, unsigned z)
    ensures (x != 1u || z != 2u || result == 2u)
{
    unsigned y = 0u;
    switch (x) {
    case 1u:
        switch (z) {
        case 2u:
            y = 2u;
            break;
        }
        y = y + 10u;
        break;
    }
    return y;
}
int main() { return 0; }
CPP

# `continue` in a switch continues the loop: an even `i` adds nothing, so the
# total is not `i`.
refused continue_as_break "$false_claim" <<'CPP'
verified unsigned odd_count(unsigned n)
    expects (n <= 1000u)
    ensures (result == n)
{
    unsigned total = 0u;
    for (unsigned i = 0u; i < n; ++i)
        invariant (i <= n && total == i)
        decreases (n - i)
    {
        switch (i % 2u) {
        case 0u:
            continue;
        default:
            break;
        }
        total = total + 1u;
    }
    return total;
}
int main() { return 0; }
CPP

# The condition is evaluated once, so `c` is advanced once: by 2 would be twice.
refused condition_evaluated_twice "$false_claim" <<'CPP'
verified unsigned bump(unsigned& c, unsigned before)
    expects (c == before && before < 100u)
    ensures (c == before + 1u && result == before)
{
    c = c + 1u;
    return before;
}
verified unsigned once(unsigned start)
    expects (start < 10u)
    ensures (start != 0u || result == 12u)
{
    unsigned c = start;
    unsigned seen = 0u;
    switch (bump(c, c)) {
    case 0u:
        seen = 10u;
        break;
    default:
        seen = 30u;
    }
    return seen + c;
}
int main() { return 0; }
CPP

# The condition's call writes through the pointer it is handed, so what `q`
# designates is unknown after the switch, not the value read before it.
refused condition_call_effects_forgotten "$false_claim" <<'CPP'
verified unsigned mark(unsigned* p)
    expects (writable(p))
    ensures (result == 1u)
{
    *p = 7u;
    return 1u;
}
verified unsigned probe(unsigned* q)
    expects (readable(q) && writable(q))
    ensures (result == 0u)
{
    unsigned before = *q;
    switch (mark(q)) {
    default:
        break;
    }
    unsigned after = *q;
    return after - before;
}
int main() { return 0; }
CPP

# Both operands of a statement-level comma run: `c = 3u` is not dropped.
refused comma_drops_an_operand "$false_claim" <<'CPP'
verified unsigned commas()
    ensures (result == 3u)
{
    unsigned a = 0u;
    unsigned b = 0u;
    unsigned c = 0u;
    a = 1u, b = 2u, c = 3u;
    return a + b + c;
}
int main() { return 0; }
CPP

# Both operands of a comma increment run: `--j` changes `j` every iteration.
refused comma_increment_drops_an_operand "$false_claim" <<'CPP'
verified unsigned count(unsigned n)
    expects (n < 100u)
    ensures (result == n)
{
    unsigned j = n;
    unsigned i = 0u;
    for (; i < n; ++i, --j)
        invariant (i <= n && j == n)
        decreases (n - i)
    {
    }
    return i;
}
int main() { return 0; }
CPP

# A case the precondition allows is not shown not to occur.
refused possible_case_claimed_impossible 'is not shown to be unreachable' <<'CPP'
pure unsigned zero() {
    return 0u;
}
proof nothing()
    proves (zero() == 0u)
{
    refl;
}
verified unsigned guarded(unsigned x)
    expects (x < 3u)
    ensures (result <= 3u)
{
    switch (x) {
    case 2u:
        contradiction nothing;
    default:
        return x;
    }
}
int main() { return 0; }
CPP

# A `break` in an unsafe block in a case would leave the block, and a verified
# body goes on after an unsafe block.
refused break_out_of_an_unsafe_block 'control leaves the unsafe block at .* through a break' <<'CPP'
unsafe unsigned sample();
verified unsigned probe(unsigned x)
    ensures (result <= 1u)
{
    switch (x) {
    case 1u:
        unsafe {
            if (sample() == 0u) {
                break;
            }
        }
        return 1u;
    }
    return 0u;
}
int main() { return 0; }
CPP

# --- forms refused by name --------------------------------------------------

# A label inside a block of the body is a way into the middle of that block.
refused label_in_a_nested_block "a 'case' or 'default' label inside a nested statement of its 'switch' is not modeled" <<'CPP'
verified unsigned probe(unsigned x)
    ensures (result <= 2u)
{
    switch (x) {
    case 1u: {
    case 2u:
        return 2u;
    }
    }
    return 0u;
}
int main() { return 0; }
CPP

# Duff's device: a label inside a loop of the body.
refused duffs_device "a 'case' or 'default' label inside a nested statement of its 'switch' is not modeled" <<'CPP'
verified unsigned probe(unsigned x)
    ensures (result <= 1000u)
{
    unsigned n = 0u;
    switch (x % 2u) {
    case 0u:
        do {
            n = n + 1u;
        case 1u:
            n = n + 1u;
        } while (n < 10u);
    }
    return n;
}
int main() { return 0; }
CPP

refused case_range "a case range, 'case low ... high:', is not modeled" <<'CPP'
verified unsigned probe(unsigned x)
    ensures (result <= 1u)
{
    switch (x) {
    case 1u ... 3u:
        return 1u;
    }
    return 0u;
}
int main() { return 0; }
CPP

# Never executed, so nothing in it is modeled.
refused statement_before_the_first_label "a statement before the first label of a 'switch' is never executed" <<'CPP'
verified unsigned probe(unsigned x)
    ensures (result <= 1u)
{
    unsigned y = 0u;
    switch (x) {
        y = 1u;
    case 1u:
        return 1u;
    }
    return y;
}
int main() { return 0; }
CPP

# libclang does not list a switch's init-statement among its parts, so it is
# found in the head and refused rather than dropped: this claim held only if
# `y = 7u` were dropped.
refused switch_init_statement "a 'switch' statement with an init-statement is not modeled" <<'CPP'
verified unsigned probe(unsigned x)
    ensures (x == 1u || result == 0u)
{
    unsigned y = 0u;
    switch (y = 7u; x) {
    case 1u:
        return 1u;
    }
    return y;
}
int main() { return 0; }
CPP

# A comma inside an expression would need its left operand modeled there.
refused nested_comma "the comma operator inside an expression is not modeled" <<'CPP'
verified unsigned probe(unsigned x)
    ensures (result == x)
{
    unsigned copy = 0u;
    copy = (copy = 1u, x);
    return copy;
}
int main() { return 0; }
CPP

# Only `[[fallthrough]]` is read as an empty statement: an assumption is not.
refused assumption_is_not_fallthrough "found 'UnexposedStmt'" c++23 <<'CPP'
verified unsigned probe(unsigned x)
    ensures (result <= 2u)
{
    switch (x) {
    case 1u:
        [[assume(x == 1u)]];
        return 1u;
    }
    return 2u;
}
int main() { return 0; }
CPP

echo 'every false claim through a switch or a comma is refused, and each unmodeled form by name'
