#!/usr/bin/env bash
# SPEC: TUBOUND-003, TUBOUND-004, TUBOUND-005, TUBOUND-006, TUBOUND-007, TUBOUND-009, TUBOUND-011, TUBOUND-012, TUBOUND-013, TUBOUND-014, TU-003, TU-004, TUBOUND-001, TEMPLATE-003
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

# SPEC: TUBOUND-004, TUBOUND-007 -- asking to terminate is part of the contract
# across units; the measure that proved it is the proving unit's own.
accept ranked ranked.cpp --cppl-emit-interface=ranked.cppli
"$CPPL" -std=c++17 -I "$run" -c "$NEGATIVE/xtu_other_measure.cpp" -o other_measure.o \
    --cppl-import-interface=ranked.cppli --cppl-trust-report > other_measure.out 2> other_measure.err ||
    { cat other_measure.err >&2; fail "a total contract proven with another measure was refused"; }
grep -Eq '^  partial correctness only: +0$' other_measure.out || fail "a caller of a total contract became partial"
grep -Eq '^Function contracts imported: +1$' other_measure.out
refuse partial_request "the contract this translation unit declares for 'count_down' is not the one 'ranked.cppli' records as verified" \
    "$NEGATIVE/xtu_partial_request.cpp" --cppl-import-interface=ranked.cppli
refuse total_request "the contract this translation unit declares for 'count_up' is not the one 'library.cppli' records as verified" \
    "$NEGATIVE/xtu_total_request.cpp" "${both[@]}"
# SPEC: TUBOUND-004 -- a pure definition a contract reaches, however indirectly,
# is part of what it means.
refuse changed_pure_definition "the contract this translation unit declares for 'bounded' is not the one 'ranked.cppli' records as verified" \
    "$NEGATIVE/xtu_changed_pure_definition.cpp" --cppl-import-interface=ranked.cppli

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
refuse version_fixture "it is format version '4', and this compiler reads only version 3" \
    client.cpp "--cppl-import-interface=$NEGATIVE/xtu_other_version.cppli"
# SPEC: TUBOUND-005, STDMODEL-018, RUNTIMECHECK-015 -- an intact interface of a
# format before models (version 1) or runtime validation sites (version 2) were
# recorded cannot say whether a contract rested on one, so it is refused whole
# rather than read as resting on none (TRUST.md TCB-LIB-010).
refuse format_v1_fixture "it is format version '1', and this compiler reads only version 3" \
    client.cpp "--cppl-import-interface=$NEGATIVE/xtu_format_v1.cppli"
refuse format_v2_fixture "it is format version '2', and this compiler reads only version 3" \
    client.cpp "--cppl-import-interface=$NEGATIVE/xtu_format_v2.cppli"
refuse semantics_fixture "it was verified under other verification semantics: it records 'cppl-verification-0'" \
    client.cpp "--cppl-import-interface=$NEGATIVE/xtu_other_semantics.cppli"

# reseal <interface> <output> <sed expression>: the interface with its content
# edited and its checksum recomputed, as an edit that knows the format leaves
# it, so what is decided is the edited field alone.
reseal() {
    sed '$d' "$1" | sed "$3" > "$2.body"
    sed '$d' "$1" | cmp -s - "$2.body" && fail "the edit making $2 changed nothing"
    { cat "$2.body"; printf 'checksum %s\n' "$(sha256 < "$2.body")"; } > "$2"
}

# SPEC: TUBOUND-005, TUBOUND-013 -- an interface is bound to the compiler version
# and to the verification semantics, declared and mechanical, each compared
# alone: a record whose compiler version, semantics version or verifier
# semantics digest differs is refused, while one whose executable digest alone
# differs, another build of the same sources, is used (TRUST.md TCB-XTU-008).
reseal library.cppli other_release.cppli 's/^compiler .*/compiler 9.9.9/'
refuse other_release "cannot use verification interface 'other_release.cppli': it was produced by another C\\+\\+L compiler version: it records '9\\.9\\.9'" \
    client.cpp --cppl-import-interface=other_release.cppli --cppl-import-interface=middle.cppli
