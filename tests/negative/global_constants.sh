#!/usr/bin/env bash
# Constants a verified body reads that it does not declare.
#
# SPEC: ARITH-008
#
# A namespace-scope or static constant, `constexpr` or `const` and not
# `volatile`, holds the value its constant initializer gives it for the whole
# run, since writing it is undefined behavior; that value is the one Clang
# computes for the target. An unscoped enumeration's value converts implicitly
# to an integer type as a value of its underlying type. Each accepted program is
# run and prints what was proven; each refused one would be proven if a
# constant were assumed to hold a value it need not hold.
set -euo pipefail
CPPL="$1"
WORK="$3"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/global-constants.XXXXXX")

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


accept every_constant_form "100 50 7 3 9 1" <<'CPP'
#include <cstdio>
constexpr unsigned kMax = 100u;
const unsigned kLimit = 50u;
enum { kCount = 7 };
namespace cfg { inline constexpr unsigned depth = 3u; }
struct Limits { static constexpr unsigned cap = 9u; };
constexpr bool kOn = true;
verified unsigned clamped(unsigned x) ensures (result <= 100u) { return x < kMax ? x : kMax; }
verified unsigned limit() ensures (result == 50u) { return kLimit; }
verified unsigned count() ensures (result == 7u) { return kCount; }
verified unsigned depth() ensures (result == 3u) { return cfg::depth; }
verified unsigned cap() ensures (result == 9u) { return Limits::cap; }
verified unsigned on() ensures (result == 1u) { return kOn ? 1u : 0u; }
int main() {
    std::printf("%u %u %u %u %u %u\n", clamped(500u), limit(), count(), depth(), cap(), on());
    return 0;
}
CPP

# An unscoped enumeration is a value of its underlying type, compared and
# converted as one.
accept unscoped_enumeration_values "1 2" <<'CPP'
#include <cstdio>
enum Color : unsigned { Red, Green };
enum Plain { Off, On };
verified unsigned code(Color c) ensures (result == c) { return c; }
verified unsigned bump(Plain p) expects (p == On) ensures (result == 2u) { return p + 1u; }
int main() {
    std::printf("%u %u\n", code(Green), bump(On));
    return 0;
}
CPP

refuse false_claim_about_a_constant "does not satisfy its contract" <<'CPP'
constexpr unsigned kMax = 100u;
verified unsigned limit() ensures (result == 101u) { return kMax; }
CPP

refuse false_claim_about_an_enumerator "does not satisfy its contract" <<'CPP'
enum { kCount = 7 };
verified unsigned count() ensures (result == 8u) { return kCount; }
CPP

# An unscoped enumeration's object holds any value of its underlying type, not
# only its enumerators.
refuse enumeration_value_not_only_its_enumerators "does not satisfy its contract" <<'CPP'
enum Color : unsigned { Red, Green };
verified unsigned code(Color c) ensures (result <= 1u) { return c; }
CPP

refuse runtime_initialized_constant "'kSeed' is a constant whose value Clang cannot compute" <<'CPP'
unsigned seed();
const unsigned kSeed = seed();
verified unsigned read_seed() ensures (true) { return kSeed; }
unsigned seed() { return 4u; }
CPP

refuse mutable_global "'counter' is not a parameter or local of the enclosing declaration" <<'CPP'
unsigned counter = 3u;
verified unsigned read_counter() ensures (result == 3u) { return counter; }
CPP

refuse volatile_constant "implicit conversion from 'const volatile unsigned int' to 'unsigned int' is not modeled" <<'CPP'
const volatile unsigned kPort = 3u;
verified unsigned read_port() ensures (result == 3u) { return kPort; }
CPP

echo 'every constant a verified body reads is the value Clang computes, and nothing else is read as one'
