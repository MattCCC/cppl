#!/usr/bin/env bash
# SPEC: RUNTIMECHECK-001, RUNTIMECHECK-002, RUNTIMECHECK-003, RUNTIMECHECK-004, RUNTIMECHECK-005, RUNTIMECHECK-007
# SPEC: RUNTIMECHECK-008, RUNTIMECHECK-009, RUNTIMECHECK-010, RUNTIMECHECK-011, RUNTIMECHECK-012, RUNTIMECHECK-013
# SPEC: RUNTIMECHECK-014, RUNTIMECHECK-015, ERASE-012, ORTHOCHECK-001, ORTHOCHECK-002, INTERACT-022, INTERACT-023
# TRUST.md TCB-RUNTIMECHK-001 to TCB-RUNTIMECHK-005, TCB-REPORT-004
#
# Runtime-checked refinement construction (RFC 0021).
#
# `fixtures/runtime_validation.cpp` moves unknown runtime values into refined
# types through ordinary C++ checks. This pins, in c++17, c++20 and c++23:
#
#   - every crossing a check establishes is listed as a RUNTIME-CHECKED site
#     with where it is, what it enters and what its predicate states, and no
#     crossing the program establishes statically is: not a literal on a
#     checked path, not a value a precondition bounds;
#   - every contract proven through a site names it, in its own body or
#     through a verified call it makes, and stays PROVEN;
#   - the program run on valid and invalid input returns what its contracts
#     say, taking the failure path where the check fails;
#   - erasure keeps every check: the program is the same code as the fixture
#     erased by hand, where every check stays;
#   - across translation units, a contract proven through a site records it in
#     its interface, a unit proven through that contract names it, and a unit
#     proven through that one carries it on.
#
# The refused twin of each accepted crossing is in
# `negative/runtime_validation.sh`.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"
CLANG="$4"
# shellcheck source=../support/equivalence.sh
source "$(dirname "$0")/../support/equivalence.sh"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/runtime-validation.XXXXXX")

fail() {
    echo "$1" >&2
    exit 1
}

fixture="$FIXTURES/runtime_validation.cpp"
reference="$FIXTURES/runtime_validation.reference.cpp"

for standard in c++17 c++20 c++23; do
    base="$run/runtime-validation-$standard"
    "$CPPL" "-std=$standard" "$fixture" -o "$base" --cppl-trust-report "--cppl-emit-projection=$base.runtime.ii" \
        > "$base.report"
    report="$base.report"

    grep -Eq '^Function contracts proven: +15$' "$report" || fail "not every contract was proven ($standard)"
    grep -Eq '^Unresolved obligations: +0$' "$report" || fail "an obligation is unresolved ($standard)"
    contracts=$(grep -A7 '^Function contracts proven:' "$report")
    # SPEC: RUNTIMECHECK-014, INTERACT-023 -- a claim resting on a runtime
    # check stays PROVEN and is not made to rest on an assumption: only the
    # one resting on an unsafe block and the one resting on the vector model
    # are not assumption-free.
    grep -Eq '^ +relying on runtime checks: +11$' <<< "$contracts" || fail "the claims resting on checks are miscounted"
    grep -Eq '^ +assumption-free: +13$' <<< "$contracts" || fail "a runtime check was reported as an assumption"
    grep -Eq '^ +relying on unsafe code: +1$' <<< "$contracts"

    # SPEC: RUNTIMECHECK-013, TCB-REPORT-004 -- every site, whole, and nothing
    # else: a crossing the program establishes on every execution is not one.
    sed -e "s|$FIXTURES/||g" "$report" | sed -n '/^Runtime validation sites:/,/^Unverified FFI boundaries:/p' \
        > "$run/sites.actual"
    cat > "$run/sites.expected" <<'REPORT'
Runtime validation sites:    10
  RUNTIME-CHECKED:           runtime_validation.cpp:33:24, a value enters Percentage, where ((self >= 0) && (self <= 100)), in verified function percentage_or_zero
  RUNTIME-CHECKED:           runtime_validation.cpp:47:18, a value enters Positive, where (self > 0), in verified function positive_or_one
  RUNTIME-CHECKED:           runtime_validation.cpp:55:16, a value enters Positive, where (self > 0), in verified function checked_result
  RUNTIME-CHECKED:           runtime_validation.cpp:72:34, a value enters Positive, where (self > 0), in verified function checked_argument
  RUNTIME-CHECKED:           runtime_validation.cpp:84:28, a value enters Positive, where (self > 0), in verified function checked_conditional
  RUNTIME-CHECKED:           runtime_validation.cpp:102:19, a value enters Small, where (self < 10), in verified function last_small
  RUNTIME-CHECKED:           runtime_validation.cpp:115:22, a value enters Index<4>, where (self < n), in verified function checked_index
  RUNTIME-CHECKED:           runtime_validation.cpp:128:25, a value enters Positive, where (self > 0), in verified function checked_member
  RUNTIME-CHECKED:           runtime_validation.cpp:140:26, a value enters Positive, where (self > 0), in verified function checked_elements
  RUNTIME-CHECKED:           runtime_validation.cpp:209:18, a value enters Positive, where (self > 0), in verified function rechecked
