#!/usr/bin/env bash
# SPEC: TRUSTED-002, PROOFSRC-005, STATUS-002, STATUSPROMO-002, INTERACT-020
# Trust propagation and per-claim assumption closure (TRUST.md 35, 36).
#
# A trusted law may be used like a proven one, and every claim derived from it
# is PROVEN relative to it. What this pins is that the dependency is never
# lost: each proven claim is listed with every trusted law it rests on, whether
# it names that law itself or reaches it through a chain of proofs, and a claim
# that rests on none is counted apart from those that do (TRUST.md 3.2).
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/trust_closure.XXXXXX")

binary="$run/trust_closure"
report="$run/trust_closure.report"

"$CPPL" -std=c++20 "$FIXTURES/trust_closure.cpp" -o "$binary" --cppl-trust-report \
    "--cppl-emit-projection=$run/runtime.cpp" > "$report"

expect() {
    if ! grep -Eq "$1" "$report"; then
        echo "the trust report does not state: $1" >&2
        cat "$report" >&2
        exit 1
    fi
}

# Every category of proven claim is split into what is proven outright and what
# is proven relative to trusted laws, and the two always add up.
expect '^Laws proven: +6$'
expect '^ +by a written proof: +5$'
expect '^Proof declarations proven: +9$'
expect '^Function contracts proven: +3$'
expect '^Omitted cases proven: +3$'
expect '^Impossible paths proven: +1$'
expect '^Unresolved obligations: +0$'
laws=$(grep -A3 '^Laws proven:' "$report")
grep -Eq '^ +assumption-free: +1$' <<< "$laws"
grep -Eq '^ +relative to trusted laws: +5$' <<< "$laws"
proofs=$(grep -A2 '^Proof declarations proven:' "$report")
grep -Eq '^ +assumption-free: +1$' <<< "$proofs"
grep -Eq '^ +relative to trusted laws: +8$' <<< "$proofs"
# `add_zero` names no trusted law and calls nothing that rests on one; the two
# functions whose contracts rest on `broken_counter` reach it through a runtime
# path claim and through a verified call.
contracts=$(grep -A3 '^Function contracts proven:' "$report")
grep -Eq '^ +assumption-free: +1$' <<< "$contracts"
grep -Eq '^ +relative to trusted laws: +2$' <<< "$contracts"
omissions=$(grep -A2 '^Omitted cases proven:' "$report")
grep -Eq '^ +assumption-free: +0$' <<< "$omissions"
grep -Eq '^ +relative to trusted laws: +3$' <<< "$omissions"
paths=$(grep -A2 '^Impossible paths proven:' "$report")
grep -Eq '^ +assumption-free: +0$' <<< "$paths"
grep -Eq '^ +relative to trusted laws: +1$' <<< "$paths"

# A trusted law is TRUSTED, never proven, whether or not anything uses it
# (TRUST.md TCB-REPORT-003), and each is named where it is declared together
# with an identity derived from what it states.
expect '^Laws trusted: +5$'
expect '^Trusted external axioms: +5$'
for law in sensor_identity device_bound broken_counter never_used; do
    expect "^  assumed: +$law \\(.*trust_closure\\.cpp:[0-9]+\\), identity [0-9a-f]{16}$"
done
# SPEC: TRUSTED-003, VERIFIED-044
# A memory proposition is assumed like any trusted law, and the report says what
# it admits, since no proof statement can use it.
expect '^  assumed: +device_window \(.*trust_closure\.cpp:[0-9]+\), identity [0-9a-f]{16}, admits readable\(registers, count\)$'

# Every proven claim carries the identity of what it states (TRUST.md 36.1).
claim_lines=$(sed -n '/^Trust-dependent claims:/,/^Unused trusted laws:/p' "$report" | grep -E '^  [a-z]' || true)
if grep -Evq ', identity [0-9a-f]{16}$' <<< "$claim_lines"; then
    echo 'a proven claim is reported without its identity' >&2
    cat "$report" >&2
    exit 1
