#!/usr/bin/env bash
# SPEC: UNSAFE-001, UNSAFE-002, UNSAFE-003, UNSAFE-004, UNSAFE-005, BOUNDARYEX-010, INTERACT-018
# Unsafe boundaries and what rests on them (TRUST.md TCB-UNSAFE-001 to
# TCB-UNSAFE-003, TCB-REPORT-001, TCB-REPORT-005).
#
# An unsafe block runs as ordinary C++ and establishes nothing, so a contract
# proven across it holds only as far as the block is sound. What this pins is
# that the dependency is never lost: every claim about runtime code that rests
# on an unsafe block is listed with every block it rests on, its own or one a
# verified callee holds, it is never counted as proven outright, and every
# unsafe boundary the unit writes is listed whether or not anything rests on it.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/unsafe_boundary.XXXXXX")

binary="$run/unsafe_boundary"
report="$run/unsafe_boundary.report"

"$CPPL" -std=c++20 "$FIXTURES/unsafe_boundary.cpp" -o "$binary" --cppl-trust-report \
    "--cppl-emit-projection=$run/runtime.cpp" > "$report"

expect() {
    if ! grep -Eq "$1" "$report"; then
        echo "the trust report does not state: $1" >&2
        cat "$report" >&2
        exit 1
    fi
}

# Each contract across an unsafe block is PROVEN, and relying on unsafe code;
# only the one no block reaches is proven outright. An unsafe block may not
# return, so none of them is a total-correctness claim.
expect '^Function contracts proven: +7$'
expect '^Unresolved obligations: +0$'
contracts=$(grep -A4 '^Function contracts proven:' "$report")
grep -Eq '^ +partial correctness only: +6$' <<< "$contracts"
grep -Eq '^ +assumption-free: +1$' <<< "$contracts"
grep -Eq '^ +relative to trusted laws: +1$' <<< "$contracts"
grep -Eq '^ +relying on unsafe code: +6$' <<< "$contracts"
paths=$(grep -A3 '^Impossible paths proven:' "$report")
grep -Eq '^ +assumption-free: +0$' <<< "$paths"
grep -Eq '^ +relying on unsafe code: +1$' <<< "$paths"

# Every claim resting on an unsafe block, with each block, and every claim
# resting on nothing, then every boundary the unit writes: an unsafe function
# once however often it is declared, and a block wherever it stands. Compared
# whole, so a dependency that goes missing, a block that is dropped, or an order
# that changes all fail here. Identities are content hashes, as for every claim.
sed -e "s|$FIXTURES/||g" "$report" | sed -E -e 's/, identity [0-9a-f]{16}$//' |
    sed -n '/^Unsafe-dependent claims:/,/^Unused trusted laws:/p;/^Unsafe regions:/,/^Runtime validation sites:/p' \
        > "$run/unsafe.actual"
cat > "$run/unsafe.expected" <<'REPORT'
Unsafe-dependent claims:     7
  unreachable runtime path never_seven_reading path 1 (unsafe_boundary.cpp:117)
    rests on unsafe block (unsafe_boundary.cpp:113:5), in its own body
  contract of clamped_reading (unsafe_boundary.cpp:56)
    rests on unsafe block (unsafe_boundary.cpp:52:5), in its own body
  contract of passes_reading (unsafe_boundary.cpp:65)
    rests on unsafe block (unsafe_boundary.cpp:52:5), through a verified call it makes
  contract of keeps_untouched (unsafe_boundary.cpp:77)
    rests on unsafe block (unsafe_boundary.cpp:74:5), in its own body
  contract of count_readings (unsafe_boundary.cpp:87)
    rests on unsafe block (unsafe_boundary.cpp:89:9), in its own body
  contract of store_then_log (unsafe_boundary.cpp:105)
    rests on unsafe block (unsafe_boundary.cpp:102:5), in its own body
  contract of never_seven_reading (unsafe_boundary.cpp:117)
    rests on unsafe block (unsafe_boundary.cpp:113:5), in its own body
Assumption-free claims:      1
  contract of add_one (unsafe_boundary.cpp:127)
Unused trusted laws:         0
Unsafe regions:              8
  unsafe function:         read_device (unsafe_boundary.cpp:33:17)
  unsafe function:         log_write (unsafe_boundary.cpp:40:13)
  unsafe block:            unsafe_boundary.cpp:52:5, in verified function clamped_reading
  unsafe block:            unsafe_boundary.cpp:74:5, in verified function keeps_untouched
  unsafe block:            unsafe_boundary.cpp:89:9, in verified function count_readings
  unsafe block:            unsafe_boundary.cpp:102:5, in verified function store_then_log
  unsafe block:            unsafe_boundary.cpp:113:5, in verified function never_seven_reading
  unsafe block:            unsafe_boundary.cpp:136:5