Unverified FFI boundaries:   not analysed
REPORT
    if ! diff -u "$run/sites.expected" "$run/sites.actual" >&2; then
        fail "the runtime validation sites differ ($standard)"
    fi

    # SPEC: RUNTIMECHECK-014 -- every claim resting on a site, with each site,
    # its own or a verified callee's.
    sed -e "s|$FIXTURES/||g" "$report" | sed -E -e 's/, identity [0-9a-f]{16}$//' |
        sed -n '/^Runtime-check-dependent claims:/,/^Unsafe regions:/p' > "$run/claims.actual"
    cat > "$run/claims.expected" <<'REPORT'
Runtime-check-dependent claims: 11
  contract of percentage_or_zero (runtime_validation.cpp:30)
    rests on the runtime check of Percentage (runtime_validation.cpp:33:24), in its own body
  contract of positive_or_one (runtime_validation.cpp:42)
    rests on the runtime check of Positive (runtime_validation.cpp:47:18), in its own body
  contract of checked_result (runtime_validation.cpp:53)
    rests on the runtime check of Positive (runtime_validation.cpp:55:16), in its own body
  contract of checked_argument (runtime_validation.cpp:69)
    rests on the runtime check of Positive (runtime_validation.cpp:72:34), in its own body
  contract of checked_conditional (runtime_validation.cpp:82)
    rests on the runtime check of Positive (runtime_validation.cpp:84:28), in its own body
  contract of last_small (runtime_validation.cpp:96)
    rests on the runtime check of Small (runtime_validation.cpp:102:19), in its own body
  contract of checked_index (runtime_validation.cpp:112)
    rests on the runtime check of Index<4> (runtime_validation.cpp:115:22), in its own body
  contract of checked_member (runtime_validation.cpp:124)
    rests on the runtime check of Positive (runtime_validation.cpp:128:25), in its own body
  contract of checked_elements (runtime_validation.cpp:140)
    rests on the runtime check of Positive (runtime_validation.cpp:140:26), in its own body
  contract of rechecked (runtime_validation.cpp:193)
    rests on the runtime check of Positive (runtime_validation.cpp:209:18), in its own body
  contract of through_call (runtime_validation.cpp:219)
    rests on the runtime check of Positive (runtime_validation.cpp:47:18), in positive_or_one, through a verified call it makes
Unsafe regions:              2
REPORT
    if ! diff -u "$run/claims.expected" "$run/claims.actual" >&2; then
        fail "the claims resting on runtime checks differ ($standard)"
    fi
    # A site's fact is never shown as a universal proof (TCB-REPORT-004).
    if grep -E 'RUNTIME-CHECKED' "$report" | grep -q 'PROVEN'; then
        fail "a runtime-checked site was described as proven ($standard)"
    fi

    # SPEC: RUNTIMECHECK-007, ORTHOCHECK-002 -- run on valid and invalid input,
    # each function returns what its contract says, and the failure path is
    # taken where the check fails.
    for case in '-5:0 1 1 1 1 0 0 1 1 5 1 1 1' '0:0 1 1 1 1 0 0 1 1 5 1 1 1' '2:2 2 2 2 2 1 2 2 2 5 2 4 2' \
        '3:3 3 3 3 3 2 3 3 2 5 3 1 3' '50:50 50 50 50 50 9 0 50 2 5 50 48 50' \
        '150:0 150 150 150 150 9 0 150 2 5 100 99 150' '-2147483648:0 1 1 1 1 0 0 1 1 5 1 1 1'; do
        input="${case%%:*}"
        expected="${case#*:}"
        actual=$("$base" "$input")
        [ "$actual" = "$expected" ] || fail "on input $input the program printed '$actual', not '$expected' ($standard)"
    done

    # SPEC: ERASE-012, RUNTIMECHECK-009 -- the checks are runtime code and stay.
    for check in 'if (raw >= 0 && raw <= 100) {' 'if (raw <= 0) {' 'Positive p = raw > 0 ? raw : 1;' \
        'while (i < 10u)' 'if (i >= n) {' 'if (raw < 4u) {' 'if (value <= 0) {'; do
        grep -qF "$check" "$base.runtime.ii" || fail "erasure removed the runtime check '$check' ($standard)"
    done
    if grep -Eq '^[[:space:]]*(ensures|expects|invariant|decreases)[[:space:]]' "$base.runtime.ii"; then
        fail "a specification clause reached the runtime program ($standard)"
    fi
    "$CLANG" "-std=$standard" "$reference" -o "$base.reference"
    for input in -5 3 150; do
        [ "$("$base.reference" "$input")" = "$("$base" "$input")" ] ||
            fail "the program and its erasure by hand behave differently on $input ($standard)"
    done
    for level in -O0 -O2; do
        assembly "$base$level.cppl" "$CPPL" "-std=$standard" "$level" "$fixture"
        assembly "$base$level.reference" "$CLANG" "-std=$standard" "$level" "$reference"
        same_code "runtime_validation ($standard, $level)" "$base$level.cppl" "$base$level.reference"
    done