reseal library.cppli other_binary.cppli 's/^build .*/build 0000000000000000000000000000000000000000000000000000000000000000/'
accept other_binary client.cpp --cppl-import-interface=other_binary.cppli --cppl-import-interface=middle.cppli
reseal library.cppli other_semantics.cppli 's/^semantics .*/semantics cppl-verification-0/'
refuse other_semantics "cannot use verification interface 'other_semantics.cppli': it was verified under other verification semantics" \
    client.cpp --cppl-import-interface=other_semantics.cppli --cppl-import-interface=middle.cppli
reseal library.cppli other_verifier.cppli 's/^verifier .*/verifier 0000000000000000000000000000000000000000000000000000000000000000/'
refuse other_verifier "cannot use verification interface 'other_verifier.cppli': it was produced by a verifier built from other semantic sources" \
    client.cpp --cppl-import-interface=other_verifier.cppli --cppl-import-interface=middle.cppli

# SPEC: TUBOUND-004 -- a contract's identity follows every pure definition it
# reaches, transitively, and a measure only as the request to terminate: a
# consumer declaring the same pure functions and another measure uses the
# records, one whose inner pure function differs, or that does not ask to
# terminate, is refused.
accept identity identity.cpp --cppl-emit-interface=identity.cppli
accept identity_client identity_client.cpp --cppl-import-interface=identity.cppli
refuse transitive_differs "the contract this translation unit declares for 'stepped' is not the one 'identity.cppli' records as verified" \
    "$NEGATIVE/xtu_transitive_differs.cpp" --cppl-import-interface=identity.cppli
refuse measure_unrequested "the contract this translation unit declares for 'counted_down' is not the one 'identity.cppli' records as verified" \
    "$NEGATIVE/xtu_measure_unrequested.cpp" --cppl-import-interface=identity.cppli

# SPEC: TUBOUND-009 -- a record is identified by what it says was verified, not
# how it says it: rewording clamp4's record leaves the middle unit's record,
# proven through it, usable; recording it partial instead is another result,
# and what rests on it is refused.
reseal library.cppli reworded.cppli '/^entry c:@F@clamp4#i#$/,/^end$/ { s/^name .*/name clamp_to_four/; s/^contract .*/contract reworded/; }'
accept reworded_record client.cpp --cppl-import-interface=reworded.cppli --cppl-import-interface=middle.cppli
grep -q "^correctness total$" library.cppli || fail "library.cppli records no total contract to make partial"
reseal library.cppli repartial.cppli '/^entry c:@F@clamp4#i#$/,/^end$/ s/^correctness total$/correctness partial/'
refuse repartial_record "it was proven through the contract of 'c:@F@clamp4#i#', and no imported interface records that contract as it was" \
    client.cpp --cppl-import-interface=repartial.cppli --cppl-import-interface=middle.cppli

# SPEC: TUBOUND-005 -- staleness is content, never time or place: a source touched
# but unchanged leaves the interface usable, and so does a copy of the interface
# kept at another path.
touch -t 203001010000 library.cpp library.hpp
accept touched_sources client.cpp --cppl-import-interface=library.cppli --cppl-import-interface=middle.cppli
mkdir -p elsewhere
cp library.cppli elsewhere/copied.cppli
accept copied_interface client.cpp --cppl-import-interface=elsewhere/copied.cppli --cppl-import-interface=middle.cppli

# SPEC: TUBOUND-005 -- the real artifact, damaged after it was written.
head -c 300 library.cppli > cut.cppli
refuse truncated_artifact "cannot use verification interface 'cut.cppli': it is truncated" \
    client.cpp --cppl-import-interface=cut.cppli --cppl-import-interface=middle.cppli
awk '{ if ($0 == "correctness partial" && !done) { print "correctness total"; done = 1 } else print }' \
    library.cppli > edited.cppli
cmp -s library.cppli edited.cppli && fail "the edit changed nothing"
refuse edited_artifact "cannot use verification interface 'edited.cppli': it is corrupt" \
    client.cpp --cppl-import-interface=edited.cppli --cppl-import-interface=middle.cppli