Runtime validation sites:    0
REPORT
if ! diff -u "$run/unsafe.expected" "$run/unsafe.actual" >&2; then
    echo 'the unsafe dependencies differ from what the fixture states' >&2
    exit 1
fi

# Unsafe is not trusted (SPEC.md 26.1): a claim resting on a block and on a
# trusted law is listed under both, and the block adds no trusted law.
expect '^Laws trusted: +1$'
expect '^Trusted external axioms: +1$'
trusted=$(sed -n '/^Trust-dependent claims:/,/^Unsafe-dependent claims:/p' "$report")
grep -Eq '^  contract of never_seven_reading ' <<< "$trusted"
if grep -Eq '^  contract of (clamped_reading|passes_reading|keeps_untouched|count_readings|store_then_log) ' \
    <<< "$trusted"; then
    echo 'a contract resting only on an unsafe block was reported as resting on a trusted law' >&2
    exit 1
fi

# The report is the same on every run (TRUST.md TCB-PROV-005).
"$CPPL" -std=c++20 "$FIXTURES/unsafe_boundary.cpp" -o "$run/again" --cppl-trust-report > "$run/again.report"
if ! diff -q "$report" "$run/again.report" > /dev/null; then
    echo 'the trust report is not deterministic' >&2
    diff -u "$report" "$run/again.report" >&2
    exit 1
fi

# Every block runs as written, the one in `main` and those in verified bodies
# alike (SPEC.md 26.4).
output=$("$binary")
if [ "$output" != $'wrote 7\n42 42 3 2 4 2 42 7' ]; then
    printf 'expected the program to print its readings, got\n%s\n' "$output" >&2
    exit 1
fi
# A system header may spell the word inside a string, which is not the construct.
if grep -qE '(^|[^_[:alnum:]])unsafe +(\{|unsigned|void)' "$run/runtime.cpp"; then
    echo "'unsafe' survived into the erased translation unit" >&2
    exit 1
fi

# A unit that writes no unsafe boundary lists none, and no claim of it relies on
# unsafe code.
zero="$run/zero.report"
"$CPPL" -std=c++17 "$FIXTURES/verified_functions.cpp" -o "$run/zero" --cppl-trust-report > "$zero"
grep -Eq '^Unsafe regions: +0$' "$zero"
grep -Eq '^Unsafe-dependent claims: +0$' "$zero"
if grep -Eq '^ +relying on unsafe code: +[1-9]' "$zero"; then
    echo 'a unit without unsafe code reported a claim relying on it' >&2
    cat "$zero" >&2
    exit 1
fi

# One unsafe function, declared in a header and included by two units. Each unit
# lists it where the header declares it.
mkdir -p "$run/units"
cat > "$run/units/device.hpp" <<'CPP'
#pragma once
unsafe unsigned read_port();
CPP
cat > "$run/units/port.cpp" <<'CPP'
#include "device.hpp"
unsafe unsigned read_port() { return 5u; }
CPP
cat > "$run/units/user.cpp" <<'CPP'
#include "device.hpp"
verified unsigned bounded() ensures (result <= 9u) {
    unsigned x = 0u;
    unsafe { x = read_port(); }
    if (x > 9u) { return 9u; }
    return x;
}
int main() { return static_cast<int>(bounded()); }
CPP
units="$run/units.report"
"$CPPL" -std=c++20 -I"$run/units" "$run/units/port.cpp" "$run/units/user.cpp" -o "$run/units/program" \
    --cppl-trust-report > "$units"
grep -Eq '^Unsafe regions: +3$' "$units"
if [ "$(grep -cE '^  unsafe function: +read_port \(.*device\.hpp:2:17\)$' "$units")" != 2 ]; then
    echo 'an unsafe function declared in a shared header is not listed by each unit that includes it' >&2
    cat "$units" >&2
    exit 1
fi
grep -Eq '^  contract of bounded \(.*user\.cpp:[0-9]+\), identity [0-9a-f]{16}$' "$units"
grep -Eq '^    rests on unsafe block \(.*user\.cpp:4:5\), in its own body$' "$units"
set +e
"$run/units/program"
status=$?
set -e
if [ "$status" != 5 ]; then
    echo "expected the two-unit program to exit with 5, got $status" >&2
    exit 1
fi

echo 'every claim resting on unsafe code names each block it rests on, and every boundary is listed'
