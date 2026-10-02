#!/usr/bin/env bash
# SPEC: RUNTIMECHECK-001, RUNTIMECHECK-002, RUNTIMECHECK-003, RUNTIMECHECK-004, RUNTIMECHECK-005, RUNTIMECHECK-007
# SPEC: RUNTIMECHECK-008, RUNTIMECHECK-009, RUNTIMECHECK-010, RUNTIMECHECK-011, RUNTIMECHECK-012, RUNTIMECHECK-014
# SPEC: RUNTIMECHECK-015, RUNTIMECHECK-018, RUNTIMECHECK-019, RUNTIMECHECK-021, ERASE-012, REFINE-017, REFINE-018
# SPEC: ORTHOCHECK-001, ORTHOCHECK-002, INTERACT-022, INTERACT-023
# TRUST.md TCB-RUNTIMECHK-001 to TCB-RUNTIMECHK-006, TCB-REPORT-004
#
# Runtime validation (SPEC.md 28).
#
# `fixtures/runtime_validation.cpp` moves unknown runtime values into refined
# types two ways. This pins, in c++17, c++20 and c++23:
#
#   - a crossing proven from the facts of the path an ordinary condition
#     selects is proven statically: no runtime validation site, nothing a
#     claim rests on;
#   - every validation expression is listed as a RUNTIME-CHECKED site with
#     where it is, what it tests and what the predicate states, and nothing
#     else is;
#   - every contract about a body holding a site, or proven through the
#     contract of one, names it and stays PROVEN;
#   - the program run on valid and invalid input returns what its contracts
#     say, taking the failure path where a test fails;
#   - erasure keeps every ordinary check as written and lowers every
#     validation to a call of the validator its refinement lowers to: the
#     program is the same code as the fixture erased by hand;
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
# shellcheck source=../support/parallel.sh
source "$(dirname "$0")/../support/parallel.sh"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/runtime-validation.XXXXXX")

fail() {
    echo "$1" >&2
    exit 1
}

fixture="$FIXTURES/runtime_validation.cpp"
reference="$FIXTURES/runtime_validation.reference.cpp"