# SPEC: TUBOUND-006, TUBOUND-012, TUBOUND-014 -- the honest limit of an
# interface: an edit whose checksum is recomputed is not detected, since nothing
# here re-checks another unit's proof, and interface provenance is a trusted
# build input (TRUST.md 31.1). The claim resting on the forged record is never
# free of assumptions, though the record rests on nothing trusted: the record,
# the interface it came from and its identity are listed under it among the
# interface-dependent claims, and the report states that the interfaces'
# provenance is unauthenticated (TRUST.md TCB-XTU-007, TCB-XTU-010, RFC 0017
# "Safety").
sed '$d' edited.cppli > forged.body
{ cat forged.body; printf 'checksum %s\n' "$(sha256 < forged.body)"; } > forged.cppli
if ! "$CPPL" -std=c++17 -I "$run" -c "$NEGATIVE/xtu_partial_callee.cpp" -o forged.o \
    --cppl-import-interface=forged.cppli --cppl-import-interface=middle.cppli --cppl-trust-report \
    > forged.report 2> forged.err; then
    cat forged.err >&2
    fail "a forged record with a recomputed checksum was not accepted, so this check tests nothing"
fi
grep -Eq '^Assumption-free claims: +0$' forged.report ||
    { cat forged.report >&2; fail "a claim resting on a forged record was assumption-free"; }
grep -Eq '^Interface-dependent claims: +1$' forged.report
sed -n '/^Interface-dependent claims:/,/^Interface provenance:/p' forged.report > forged.interfaced
grep -A1 '^  contract of counted ' forged.interfaced > forged.counted
grep -Eq '^    rests on the contract of count_up \[c:@F@count_up#i#\], imported from forged\.cppli, entry [0-9a-f]{16}, called in its own body$' \
    forged.counted || { cat forged.report >&2; fail "a claim resting on a forged record was shown as a proof of its own unit"; }
grep -Eq "^Interface provenance: +unauthenticated; [0-9]+ imported contracts are believed on the build's word" forged.report ||
    { cat forged.report >&2; fail "the report does not state that interface provenance is unauthenticated"; }

# SPEC: TUBOUND-005, TUBOUND-006 -- a name a forged record carries is shown with
# its newline escaped, so it cannot add a line to the report that names it. The
# name of a trusted law is provenance, so renaming it leaves the record usable;
# a model name this compiler does not have makes the interface unreadable.
awk '
    $1 == "entry" { inside = ($2 == "c:@F@never_seven#i#") }
    inside && $1 == "premise" { $NF = "x%0AAssumption-free%20claims:%20%20%20%20%20%207" }
    { print }' library.cppli | sed '$d' > named.body
grep -q '^premise .* x%0AAssumption-free' named.body || fail "the forged law name was not written"
{ cat named.body; printf 'checksum %s\n' "$(sha256 < named.body)"; } > named.cppli
"$CPPL" -std=c++17 -I "$run" -c client.cpp -o named.o --cppl-import-interface=named.cppli \
    --cppl-import-interface=middle.cppli --cppl-trust-report > named.report 2> named.err ||
    { cat named.err >&2; fail "the record with a forged law name was not accepted, so this check tests nothing"; }
grep -Eq 'rests on x%0AAssumption-free claims: +7 \(library\.cpp:[0-9]+\), identity [0-9a-f]{16}, through the imported contract of never_seven' \
    named.report || { cat named.report >&2; fail "the forged law name is not shown escaped"; }
if grep -Eq '^Assumption-free claims: +7' named.report; then
    fail "a name in a forged record added a line to the trust report"
fi

# SPEC: TUBOUND-005, TUBOUND-009 -- what a refusal names from an interface is
# shown escaped as well: the unit and a file a stale interface names, a
# function two interfaces record differently, and the record a dependent was
# proven through. None can add a line of its own to a diagnostic
# (TRUST.md TCB-XTU-010).
hostile='x%0Aerror:%20injected'
unescaped() {
    if grep -q '^error: injected' "$1.err"; then
        cat "$1.err" >&2
        fail "text read from an interface added a line to the diagnostics of $1"
    fi
}
reseal library.cppli hostile_stale.cppli \
    "s|^unit .*|unit /$hostile|; s|^\\(source [0-9a-f]*\\) .*/library\\.hpp\$|\\1 /$hostile.hpp|"
