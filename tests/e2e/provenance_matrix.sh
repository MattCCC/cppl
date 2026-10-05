#!/usr/bin/env bash
# SPEC: TRUSTED-002, STATUS-002, STDMODEL-018, RUNTIMECHECK-013, RUNTIMECHECK-014, UNSAFE-003, TUBOUND-002
# SPEC: TUBOUND-006, TUBOUND-014
# TRUST.md 36.3, Annex C.2, Annex C.5, TCB-REPORT-002, TCB-REPORT-004, TCB-REPORT-005, TCB-XTU-010
#
# The trust closure of every claim, exhaustively (TRUST.md 36.3). Each kind of
# dependency -- a trusted law, an unsafe block, a library model, a runtime
# validation site and another unit's contract -- reaches claims at its source,
# through one call, a chain of three, both sides of a diamond, a recursion
# group, a unit between, and in combination (`fixtures/provenance_matrix.cpp`,
# `fixtures/provenance_matrix_cross_tu/`). For every claim:
#
#   - the JSON report names exactly the closure written below: each dependency
#     once however many paths reach it, marked as reached through what the
#     claim uses where it is not the claim's own, and none that no path
#     reaches; an imported contract carries what its record rests on;
#   - each per-claim section of the text report lists exactly the claims whose
#     JSON closure holds that kind of dependency, so the two reports agree;
#   - the program computes what the contracts state.
#
# `lsp_verification_test` checks that the editor names the same closures.
set -euo pipefail
CPPL="$1"
FIXTURES="$2"
WORK="$3"
CLANG="$4"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/provenance-matrix.XXXXXX")
fail() {
    echo "$1" >&2
    exit 1
}

