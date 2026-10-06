#!/usr/bin/env bash
# Conversions between `bool` and the integer types in verified code.
#
# SPEC: ARITH-008
#
# An integer converts to `bool` as whether it is nonzero, and `bool` converts to
# an integer type as 1 when true and 0 when false, implicitly or written as a
# cast. Each accepted program is run and prints what was proven; each refused
# one states a claim one of those conversions does not support.
set -euo pipefail
CPPL="$1"
WORK="$3"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/boolean-conversions.XXXXXX")

fail() {
    echo "$1" >&2
    exit 1
}

# refuse <name> <diagnostic>: the program is refused for that reason, produces
# no program, and nothing is reported proven.
refuse() {
    local name="$1" diagnostic="$2"
    cat > "$run/$name.cpp"
    echo 'int main() { return 0; }' >> "$run/$name.cpp"
    if "$CPPL" -std=c++20 "$run/$name.cpp" -o "$run/$name" --cppl-trust-report > "$run/$name.log" 2>&1; then
        cat "$run/$name.log" >&2
        fail "accepted what must be refused: $name"
    fi
    [ ! -e "$run/$name" ] || fail "$name produced a program"
    if grep -q 'C++L Trust Report' "$run/$name.log"; then
        cat "$run/$name.log" >&2
        fail "$name was reported"
    fi
    grep -qF -- "$diagnostic" "$run/$name.log" || {
        cat "$run/$name.log" >&2
        fail "$name was not refused for the stated reason: $diagnostic"
    }
}

# accept <name> <printed>: the program verifies and prints exactly <printed>.
accept() {
    local name="$1" printed="$2"
    cat > "$run/$name.cpp"
    if ! "$CPPL" -std=c++20 "$run/$name.cpp" -o "$run/$name" > "$run/$name.log" 2>&1; then
        cat "$run/$name.log" >&2
        fail "refused what should verify: $name"
    fi
    local output
    output=$("$run/$name")
    [ "$output" = "$printed" ] || fail "$name printed '$output', not '$printed'"
}


accept every_boolean_conversion "1 1 1 0 0 2" <<'CPP'
#include <cstdio>
verified unsigned count_true(bool a, bool c) ensures (result <= 2u) { unsigned n = a; n = n + c; return n; }
verified unsigned from_bool(bool b) ensures (result <= 1u) { return b; }
verified bool to_bool(unsigned x) ensures (result == (x != 0u)) { return x; }
verified bool flag(int x) ensures (result == (x != 0)) { bool b = x; return b; }
verified unsigned cast_bool(bool b) ensures (result == (b ? 1u : 0u)) { return static_cast<unsigned>(b); }
verified bool cast_to_bool(unsigned x) ensures (result == (x != 0u)) { return static_cast<bool>(x); }
int main() {
    std::printf("%u %d %d %u %d %u\n", from_bool(true), to_bool(5u), flag(-3), cast_bool(false), cast_to_bool(0u),
                count_true(true, true));
    return 0;
}
CPP

refuse false_bool_to_integer "does not satisfy its contract" <<'CPP'
verified unsigned from_bool(bool b) ensures (result == 1u) { return b; }
CPP

refuse false_integer_to_bool "does not satisfy its contract" <<'CPP'
verified bool to_bool(unsigned x) ensures (result == (x == 1u)) { return x; }
CPP

refuse false_cast_to_bool "does not satisfy its contract" <<'CPP'
verified bool cast_to_bool(int x) ensures (result == (x > 0)) { return static_cast<bool>(x); }
CPP

echo 'every conversion between bool and an integer type means what C++ defines it to'