refuse hostile_stale "it is stale: '/x%0Aerror: injected\\.hpp', which it was produced from, can no longer be read" \
    client.cpp --cppl-import-interface=hostile_stale.cppli --cppl-import-interface=middle.cppli
grep -q "rebuild '/x%0Aerror: injected' with this compiler" hostile_stale.err ||
    { cat hostile_stale.err >&2; fail "the unit a stale interface names is not shown escaped"; }
unescaped hostile_stale
awk -v name="$hostile" '
    $1 == "entry" { inside = ($2 == "c:@F@count_up#i#") }
    inside && $1 == "name" { $0 = "name " name }
    { print }' forged.cppli | sed '$d' > hostile_rival.body
grep -q "^name $hostile\$" hostile_rival.body || fail "the rival record's name was not replaced"
{ cat hostile_rival.body; printf 'checksum %s\n' "$(sha256 < hostile_rival.body)"; } > hostile_rival.cppli
refuse hostile_conflict "'library\\.cppli' and 'hostile_rival\\.cppli' record different contracts for 'x%0Aerror: injected'" \
    client.cpp --cppl-import-interface=library.cppli --cppl-import-interface=hostile_rival.cppli \
    --cppl-import-interface=middle.cppli
unescaped hostile_conflict
reseal middle.cppli hostile_depends.cppli "s|^\\(depends [0-9a-f]*\\) c:@F@clamp4#i#\$|\\1 c:@F@clamp4#i#$hostile|"
refuse hostile_depends "it was proven through the contract of 'c:@F@clamp4#i#x%0Aerror: injected'" \
    client.cpp --cppl-import-interface=library.cppli --cppl-import-interface=hostile_depends.cppli
unescaped hostile_depends

# SPEC: TUBOUND-005 -- a record resting on a library model this compiler does
# not have was not written by a compiler of these semantics, which give every
# model one identity: the interface is refused whole, naming the model escaped.
awk -v name="$hostile" '
    { print }
    $1 == "entry" { inside = ($2 == "c:@F@never_seven#i#") }
    inside && $1 == "premise" { print "model 0000000000000000000000000000000000000000000000000000000000000000 " name }
    ' library.cppli | sed '$d' > unknown_model.body
grep -q '^model 0\{64\} ' unknown_model.body || fail "the unknown model line was not added"
{ cat unknown_model.body; printf 'checksum %s\n' "$(sha256 < unknown_model.body)"; } > unknown_model.cppli
refuse unknown_model "cannot use verification interface 'unknown_model\\.cppli': the record of 'c:@F@never_seven#i#' rests on a library model this compiler does not have: 'x%0Aerror: injected', identity 0{16}" \
    client.cpp --cppl-import-interface=unknown_model.cppli --cppl-import-interface=middle.cppli
unescaped unknown_model
# A model this compiler has, recorded under another model's name, is refused as
# well, so a report never shows a model name an interface chose; the same
# record under its own name is used.
accept sequences -std=c++20 sequences.cpp --cppl-emit-interface=sequences.cppli
grep -q '^model [0-9a-f]\{64\} std::vector%20model$' sequences.cppli || fail "sequences.cppli records no vector model"
accept sequences_middle -std=c++20 sequences_middle.cpp --cppl-import-interface=sequences.cppli
reseal sequences.cppli renamed_model.cppli 's/^\(model [0-9a-f]*\) std::vector%20model$/\1 std::array%20model/'
refuse renamed_model "cannot use verification interface 'renamed_model\\.cppli': the record of '[^']+' rests on a library model this compiler does not have: 'std::array model'" \
    -std=c++20 sequences_middle.cpp --cppl-import-interface=renamed_model.cppli

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