# Each standard is a case of its own, in files of its own, and so are the units
# below, so they are checked side by side (support/parallel.sh).
in_standard() {
    local standard="$1"
    base="$run/runtime-validation-$standard"
    "$CPPL" "-std=$standard" "$fixture" -o "$base" --cppl-trust-report "--cppl-emit-projection=$base.runtime.ii" \
        > "$base.report"
    report="$base.report"

    grep -Eq '^Function contracts proven: +24$' "$report" || fail "not every contract was proven ($standard)"
    grep -Eq '^Unresolved obligations: +0$' "$report" || fail "an obligation is unresolved ($standard)"
    contracts=$(grep -A7 '^Function contracts proven:' "$report")
    # SPEC: RUNTIMECHECK-010, RUNTIMECHECK-014, INTERACT-023 -- only a claim
    # about a body holding a validation, or proven through one, rests on a
    # site; it stays PROVEN and is not made to rest on an assumption: only the
    # one resting on an unsafe block and the one resting on the vector model
    # are not assumption-free.
    grep -Eq '^ +relying on runtime checks: +7$' <<< "$contracts" || fail "the claims resting on validations are miscounted"
    grep -Eq '^ +assumption-free: +22$' <<< "$contracts" || fail "a validation was reported as an assumption"
    grep -Eq '^ +relying on unsafe code: +1$' <<< "$contracts"

    # SPEC: RUNTIMECHECK-010, RUNTIMECHECK-011, RUNTIMECHECK-014, TCB-REPORT-004
    # -- every validation expression, whole, and nothing else: no crossing an
    # ordinary condition selects is a site.
    sed -e "s|$FIXTURES/||g" "$report" | sed -n '/^Runtime validation sites:/,/^Unverified FFI boundaries:/p' \
        > "$base.sites.actual"
    cat > "$base.sites.expected" <<'REPORT'
Runtime validation sites:    7
  RUNTIME-CHECKED:           runtime_validation.cpp:231:9, validates a value against Percentage, where ((self >= 0) && (self <= 100)), in verified function validated_percentage
  RUNTIME-CHECKED:           runtime_validation.cpp:243:10, validates a value against Positive, where (self > 0), in verified function validated_or_one
  RUNTIME-CHECKED:           runtime_validation.cpp:255:23, validates a value against Positive, where (self > 0), in verified function validated_below
  RUNTIME-CHECKED:           runtime_validation.cpp:267:27, validates a value against Positive, where (self > 0), in verified function validated_later
  RUNTIME-CHECKED:           runtime_validation.cpp:282:10, validates a value against Positive, where (self > 0), in verified function revalidated
  RUNTIME-CHECKED:           runtime_validation.cpp:286:10, validates a value against Positive, where (self > 0), in verified function revalidated
  RUNTIME-CHECKED:           runtime_validation.cpp:310:12, validates a value against Positive, where (self > 0), in verified function validated_halvings
Unverified FFI boundaries:   not analysed
REPORT
    if ! diff -u "$base.sites.expected" "$base.sites.actual" >&2; then
        fail "the runtime validation sites differ ($standard)"
    fi

    # SPEC: RUNTIMECHECK-014 -- every claim resting on a site, with each site,
    # its own or a verified callee's.
    sed -e "s|$FIXTURES/||g" "$report" | sed -E -e 's/, identity [0-9a-f]{16}$//' |
        sed -n '/^Runtime-check-dependent claims:/,/^Unsafe regions:/p' > "$base.claims.actual"
    cat > "$base.claims.expected" <<'REPORT'
Runtime-check-dependent claims: 7
  contract of validated_percentage (runtime_validation.cpp:232)
    rests on the validation of Percentage (runtime_validation.cpp:231:9), in its own body
  contract of validated_or_one (runtime_validation.cpp:246)
    rests on the validation of Positive (runtime_validation.cpp:243:10), in its own body
  contract of validated_below (runtime_validation.cpp:256)
    rests on the validation of Positive (runtime_validation.cpp:255:23), in its own body
  contract of validated_later (runtime_validation.cpp:269)
    rests on the validation of Positive (runtime_validation.cpp:267:27), in its own body
  contract of revalidated (runtime_validation.cpp:285)
    rests on the validation of Positive (runtime_validation.cpp:282:10), in its own body
    rests on the validation of Positive (runtime_validation.cpp:286:10), in its own body
  contract of through_validation (runtime_validation.cpp:299)
    rests on the validation of Positive (runtime_validation.cpp:243:10), in validated_or_one, through a verified call it makes
  contract of validated_halvings (runtime_validation.cpp:311)
    rests on the validation of Positive (runtime_validation.cpp:310:12), in its own body
Unsafe regions:              2
REPORT
    if ! diff -u "$base.claims.expected" "$base.claims.actual" >&2; then
        fail "the claims resting on validations differ ($standard)"
    fi
    # A site's fact is never shown as a universal proof (TCB-REPORT-004).
    if grep -E 'RUNTIME-CHECKED' "$report" | grep -q 'PROVEN'; then
        fail "a runtime-checked site was described as proven ($standard)"
    fi

    # SPEC: RUNTIMECHECK-007, ORTHOCHECK-002 -- run on valid and invalid input,
    # each function returns what its contract says, and the failure path is
    # taken where the condition or the validation fails.
    for case in '-5:0 1 1 1 1 0 0 1 1 5 1 1 1|0 1 1 1 1 1 0|0' '0:0 1 1 1 1 0 0 1 1 5 1 1 1|0 1 1 1 1 1 0|0' \
        '2:2 2 2 2 2 1 2 2 2 5 2 4 2|2 2 2 2 1 2 2|2' '3:3 3 3 3 3 2 3 3 2 5 3 1 3|3 3 3 3 2 3 2|3' \
        '50:50 50 50 50 50 9 0 50 2 5 50 48 50|50 50 50 50 49 50 6|50' \
        '150:0 150 150 150 150 9 0 150 2 5 100 99 150|0 150 150 150 149 150 8|0' \
        '1000:0 1000 1000 1000 1000 9 0 1000 2 5 100 99 1000|0 1000 1 1000 999 1000 10|0' \
        '-2147483648:0 1 1 1 1 0 0 1 1 5 1 1 1|0 1 1 1 1 1 0|0'; do
        input="${case%%:*}"
        expected="${case#*:}"
        actual=$("$base" "$input" | paste -sd'|' -)
        [ "$actual" = "$expected" ] || fail "on input $input the program printed '$actual', not '$expected' ($standard)"
    done

    # SPEC: ERASE-012, RUNTIMECHECK-009, RUNTIMECHECK-021 -- ordinary checks
    # stay as written, and each validation calls the validator its refinement
    # lowers to beside the alias; a refinement no validation names gains none.
    for check in 'if (raw >= 0 && raw <= 100) {' 'if (raw <= 0) {' 'Positive p = raw > 0 ? raw : 1;' \
        'while (i < 10u)' 'if (i >= n) {' 'if (raw < 4u) {' 'if (value <= 0) {' 'if (__cppl_validate_0(raw)) {' \
        'if (!__cppl_validate_1(raw)) {' 'if (raw < 1000 && __cppl_validate_1(raw)) {' \
        'const bool positive = __cppl_validate_1(raw);' 'if (!__cppl_validate_1(value)) {' \
        'while (__cppl_validate_1(x))' 'if (is_percentage(raw)) {' \
        'using Percentage = int; [[maybe_unused]] static inline bool __cppl_validate_0(int self) { return static_cast<bool>(self >= 0 && self <= 100); }' \
        'using Positive = int; [[maybe_unused]] static inline bool __cppl_validate_1(int self) { return static_cast<bool>(self > 0); }'; do
        grep -qF "$check" "$base.runtime.ii" || fail "erasure did not keep the runtime check '$check' ($standard)"
    done
    grep -qx 'using Small = unsigned;' "$base.runtime.ii" || fail "a refinement no validation names gained a validator"
    if grep -q 'validate<' "$base.runtime.ii"; then
        fail "a validation expression reached the runtime program unlowered ($standard)"
    fi
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
}

