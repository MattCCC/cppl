#!/usr/bin/env bash
# A refused twin for every accepted fixture.
#
# A fixture the compiler accepts shows that a program verifies; it does not
# show that verification looked at what the fixture claims. Its refused twin,
# the same program with one thing false, does: if the twin were accepted too,
# the fixture's acceptance would prove nothing. `fixtures/negative/twins/
# manifest.tsv` gives every fixture outside `negative/`, `subset/` (which
# carries its own twins, e2e/safety_subset.sh) and `include/`, and other than a
# hand-written erasure (`*.reference.cpp`), exactly one row:
#
#   twin      its twin, written out in negative/twins/, is compiled here with the
#             fixture: the fixture is accepted, the twin is refused with the
#             diagnostic the row names and writes no object. Units the row
#             imports are compiled first, each writing the verification
#             interface the next ones and both halves import;
#   covered   a refused counterpart written out in negative/ already exists, and
#             the negative or e2e script the row names compiles it;
#   refused   the fixture is itself the refused half of a pair, run by the
#             script the row names;
#   exempt    the fixture states no claim a twin could make false, for the
#             reason the row gives.
set -euo pipefail
# shellcheck source=../support/parallel.sh
source "$(dirname "$0")/../support/parallel.sh"

CPPL="$1"
FIXTURES="$2"
WORK="$3"
TESTS="$FIXTURES/.."
MANIFEST="$FIXTURES/negative/twins/manifest.tsv"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/refused-twins.XXXXXX")

fail() {
    echo "$1" >&2
    exit 1
}

rows() {
    grep -v -e '^#' -e '^$' "$MANIFEST"
}

# Every fixture has one row, and every row names a fixture.
(cd "$FIXTURES" && find . -name '*.cpp' ! -path './negative/*' ! -path './subset/*' ! -path './include/*' \
    ! -name '*.reference.cpp' | sed 's#^\./##' | sort) > "$run/fixtures"
rows | cut -f2 | sort > "$run/rows"
if ! diff -u "$run/fixtures" "$run/rows" > "$run/coverage.diff"; then
    cat "$run/coverage.diff" >&2
    fail "manifest.tsv does not give each fixture exactly one row"
fi
# Every written-out twin belongs to a row.
rows | awk -F'\t' '$1 == "twin" { print $3 }' | sort > "$run/listed"
(cd "$FIXTURES" && find negative/twins -name '*.cpp' | sort) > "$run/present"
if ! diff -u "$run/listed" "$run/present" > "$run/twins.diff"; then
    cat "$run/twins.diff" >&2
    fail "the twins in negative/twins/ are not exactly those manifest.tsv names"
fi

# compile <output> <source> <fixture> [arguments...]: succeeds when the compiler
# accepts <source>, read as <fixture> is: with the headers beside <fixture>.
compile() {
    local output="$1" source="$2" fixture="$3"
    shift 3
    "$CPPL" -std=c++23 -I"$FIXTURES" -I"$FIXTURES/include" -I"$FIXTURES/negative/twins/include" \
        -I"$(dirname "$FIXTURES/$fixture")" -c "$FIXTURES/$source" -o "$output.o" "$@" > "$output.report" 2> "$output.err"
}

# row <kind> <fixture> <counterpart> <detail> <imports> <flags>: checks one row.
# Each row compiles into a directory of its own, so the rows run side by side
# (support/parallel.sh) and are counted where they are started.
row() {
    local kind="$1" fixture="$2" counterpart="$3" detail="$4" imports="$5" flags="$6"
    case "$kind" in
        twin)
            stem="$run/$(printf '%s' "$fixture" | tr '/.' '__')"
            mkdir -p "$stem.units"
            arguments=()
            if [ -n "$flags" ] && [ "$flags" != - ]; then
                read -ra arguments <<< "$flags"
            fi
            if [ -n "$imports" ] && [ "$imports" != - ]; then
                IFS=, read -ra units <<< "$imports"
                for unit in "${units[@]}"; do
                    interface="$stem.units/$(basename "$unit" .cpp).cppli"
                    compile "$stem.units/$(basename "$unit" .cpp)" "$unit" "$unit" "${arguments[@]}" \
                        "--cppl-emit-interface=$interface" ||
                        fail "$fixture: the unit it imports, $unit, is refused"
                    arguments+=("--cppl-import-interface=$interface")
                done
            fi
            if ! compile "$stem.accepted" "$fixture" "$fixture" "${arguments[@]}"; then
                cat "$stem.accepted.err" >&2
                fail "$fixture is refused, so its twin shows nothing"
            fi
            if compile "$stem.twin" "$counterpart" "$fixture" "${arguments[@]}"; then
                fail "$counterpart, the twin of $fixture, is accepted"
            fi
            [ ! -e "$stem.twin.o" ] || fail "$counterpart wrote an object"
            # Refused by what it claims, never because it could not be read.
            if grep -q 'fatal error' "$stem.twin.err"; then
                cat "$stem.twin.err" >&2
                fail "$counterpart is refused before C++L reads it"
            fi
            if ! grep -Eq -- "$detail" "$stem.twin.err"; then
                cat "$stem.twin.err" >&2
                fail "$counterpart is refused, but not with '$detail'"
            fi
            ;;
        covered | refused)
            [ -f "$FIXTURES/$counterpart" ] || fail "$fixture: its counterpart $counterpart does not exist"
            [ -f "$TESTS/$detail" ] || fail "$fixture: the script $detail does not exist"
            # The script names the refused half: the counterpart of an accepted
            # fixture, or a refused fixture itself.
            refused_half="$counterpart"
            [ "$kind" = covered ] || refused_half="$fixture"
            grep -q -- "$(basename "$refused_half" .cpp)" "$TESTS/$detail" ||
                fail "$fixture: $detail does not compile $refused_half"
            ;;
        exempt)
            [ -n "$detail" ] && [ "$detail" != - ] || fail "$fixture is exempt without a reason"
            ;;
        *)
            fail "$fixture: unknown kind '$kind'"
            ;;
    esac
}

twins=0
covered=0
cases_begin "$run/cases"
while IFS=$'\t' read -r kind fixture counterpart detail imports flags; do
    case "$kind" in
        twin) twins=$((twins + 1)) ;;
        covered | refused) covered=$((covered + 1)) ;;
    esac
    case_run row "$kind" "$fixture" "$counterpart" "$detail" "$imports" "$flags"
done < <(rows)
cases_end

echo "refused twins: $twins compiled here, $covered run by the scripts that own them"