# One line per claim of a JSON trust report: its kind and subject, then each
# category of its closure. An entry reached only through what the claim uses is
# marked '~'; an imported contract lists, in brackets, what its record rests on.
closures() {
    awk '
        function flush() {
            if (subject != "")
                printf "%s %s|laws:%s|unsafe:%s|models:%s|runtime:%s|imported:%s|free:%s|empty:%s\n", kind, subject, cat["trusted_laws"], cat["unsafe_blocks"], cat["library_models"], cat["runtime_checks"], cat["imported_contracts"], free, empty
            subject = ""
            split("", cat)
        }
        function add(list, item) { return list == "" ? item : list "," item }
        function value(line) { sub(/^ *"[a-z_]*": "?/, "", line); sub(/"?,?$/, "", line); return line }
        /^  "claims": \[$/ { inside = 1; next }
        inside && /^  \]/ { flush(); inside = 0 }
        !inside { next }
        /^    \{$/ { flush(); next }
        /^      "kind": / { kind = value($0) }
        /^      "subject": / { subject = value($0) }
        /^      "assumption_free": / { free = value($0) }
        /^      "trusted_closure_empty": / { empty = value($0) }
        /^      "[a-z_]*": \[/ { array = $0; sub(/^ *"/, "", array); sub(/".*/, "", array); next }
        /^        \{$/ { name = ""; line = ""; refinement = ""; direct = ""; split("", inner); contracts = 0; next }
        /^          "(name|model)": / { name = value($0) }
        /^          "refinement": / { refinement = value($0) }
        /^          "file": / { file = value($0) }
        /^          "line": / { line = value($0) }
        /^            "line": / { line = value($0) }
        /^          "direct": / { direct = value($0) == "true" ? "" : "~" }
        /^          "[a-z_]*": \[/ { nested = $0; sub(/^ *"/, "", nested); sub(/".*/, "", nested); next }
        /^            \{$/ { iname = ""; ifile = ""; iline = ""; iref = ""; next }
        /^              "name": / { iname = value($0) }
        /^              "file": / { ifile = value($0) }
        /^              "line": / { iline = value($0) }
        /^              "refinement": / { iref = value($0) }
        /^              "symbol": / { iname = "contract" }
        /^            \}/ {
            if (nested == "contracts") contracts++
            else inner[nested] = add(inner[nested], (iref != "" ? iref "@" : "") (iname != "" ? iname : ifile ":" iline))
        }
        /^        \}/ {
            if (array == "imported_contracts")
                entry = name "[laws=" inner["trusted_laws"] ";unsafe=" inner["unsafe_blocks"] ";models=" inner["library_models"] \
                        ";runtime=" inner["runtime_checks"] ";contracts=" contracts "]" direct
            else if (array == "unsafe_blocks") entry = "line" line direct
            else if (array == "runtime_checks") entry = refinement "@line" line direct
            else entry = name direct
            cat[array] = add(cat[array], entry)
        }
    ' "$1"
}

# The subjects one per-claim section of the text report lists.
section() {
    awk -v header="$2" '
        index($0, header) == 1 { inside = 1; next }
        inside && /^[^ ]/ { inside = 0 }
        inside && /^  [^ ]/ {
            line = $0
            sub(/^  (contract of |proof |law |unreachable runtime path |omitted case )/, "", line)
            sub(/ \(.*$/, "", line)
            print line
        }
    ' "$1" | sort
}

# The subjects whose JSON closure satisfies an awk condition on its fields.
holding() {
    awk -F'|' "$2"' { sub(/^[a-z_]+ /, "", $1); print $1 }' "$1" | sort
}

# The text report and the JSON report of one compile name the same claims in
# every per-claim section.
agree() {
    local report="$1" table="$2"
    [ "$(section "$report" 'Trust-dependent claims:')" = "$(holding "$table" '$8 == "empty:false"')" ] ||
        fail "$report and its JSON report disagree on the claims resting on trusted laws"
    [ "$(section "$report" 'Unsafe-dependent claims:')" = "$(holding "$table" '$3 != "unsafe:" || $6 ~ /unsafe=[^;]/')" ] ||
        fail "$report and its JSON report disagree on the claims resting on unsafe code"
    [ "$(section "$report" 'Library-model-dependent claims:')" = "$(holding "$table" '$4 != "models:" || $6 ~ /models=[^;]/')" ] ||
        fail "$report and its JSON report disagree on the claims resting on library models"
    [ "$(section "$report" 'Runtime-check-dependent claims:')" = "$(holding "$table" '$5 != "runtime:" || $6 ~ /runtime=[^;]/')" ] ||
        fail "$report and its JSON report disagree on the claims resting on runtime checks"
    [ "$(section "$report" 'Interface-dependent claims:')" = "$(holding "$table" '$6 != "imported:"')" ] ||
        fail "$report and its JSON report disagree on the claims resting on imported contracts"
    [ "$(section "$report" 'Assumption-free claims:')" = "$(holding "$table" '$7 == "free:true"')" ] ||
        fail "$report and its JSON report disagree on the assumption-free claims"
}

# expect_closures <JSON report>: the closures follow on stdin, in report order.
expect_closures() {
    closures "$1" > "$1.closures"
    if ! diff -u /dev/stdin "$1.closures" > "$1.diff"; then
        cat "$1.diff" >&2
        fail "the closures of $(basename "$1") are not the ones the fixture is built to have"
    fi
}

# --- One unit: every kind of dependency, every path to it ---------------------

"$CPPL" -std=c++20 "$FIXTURES/provenance_matrix.cpp" -o "$run/matrix" --cppl-trust-report \
    "--cppl-emit-trust-report=$run/matrix.json" > "$run/matrix.report" ||
    fail "provenance_matrix.cpp did not verify"
expect_closures "$run/matrix.json" <<'CLOSURES'
impossible_path trusted_source path 1|laws:broken_counter~|unsafe:|models:|runtime:|imported:|free:false|empty:false
impossible_path trusted_source_again path 1|laws:broken_again~|unsafe:|models:|runtime:|imported:|free:false|empty:false
proof counter_is_one|laws:broken_counter|unsafe:|models:|runtime:|imported:|free:false|empty:false
proof counter_is_two|laws:broken_again|unsafe:|models:|runtime:|imported:|free:false|empty:false
contract trusted_source|laws:broken_counter~|unsafe:|models:|runtime:|imported:|free:false|empty:false
contract trusted_source_again|laws:broken_again~|unsafe:|models:|runtime:|imported:|free:false|empty:false
contract unsafe_source|laws:|unsafe:line76|models:|runtime:|imported:|free:false|empty:true
contract vector_source|laws:|unsafe:|models:std::vector|runtime:|imported:|free:false|empty:true
contract string_source|laws:|unsafe:|models:std::basic_string<char>|runtime:|imported:|free:false|empty:true
contract validation_source|laws:|unsafe:|models:|runtime:Positive@line99|imported:|free:true|empty:true
contract trusted_hop_1|laws:broken_counter~|unsafe:|models:|runtime:|imported:|free:false|empty:false
contract trusted_hop_2|laws:broken_counter~|unsafe:|models:|runtime:|imported:|free:false|empty:false
contract trusted_hop_3|laws:broken_counter~|unsafe:|models:|runtime:|imported:|free:false|empty:false
contract trusted_right|laws:broken_counter~|unsafe:|models:|runtime:|imported:|free:false|empty:false
contract trusted_diamond|laws:broken_counter~|unsafe:|models:|runtime:|imported:|free:false|empty:false
contract unsafe_hop_1|laws:|unsafe:line76~|models:|runtime:|imported:|free:false|empty:true
contract unsafe_hop_2|laws:|unsafe:line76~|models:|runtime:|imported:|free:false|empty:true
contract unsafe_hop_3|laws:|unsafe:line76~|models:|runtime:|imported:|free:false|empty:true
contract unsafe_right|laws:|unsafe:line76~|models:|runtime:|imported:|free:false|empty:true
contract unsafe_diamond|laws:|unsafe:line76~|models:|runtime:|imported:|free:false|empty:true
contract vector_hop_1|laws:|unsafe:|models:std::vector~|runtime:|imported:|free:false|empty:true
contract vector_hop_2|laws:|unsafe:|models:std::vector~|runtime:|imported:|free:false|empty:true
contract vector_hop_3|laws:|unsafe:|models:std::vector~|runtime:|imported:|free:false|empty:true
contract vector_right|laws:|unsafe:|models:std::vector~|runtime:|imported:|free:false|empty:true
contract vector_diamond|laws:|unsafe:|models:std::vector~|runtime:|imported:|free:false|empty:true
contract validation_hop_1|laws:|unsafe:|models:|runtime:Positive@line99~|imported:|free:true|empty:true
contract validation_hop_2|laws:|unsafe:|models:|runtime:Positive@line99~|imported:|free:true|empty:true
contract validation_hop_3|laws:|unsafe:|models:|runtime:Positive@line99~|imported:|free:true|empty:true
contract validation_right|laws:|unsafe:|models:|runtime:Positive@line99~|imported:|free:true|empty:true
contract validation_diamond|laws:|unsafe:|models:|runtime:Positive@line99~|imported:|free:true|empty:true
contract trusted_even|laws:broken_counter~|unsafe:|models:|runtime:|imported:|free:false|empty:false
contract trusted_odd|laws:broken_counter~|unsafe:|models:|runtime:|imported:|free:false|empty:false
contract vector_even|laws:|unsafe:|models:std::vector~|runtime:|imported:|free:false|empty:true
contract vector_odd|laws:|unsafe:|models:std::vector~|runtime:|imported:|free:false|empty:true
contract two_laws|laws:broken_counter~,broken_again~|unsafe:|models:|runtime:|imported:|free:false|empty:false
contract two_models|laws:|unsafe:|models:std::vector~,std::basic_string<char>~|runtime:|imported:|free:false|empty:true
contract everything|laws:broken_counter~|unsafe:line76~|models:std::vector~,std::basic_string<char>~|runtime:Positive@line99~|imported:|free:false|empty:false
contract plain|laws:|unsafe:|models:|runtime:|imported:|free:true|empty:true
contract beside|laws:|unsafe:|models:|runtime:|imported:|free:true|empty:true
CLOSURES
agree "$run/matrix.report" "$run/matrix.json.closures"
grep -Eq '^Unused trusted laws: +1$' "$run/matrix.report" || fail "the unreached trusted law is not unused"
grep -Eq '^  unused: +never_reached ' "$run/matrix.report" || fail "never_reached is not the unused trusted law"
grep -Eq '^Unsafe regions: +1$' "$run/matrix.report" || fail "the unit's one unsafe block is not its one region"
grep -Eq '^Runtime validation sites: +1$' "$run/matrix.report" || fail "the unit's one validation is not its one site"
grep -Eq '^  partial correctness only: +7$' "$run/matrix.report" ||
    fail "the claims through the unsafe block are not partial correctness only"
[ "$("$run/matrix")" = '3 6 4 8 4 1 0 2 4 5 8 9 9 4 12' ] || fail "the program printed '$("$run/matrix")'"

# --- Three units: every kind of dependency through a record, and a record of
# --- a record -----------------------------------------------------------------

units="$run/units"
mkdir -p "$units"
cp "$FIXTURES"/provenance_matrix_cross_tu/* "$units/"
(
    cd "$units"
    "$CPPL" -std=c++20 -c producer.cpp -o producer.o --cppl-emit-interface=producer.cppli > producer.report
    "$CPPL" -std=c++20 -c middle.cpp -o middle.o --cppl-import-interface=producer.cppli \
        --cppl-emit-interface=middle.cppli --cppl-trust-report --cppl-emit-trust-report=middle.json > middle.report
    "$CPPL" -std=c++20 -c client.cpp -o client.o --cppl-import-interface=producer.cppli \
        --cppl-import-interface=middle.cppli --cppl-trust-report --cppl-emit-trust-report=client.json > client.report
    "$CLANG" producer.o middle.o client.o -o program
) || fail "the units of the cross-unit matrix did not verify and link"
expect_closures "$units/middle.json" <<'CLOSURES'
contract middle_trusted|laws:|unsafe:|models:|runtime:|imported:imported_trusted[laws=broken_counter;unsafe=;models=;runtime=;contracts=0]|free:false|empty:false
contract middle_all|laws:|unsafe:|models:|runtime:|imported:imported_model[laws=;unsafe=;models=std::vector model;runtime=;contracts=0],imported_plain[laws=;unsafe=;models=;runtime=;contracts=0],imported_trusted[laws=broken_counter;unsafe=;models=;runtime=;contracts=0],imported_unsafe[laws=;unsafe=producer.cpp:42;models=;runtime=;contracts=0],imported_validation[laws=;unsafe=;models=;runtime=Positive@producer.cpp:58;contracts=0]|free:false|empty:false
contract middle_plain|laws:|unsafe:|models:|runtime:|imported:imported_plain[laws=;unsafe=;models=;runtime=;contracts=0]|free:false|empty:true
CLOSURES
agree "$units/middle.report" "$units/middle.json.closures"
expect_closures "$units/client.json" <<'CLOSURES'
contract through_trusted|laws:|unsafe:|models:|runtime:|imported:imported_trusted[laws=broken_counter;unsafe=;models=;runtime=;contracts=0]|free:false|empty:false
contract through_unsafe|laws:|unsafe:|models:|runtime:|imported:imported_unsafe[laws=;unsafe=producer.cpp:42;models=;runtime=;contracts=0]|free:false|empty:true
contract through_model|laws:|unsafe:|models:|runtime:|imported:imported_model[laws=;unsafe=;models=std::vector model;runtime=;contracts=0]|free:false|empty:true
contract through_validation|laws:|unsafe:|models:|runtime:|imported:imported_validation[laws=;unsafe=;models=;runtime=Positive@producer.cpp:58;contracts=0]|free:false|empty:true
contract through_plain|laws:|unsafe:|models:|runtime:|imported:imported_plain[laws=;unsafe=;models=;runtime=;contracts=0]|free:false|empty:true
contract through_middle_trusted|laws:|unsafe:|models:|runtime:|imported:middle_trusted[laws=broken_counter;unsafe=;models=;runtime=;contracts=1]|free:false|empty:false
contract through_middle_all|laws:|unsafe:|models:|runtime:|imported:middle_all[laws=broken_counter;unsafe=producer.cpp:42;models=std::vector model;runtime=Positive@producer.cpp:58;contracts=5]|free:false|empty:false
contract through_middle_plain|laws:|unsafe:|models:|runtime:|imported:middle_plain[laws=;unsafe=;models=;runtime=;contracts=1]|free:false|empty:true
contract client_diamond|laws:|unsafe:|models:|runtime:|imported:imported_trusted[laws=broken_counter;unsafe=;models=;runtime=;contracts=0],middle_trusted[laws=broken_counter;unsafe=;models=;runtime=;contracts=1]|free:false|empty:false
contract client_local|laws:|unsafe:|models:|runtime:|imported:|free:true|empty:true
CLOSURES
agree "$units/client.report" "$units/client.json.closures"
# TCB-XTU-010: a claim through a record is never assumption-free, even a record
# that rests on nothing; the one claim using no record is.
[ "$(holding "$units/client.json.closures" '$7 == "free:true"')" = client_local ] ||
    fail "a claim through an imported record is reported assumption-free"
grep -Eq '^Function contracts imported: +8$' "$units/client.report" || fail "the client does not import eight contracts"
grep -Eq '^Interface provenance: +unauthenticated' "$units/client.report" ||
    fail "the client does not state interface provenance unauthenticated"
[ "$("$units/program")" = '3 4 2 1 5 6 6 7 4 9' ] || fail "the program of three units printed '$("$units/program")'"

echo 'every claim names exactly the closure it rests on, in both reports, alone and across units'
