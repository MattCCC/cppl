#!/usr/bin/env bash
# SPEC: STMT-002, CONSTRUCT-089, CONSTRUCT-090
# `if` statements with an init-statement, a condition variable or `if
# constexpr` that must be refused.
#
# Each false claim is a twin of a function `fixtures/if_statements.cpp` proves,
# with a contract that would hold only if the lowering got C++'s semantics
# wrong (C++ [stmt.if]): an init-statement dropped, run twice, or read as the
# condition; a condition variable read as something else; a discarded `if
# constexpr` branch taken. `if consteval` is refused by name.
set -euo pipefail
CPPL="$1"
WORK="$3"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/if-negative.XXXXXX")

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

false_claim='does not satisfy its contract'

# The init-statement runs: `y` is 7 where the condition holds.
refused init_dropped "$false_claim" <<'CPP'
verified unsigned expression_init(unsigned x)
    expects (x < 100u)
    ensures (x != 0u || result == 0u)
{
    unsigned y = 0u;
    if (y = 7u; x == 0u) {
        return y;
    }
    return x;
}
int main() { return 0; }
CPP

# What the init-statement declares is the same value in the else branch.
refused declared_init_forgotten_in_else "$false_claim" <<'CPP'
verified unsigned declared_init(unsigned x)
    expects (x < 100u)
    ensures (x > 4u || result == 0u)
{
    if (unsigned v = x + 1u; v > 5u) {
        return v;
    } else {
        return v + v;
    }
}
int main() { return 0; }
CPP

# The init-statement's call runs once: `c` is advanced by one, not two.
refused init_call_run_twice "$false_claim" <<'CPP'
verified unsigned bump(unsigned& c, unsigned before)
    expects (c == before && before < 100u)
    ensures (c == before + 1u && result == before)
{
    c = c + 1u;
    return before;
}
verified unsigned call_init(unsigned start)
    expects (start < 10u)
    ensures (start != 0u || result == 2u)
{
    unsigned c = start;
    if (unsigned was = bump(c, c); was == 0u) {
        return c;
    }
    return c + 10u;
}
int main() { return 0; }
CPP

# The condition is the call, not the init-statement: this claim held only
# while the init-statement was read as the condition.
refused init_read_as_condition "$false_claim" <<'CPP'
verified bool above(unsigned x)
    ensures (true)
{
    return x > 3u;
}
verified unsigned init_then_call(unsigned x)
    ensures (result == 7u)
{
    unsigned y = 1u;
    if (y = 7u; above(x)) {
        y = 0u;
    }
    return y;
}
int main() { return 0; }
CPP

# A condition variable holds the condition's value: zero takes the else path.
refused condition_variable_misread "$false_claim" <<'CPP'
verified unsigned condition_variable(unsigned x)
    expects (x < 100u)
    ensures (result == x)
{
    if (unsigned copy = x) {
        return copy;
    }
    return 50u;
}
int main() { return 0; }
CPP

# Only the branch the constant condition selects runs, so `pick<4>` adds 4.
refused discarded_branch_taken "$false_claim" <<'CPP'
template <unsigned N>
verified unsigned pick(unsigned x)
    expects (x < 100u)
    ensures (result == x)
{
    if constexpr (N > 1u) {
        return x + N;
    } else {
        return x;
    }
}
int main() { return static_cast<int>(pick<4u>(1u)); }
CPP

# Which branch of `if consteval` runs depends on whether the evaluation is a
# constant one.
refused if_consteval "an 'if consteval' statement is not modeled" c++23 <<'CPP'
verified unsigned twice(unsigned x)
    expects (x < 100u)
    ensures (result == 2u * x)
{
    if consteval {
        return x + x;
    } else {
        return 2u * x;
    }
}
int main() { return 0; }
CPP

echo 'every false claim through an if form is refused, and if consteval by name'