done

# SPEC: RUNTIMECHECK-015, TUBOUND-006, TUBOUND-009 -- a site crosses units.
units="$run/units"
mkdir -p "$units"
cp "$FIXTURES"/runtime_validation_cross_tu/* "$units/"
cd "$units"
"$CPPL" -std=c++20 -c validate.cpp -o validate.o --cppl-emit-interface=validate.cppli --cppl-trust-report \
    > validate.report
check_line=$(grep -n 'Positive checked = raw;' validate.cpp | cut -d: -f1)
grep -qx "runtime $check_line 24 validate.cpp Positive (self%20>%200)" validate.cppli ||
    fail "the interface does not record the site positive_or_one rests on"
[ "$(grep -c '^runtime ' validate.cppli)" = 1 ] || fail "the interface records a site always_two does not rest on"

"$CPPL" -std=c++20 -c consume.cpp -o consume.o --cppl-import-interface=validate.cppli \
    --cppl-emit-interface=consume.cppli --cppl-trust-report > consume.report
grep -Eq '^Runtime-check-dependent claims: 1$' consume.report || fail "the client does not count the claim"
grep -q "^    rests on the runtime check of Positive (validate.cpp:$check_line:24), through the imported contract of positive_or_one \[c:@F@positive_or_one#I#\], imported from validate.cppli, entry [0-9a-f]\{16\}$" \
    consume.report || fail "the client's claim does not name the site of the contract it was proven through"
grep -q "^      whose proof rests on the runtime check of Positive (validate.cpp:$check_line:24)$" consume.report ||
    fail "the imported contract does not name its site"
grep -Eq '^Runtime validation sites: +0$' consume.report || fail "another unit's site was listed as this unit's"
# The site is carried on to a unit proven through this one.
grep -qx "runtime $check_line 24 validate.cpp Positive (self%20>%200)" consume.cppli ||
    fail "the client's interface does not carry the site on"
"$CPPL" validate.o consume.o -o program
[ "$(./program -4)" = '1 2' ] && [ "$(./program 9)" = '9 2' ] || fail "the program built from two units misbehaves"

# A unit proven through consume.cpp's contract rests on the site two units
# away, named where it is.
"$CPPL" -std=c++20 -c relay.cpp -o relay.o --cppl-import-interface=validate.cppli \
    --cppl-import-interface=consume.cppli --cppl-trust-report > relay.report
grep -Eq '^Runtime-check-dependent claims: 1$' relay.report || fail "the relay does not count the claim"
grep -q "^    rests on the runtime check of Positive (validate.cpp:$check_line:24), through the imported contract of through_interface \[c:@F@through_interface#I#\], imported from consume.cppli, entry [0-9a-f]\{16\}$" \
    relay.report || fail "the relay's claim does not name the site two units away"

# SPEC: TUBOUND-009 -- a record whose site was edited away, with its checksum
# recomputed, is no longer the record a unit proven through it recorded, so
# neither is used.
sed -e '/^runtime /d' -e '$d' validate.cppli > dropped.body
grep -q '^runtime ' dropped.body && fail "the site was not edited away"
{
    cat dropped.body
    printf 'checksum %s\n' "$( (command -v sha256sum > /dev/null && sha256sum || shasum -a 256) < dropped.body | cut -d' ' -f1)"
} > dropped.cppli
if "$CPPL" -std=c++20 -c relay.cpp -o again.o --cppl-import-interface=dropped.cppli \
    --cppl-import-interface=consume.cppli > again.out 2> again.err; then
    fail "a record whose runtime site was removed was used beside a record proven through it"
fi
[ ! -e again.o ] || fail "an object was produced from the edited record"
grep -q 'positive_or_one' again.err || fail "the edited record was refused for another reason"

echo 'every refinement a runtime check establishes is reported RUNTIME-CHECKED where it is, with every claim resting on it, in every unit'
