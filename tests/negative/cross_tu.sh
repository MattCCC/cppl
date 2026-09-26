#!/usr/bin/env bash
# SPEC: TUBOUND-003, TUBOUND-004, TUBOUND-005, TUBOUND-006, TUBOUND-007, TUBOUND-009, TU-003, TU-004, TUBOUND-001, TEMPLATE-003
# TRUST.md TCB-XTU-001, TCB-XTU-003, TCB-XTU-004, TCB-XTU-006, TCB-ARTIFACT-001, TCB-VERSION-001
#
# Every way a contract of another translation unit must not be used here.
#
# Each consumer below is a written-out file in fixtures/negative/xtu_*.cpp that
# mirrors a function of fixtures/cross_tu/client.cpp, which tests/e2e/cross_tu.sh
# shows verifying, and differs from it in one thing; each goal it states is
# false, or closable only through the contract being refused. Each artifact
# defect is shown refused with its reason, beside the intact artifact accepted.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"
NEGATIVE="$FIXTURES/negative"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/cross-tu-refused.XXXXXX")
cp "$FIXTURES"/cross_tu/* "$run/"
cd "$run"

fail() {
    echo "$1" >&2
    exit 1
}

sha256() {
    if command -v sha256sum > /dev/null 2>&1; then
        sha256sum | cut -d' ' -f1
    else
        shasum -a 256 | cut -d' ' -f1
    fi
}

accept() {
    local name="$1"
    shift
    if ! "$CPPL" -std=c++17 -I "$run" -c "$@" -o "$name.o" > "$name.out" 2> "$name.err"; then
        tail -30 "$name.err" >&2
        fail "refused what should verify: $name"
    fi
}

# refuse <name> <pattern> <cppl arguments...>: the compile fails, produces no
# object, reports the reason matching <pattern>, and reports nothing proven.
refuse() {
    local name="$1" pattern="$2"
    shift 2
    if "$CPPL" -std=c++17 -I "$run" -c "$@" -o "$name.o" --cppl-trust-report > "$name.out" 2> "$name.err"; then
        fail "accepted what must be refused: $name"
    fi
    [ ! -e "$name.o" ] || fail "$name produced an object"
    if ! grep -Eq "$pattern" "$name.err"; then
        tail -30 "$name.err" >&2
        fail "$name was refused, but not because: $pattern"
    fi
    if grep -Eq 'Function contracts proven: *[1-9]' "$name.out"; then
        fail "$name reported a contract proven"
    fi
}

accept library library.cpp --cppl-emit-interface=library.cppli
accept middle middle.cpp --cppl-import-interface=library.cppli --cppl-emit-interface=middle.cppli
both=(--cppl-import-interface=library.cppli --cppl-import-interface=middle.cppli)
accept client client.cpp "${both[@]}"

# SPEC: TUBOUND-003, TUBOUND-001 -- a declaration is not evidence.
refuse no_interface 'no imported verification interface records its contract' client.cpp
refuse unproven 'identity.* no imported verification interface records its contract' \
    "$NEGATIVE/xtu_unproven_declaration.cpp" "${both[@]}"
refuse missing_file "cannot use verification interface 'absent.cppli': it cannot be read" \
    client.cpp --cppl-import-interface=absent.cppli --cppl-import-interface=middle.cppli

# SPEC: TUBOUND-003 -- what the recorded contract gives, and nothing more.
refuse false_postcondition "return path 'clamped path 1' does not satisfy its contract" \
    "$NEGATIVE/xtu_false_postcondition.cpp" "${both[@]}"
refuse missing_precondition "call-site precondition for 'clamped -> clamp4' is not proven" \
    "$NEGATIVE/xtu_missing_precondition.cpp" "${both[@]}"
refuse refined_result "return path 'small path 1' does not satisfy its contract" \
    "$NEGATIVE/xtu_refined_result_stronger.cpp" "${both[@]}"

# SPEC: TUBOUND-007 -- a recorded partial contract is not a total one.
refuse partial_callee "termination of verified function 'counted' is not established: it calls 'count_up'" \
    "$NEGATIVE/xtu_partial_callee.cpp" "${both[@]}"

# SPEC: TUBOUND-004 -- the callable, as Clang resolves it.
refuse wrong_overload "return path 'stepped path 1' does not satisfy its contract" \
    "$NEGATIVE/xtu_wrong_overload.cpp" "${both[@]}"
refuse unrecorded_overload "'step' is declared but not defined in this translation unit, and no imported" \
    "$NEGATIVE/xtu_unrecorded_overload.cpp" "${both[@]}"
refuse other_specialization "'bound' is declared but not defined in this translation unit, and no imported" \
    "$NEGATIVE/xtu_other_specialization.cpp" "${both[@]}"
refuse template_declaration "cannot be stated for this specialization" \
    "$NEGATIVE/xtu_template_declaration.cpp" "${both[@]}"
refuse internal_linkage "'hidden' has a body this implementation cannot state as a value: it is declared but not defined" \
    "$NEGATIVE/xtu_internal_linkage.cpp" "${both[@]}"
if grep -q 'verification interface' internal_linkage.err; then
    fail "an interface was consulted for a function with internal linkage"
fi

# SPEC: TUBOUND-004 -- the contract this unit states, never the one recorded.
refuse stronger_declaration "the contract this translation unit declares for 'clamp4' is not the one 'library.cppli' records as verified" \
    "$NEGATIVE/xtu_stronger_declaration.cpp" "${both[@]}"
grep -q 'declared here: ' stronger_declaration.err
grep -q 'recorded there: ' stronger_declaration.err
refuse weaker_precondition "the contract this translation unit declares for 'clamp4' is not the one" \
    "$NEGATIVE/xtu_weaker_precondition.cpp" "${both[@]}"

# SPEC: TU-003 -- one function, one contract, however often it is declared.
refuse conflicting_redeclaration "this verified declaration of 'count_to' states a different contract" \
    "$NEGATIVE/xtu_conflicting_redeclaration.cpp"

# SPEC: TUBOUND-006, TUBOUND-009 -- a record is used only with every record it was
# proven through, as it was when it was proven.
accept middle_with_library "$NEGATIVE/xtu_middle_only.cpp" "${both[@]}"
refuse middle_alone "it was proven through the contract of 'c:@F@clamp4#i#', and no imported interface records" \
    "$NEGATIVE/xtu_middle_only.cpp" --cppl-import-interface=middle.cppli

# SPEC: TUBOUND-009 -- two records of one function that disagree.
accept conflicting_producer rival.cpp --cppl-emit-interface=conflicting.cppli
refuse conflicting_interfaces "verification interfaces 'library.cppli' and 'conflicting.cppli' record different contracts for 'clamp4'" \
    client.cpp "${both[@]}" --cppl-import-interface=conflicting.cppli

# SPEC: TUBOUND-005 -- produced under another configuration.
"$CPPL" -std=c++20 -c library.cpp -o library20.o --cppl-emit-interface=library20.cppli
refuse other_language_mode "it was produced in another C\\+\\+ language mode: it records 'c\\+\\+20', and this compile uses 'c\\+\\+17'" \
    client.cpp --cppl-import-interface=library20.cppli --cppl-import-interface=middle.cppli

# SPEC: TUBOUND-005 -- malformed, of another version, from another build: each
# written out, and each refused before anything in it is read as a contract.
refuse truncated_fixture "it is truncated" client.cpp "--cppl-import-interface=$NEGATIVE/xtu_truncated.cppli"
refuse checksum_fixture "it is corrupt: its checksum does not match its content" \
    client.cpp "--cppl-import-interface=$NEGATIVE/xtu_checksum_mismatch.cppli"
refuse status_fixture "records status 'refused'; only a proven contract is recorded" \
    client.cpp "--cppl-import-interface=$NEGATIVE/xtu_status_refused.cppli"
refuse version_fixture "it is format version '2', and this compiler reads only version 1" \
    client.cpp "--cppl-import-interface=$NEGATIVE/xtu_other_version.cppli"
refuse build_fixture "it was produced by another" \
    client.cpp "--cppl-import-interface=$NEGATIVE/xtu_other_build.cppli"

# SPEC: TUBOUND-005 -- the real artifact, damaged after it was written.
head -c 300 library.cppli > cut.cppli
refuse truncated_artifact "cannot use verification interface 'cut.cppli': it is truncated" \
    client.cpp --cppl-import-interface=cut.cppli --cppl-import-interface=middle.cppli
awk '{ if ($0 == "correctness partial" && !done) { print "correctness total"; done = 1 } else print }' \
    library.cppli > edited.cppli
cmp -s library.cppli edited.cppli && fail "the edit changed nothing"
refuse edited_artifact "cannot use verification interface 'edited.cppli': it is corrupt" \
    client.cpp --cppl-import-interface=edited.cppli --cppl-import-interface=middle.cppli

# SPEC: TUBOUND-006 -- the honest limit of an interface: an edit whose checksum
# is recomputed is not detected, since nothing here re-checks another unit's
# proof. What rests on it is still reported resting on that record, never as
# proven outright (TRUST.md TCB-XTU-005, RFC 0017 "Safety").
sed '$d' edited.cppli > forged.body
{ cat forged.body; printf 'checksum %s\n' "$(sha256 < forged.body)"; } > forged.cppli
if ! "$CPPL" -std=c++17 -I "$run" -c "$NEGATIVE/xtu_partial_callee.cpp" -o forged.o \
    --cppl-import-interface=forged.cppli --cppl-import-interface=middle.cppli --cppl-trust-report \
    > forged.report 2> forged.err; then
    cat forged.err >&2
    fail "a forged record with a recomputed checksum was not accepted, so this check tests nothing"
fi
grep -Eq '^Assumption-free claims: +0$' forged.report || fail "a claim resting on a forged record was assumption-free"
grep -Eq '^Interface-dependent claims: +1$' forged.report
grep -q 'rests on the contract of count_up \[c:@F@count_up#i#\], imported from forged.cppli' forged.report

# SPEC: TUBOUND-005 -- a path an interface names is hostile: one that is not a
# regular file is never read, so a device cannot make an import run forever.
ln -s /dev/zero "$run/zz-device"
awk -v run="$run" '
    { print }
    /^source / && !added && index($0, "/library.hpp") {
        line = $0
        sub(/library\.hpp$/, "zz-device", line)
        print line
        added = 1
    }' library.cppli | sed '$d' > device.body
grep -q 'zz-device' device.body || fail "the device line was not added"
{ cat device.body; printf 'checksum %s\n' "$(sha256 < device.body)"; } > device.cppli
refuse device_source "cannot use verification interface 'device.cppli': it is stale: '.*zz-device', which it was produced from, can no longer be read as a source file" \
    client.cpp --cppl-import-interface=device.cppli --cppl-import-interface=middle.cppli

# SPEC: TUBOUND-005 -- stale: the unit changed after its interface was written.
mkdir stale
cp library.hpp library.cpp middle.hpp middle.cpp client.cpp stale/
(
    cd stale
    "$CPPL" -std=c++17 -c library.cpp -o library.o --cppl-emit-interface=library.cppli
    "$CPPL" -std=c++17 -c middle.cpp -o middle.o --cppl-import-interface=library.cppli \
        --cppl-emit-interface=middle.cppli
    "$CPPL" -std=c++17 -c client.cpp -o fresh.o --cppl-import-interface=library.cppli \
        --cppl-import-interface=middle.cppli
    cp "$NEGATIVE/xtu_library_changed.cpp" library.cpp
    if "$CPPL" -std=c++17 -c client.cpp -o stale.o --cppl-import-interface=library.cppli \
        --cppl-import-interface=middle.cppli 2> stale.err; then
        fail "a stale interface was used"
    fi
    grep -Eq "cannot use verification interface 'library.cppli': it is stale: '.*/stale/library.cpp' has changed since it was produced" stale.err \
        || { cat stale.err >&2; fail "the stale interface was refused for another reason"; }
    # The changed unit no longer verifies, and takes its interface with it.
    if "$CPPL" -std=c++17 -c library.cpp -o library.o --cppl-emit-interface=library.cppli 2> changed.err; then
        fail "the broken library verified"
    fi
    [ ! -e library.cppli ] || fail "a unit that no longer verifies left its interface behind"
)

# SPEC: TUBOUND-002 -- a failed unit withdraws only an interface: a path that
# names any other file keeps it.
printf 'not an interface\n' > precious.txt
if "$CPPL" -std=c++17 -I "$run" -c "$NEGATIVE/xtu_false_postcondition.cpp" -o precious.o \
    --cppl-import-interface=library.cppli --cppl-emit-interface=precious.txt 2> precious.err; then
    fail "a refused unit compiled"
fi
[ "$(cat precious.txt)" = 'not an interface' ] || fail "a failed unit removed a file that was not an interface"

# SPEC: TUBOUND-002 -- an interface describes one unit.
if "$CPPL" -std=c++17 -I "$run" -c library.cpp middle.cpp --cppl-emit-interface=two.cppli \
    --cppl-import-interface=library.cppli 2> two.err; then
    fail "one interface was written for two units"
fi
grep -q 'records the verification interface of one translation unit, and this command compiles 2' two.err
[ ! -e two.cppli ] || fail "an interface was written for two units"

echo 'contracts of other units fail closed: absent, unproven, stronger, weaker, partial, other callables,' \
     'conflicting, incomplete, stale, damaged, of another version, mode or build'