# SPEC: RUNTIMECHECK-015, TUBOUND-006, TUBOUND-009 -- a site crosses units.
cross_units() {
    units="$run/units"
    mkdir -p "$units"
    cp "$FIXTURES"/runtime_validation_cross_tu/* "$units/"
    cd "$units"
    "$CPPL" -std=c++20 -c validate.cpp -o validate.o --cppl-emit-interface=validate.cppli --cppl-trust-report \
        > validate.report
    check_line=$(grep -n 'validate<Positive>(raw)' validate.cpp | cut -d: -f1)
    check_column=$(( $(grep 'validate<Positive>(raw)' validate.cpp | awk '{print index($0, "validate<")}') ))
    grep -qx "runtime $check_line $check_column validate.cpp Positive (self%20>%200)" validate.cppli ||
        fail "the interface does not record the site positive_or_one rests on"
    [ "$(grep -c '^runtime ' validate.cppli)" = 1 ] || fail "the interface records a site always_two does not rest on"

    "$CPPL" -std=c++20 -c consume.cpp -o consume.o --cppl-import-interface=validate.cppli \
        --cppl-emit-interface=consume.cppli --cppl-trust-report > consume.report
    grep -Eq '^Runtime-check-dependent claims: 1$' consume.report || fail "the client does not count the claim"
    grep -q "^    rests on the validation of Positive (validate.cpp:$check_line:$check_column), through the imported contract of positive_or_one \[c:@F@positive_or_one#I#\], imported from validate.cppli, entry [0-9a-f]\{16\}$" \
        consume.report || fail "the client's claim does not name the site of the contract it was proven through"
    grep -q "^      whose proof rests on the validation of Positive (validate.cpp:$check_line:$check_column)$" consume.report ||
        fail "the imported contract does not name its site"
    grep -Eq '^Runtime validation sites: +0$' consume.report || fail "another unit's site was listed as this unit's"
    # The site is carried on to a unit proven through this one.
    grep -qx "runtime $check_line $check_column validate.cpp Positive (self%20>%200)" consume.cppli ||
        fail "the client's interface does not carry the site on"
    "$CPPL" validate.o consume.o -o program
    [ "$(./program -4)" = '1 2' ] && [ "$(./program 9)" = '9 2' ] || fail "the program built from two units misbehaves"

    # A unit proven through consume.cpp's contract rests on the site two units
    # away, named where it is.
    "$CPPL" -std=c++20 -c relay.cpp -o relay.o --cppl-import-interface=validate.cppli \
        --cppl-import-interface=consume.cppli --cppl-trust-report > relay.report
    grep -Eq '^Runtime-check-dependent claims: 1$' relay.report || fail "the relay does not count the claim"
    grep -q "^    rests on the validation of Positive (validate.cpp:$check_line:$check_column), through the imported contract of through_interface \[c:@F@through_interface#I#\], imported from consume.cppli, entry [0-9a-f]\{16\}$" \
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
}

cases_begin "$run/cases"
for standard in c++17 c++20 c++23; do
    case_run in_standard "$standard"
done
case_run cross_units
cases_end

echo 'only validation expressions are RUNTIME-CHECKED sites, each reported where it is with every claim resting on it, in every unit; path facts are proven statically'