fi

# The closure of every claim that rests on a trusted law, in program order, every
# claim that rests on none, and every trusted law nothing rests on. Compared
# whole, so a dependency that goes missing, a claim that is dropped, or an order
# that changes all fail here. Identities are content hashes, checked above.
sed -e "s|$FIXTURES/||g" "$report" | sed -E -e 's/, identity [0-9a-f]{16}$//' |
    sed -n '/^Trust-dependent claims:/,/^$/p' > "$run/closure.actual"
cat > "$run/closure.expected" <<'REPORT'
Trust-dependent claims:      20
  law identity_holds (trust_closure.cpp:46)
    rests on sensor_identity (trust_closure.cpp:20), named directly
  law bound_after_identity (trust_closure.cpp:74)
    rests on sensor_identity (trust_closure.cpp:20), named directly
    rests on device_bound (trust_closure.cpp:24), named directly
  law anything_goes (trust_closure.cpp:127)
    rests on broken_counter (trust_closure.cpp:30), named directly
  law omission_relies_on_assumption (trust_closure.cpp:135)
    rests on broken_counter (trust_closure.cpp:30), named directly
  law only_where_named (trust_closure.cpp:153)
    rests on broken_counter (trust_closure.cpp:30), named directly
  unreachable runtime path never_seven path 1 (trust_closure.cpp:195)
    rests on broken_counter (trust_closure.cpp:30), through a proof it uses
  proof first_link (trust_closure.cpp:54)
    rests on sensor_identity (trust_closure.cpp:20), named directly
  proof second_link (trust_closure.cpp:60)
    rests on sensor_identity (trust_closure.cpp:20), through a proof it uses
  proof third_link (trust_closure.cpp:66)
    rests on sensor_identity (trust_closure.cpp:20), through a proof it uses
  proof mixed (trust_closure.cpp:92)
    rests on sensor_identity (trust_closure.cpp:20), through a proof it uses
  proof through_a_law (trust_closure.cpp:103)
    rests on sensor_identity (trust_closure.cpp:20), through a proof it uses
  proof named_twice (trust_closure.cpp:110)
    rests on sensor_identity (trust_closure.cpp:20), named directly
  proof direct_and_through (trust_closure.cpp:118)
    rests on sensor_identity (trust_closure.cpp:20), named directly
  proof counter_is_one (trust_closure.cpp:181)
    rests on broken_counter (trust_closure.cpp:30), named directly
  omitted case 'State::running' of proof 'omission_relies_on_assumption' (trust_closure.cpp:143)
    rests on broken_counter (trust_closure.cpp:30), through the proof it is written in
  omitted case 'unnamed' of proof 'omission_relies_on_assumption' (trust_closure.cpp:145)
    rests on broken_counter (trust_closure.cpp:30), through the proof it is written in
  omitted case 'unnamed' of proof 'only_where_named' (trust_closure.cpp:167)
    rests on broken_counter (trust_closure.cpp:30), through the proof it is written in
  proof identity_at_three of a law instance (trust_closure.cpp:173)
    rests on sensor_identity (trust_closure.cpp:20), named directly
  contract of never_seven (trust_closure.cpp:195)
    rests on broken_counter (trust_closure.cpp:30), through a proof or verified call it uses
  contract of calls_never_seven (trust_closure.cpp:206)
    rests on broken_counter (trust_closure.cpp:30), through a proof or verified call it uses
Unsafe-dependent claims:     0
Assumption-free claims:      3
  law plain (trust_closure.cpp:211)
  proof outright (trust_closure.cpp:86)
  contract of add_zero (trust_closure.cpp:217)
Unused trusted laws:         2
  unused:                  never_used (trust_closure.cpp:34)
  unused:                  device_window (trust_closure.cpp:40), no statement can use a memory proposition
Library-model-dependent claims: 0

REPORT
if ! diff -u "$run/closure.expected" "$run/closure.actual" >&2; then
    echo 'the trust closure differs from what the fixture states' >&2
    exit 1
fi

# The report is the same on every run (TRUST.md TCB-PROV-005).
"$CPPL" -std=c++20 "$FIXTURES/trust_closure.cpp" -o "$run/again" --cppl-trust-report > "$run/again.report"
if ! diff -q "$report" "$run/again.report" > /dev/null; then
    echo 'the trust report is not deterministic' >&2
    diff -u "$report" "$run/again.report" >&2
    exit 1
fi

output=$("$binary")
if [ "$output" != "42 3" ]; then
    echo "expected the verified program to print '42 3', got '$output'" >&2
    exit 1
fi

# Trusted laws erase like every other formal declaration (FOUNDATIONS.md 120).
if grep -qE '(^|[^_[:alnum:]])(trusted|law|proof) +[A-Za-z_]+ *\(' "$run/runtime.cpp"; then
    echo 'a formal declaration survived into the erased translation unit' >&2
    exit 1
fi

# A unit that assumes nothing says so in every category, because that is true of
# it; the zero-trust report keeps every line it had.
zero="$run/zero.report"
"$CPPL" -std=c++17 "$FIXTURES/identity_law.cpp" -o "$run/zero" --cppl-trust-report > "$zero"
grep -Eq '^Laws trusted: +0$' "$zero"
grep -Eq '^Trusted external axioms: +0$' "$zero"
grep -Eq '^Trust-dependent claims: +0$' "$zero"
grep -Eq '^Unused trusted laws: +0$' "$zero"
if grep -Eq '^ +relative to trusted laws: +[1-9]' "$zero"; then
    echo 'a unit that assumes nothing reported a claim relative to trusted laws' >&2
    cat "$zero" >&2
    exit 1
fi
if grep -q 'rests on' "$zero"; then
    echo 'a unit that assumes nothing reported a dependency' >&2
    exit 1
fi

# One assumption, declared in a header, included by two units and used by one.
# Each unit states it, so each lists it, under the same identity: it is one
# proposition wherever it is included (TRUST.md TCB-TRUST-005). Whether anything
# rests on it is decided per unit, so it is unused only where nothing uses it.
mkdir -p "$run/units"
cat > "$run/units/assumption.hpp" <<'CPP'
#pragma once
trusted law shared_fact(unsigned x) proves (x + 0u == x);
CPP
cat > "$run/units/user.cpp" <<'CPP'
#include "assumption.hpp"
proof relies(unsigned y) proves (y + 0u == y) { exact shared_fact(y); }
int main() { return 0; }
CPP
cat > "$run/units/bystander.cpp" <<'CPP'
#include "assumption.hpp"
law independent(unsigned y) proves (y + 0u == y);
CPP
units="$run/units.report"
"$CPPL" -std=c++20 -I"$run/units" "$run/units/user.cpp" "$run/units/bystander.cpp" -o "$run/units/program" \
    --cppl-trust-report > "$units"
grep -Eq '^Laws trusted: +2$' "$units"
identities=$(grep -E '^  assumed: +shared_fact \(.*assumption\.hpp:2\), identity ' "$units" | sed -E 's/.*identity //' | sort -u)
if [ "$(wc -l <<< "$identities" | tr -d ' ')" != 1 ]; then
    echo 'one assumption included into two units was given two identities' >&2
    cat "$units" >&2
    exit 1
fi
grep -Eq '^Trust-dependent claims: +1$' "$units"
grep -Eq '^  proof relies \(.*user\.cpp:2\), identity [0-9a-f]{16}$' "$units"
grep -Eq '^    rests on shared_fact \(.*assumption\.hpp:2\), named directly$' "$units"
grep -Eq '^Unused trusted laws: +1$' "$units"
grep -Eq '^Laws proven: +1$' "$units"
laws=$(grep -A3 '^Laws proven:' "$units")
grep -Eq '^ +assumption-free: +1$' <<< "$laws"

echo 'every proven claim names the trusted laws it rests on, and nothing more is assumed'
