#!/usr/bin/env bash
# SPEC: CONSTRUCT-001, CONSTRUCT-151
# RFC 0022: the V1 verified C++ subset, one row for each construct of SPEC.md
# Annex X.
#
# `fixtures/subset/manifest.tsv` classifies every construct Annex X lists:
#
#   verified  a verified body may use the construct, and it is modeled:
#             `subset/<fixture>` is proven with nothing unresolved and runs
#             as its `main` expects, and its refused twin
#             `negative/subset/<fixture>`, the same program with one thing
#             false, is refused with the diagnostic the row names;
#   refused   a verified body that uses the construct is refused:
#             `negative/subset/<fixture>` is refused with the diagnostic the
#             row names.
#
# Either way a refused program writes no object. The manifest names each
# construct of Annex X once, and every fixture of either directory belongs to a
# row, so a construct the specification adds is not verified until it is
# classified here, and no fixture is left out of the matrix.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"
SPEC="$FIXTURES/../../docs/SPEC.md"
MANIFEST="$FIXTURES/subset/manifest.tsv"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/safety-subset.XXXXXX")

fail() {
    echo "$1" >&2
    exit 1
}

rows() {
    grep -v -e '^#' -e '^$' "$MANIFEST"
}

# Each construct of Annex X once, and nothing else.
grep -oE '\[CONSTRUCT-[0-9]{3}\]' "$SPEC" | tr -d '[]' | sort > "$run/specified"
rows | cut -f1 | sort > "$run/classified"
if ! diff -u "$run/specified" "$run/classified" > "$run/coverage.diff"; then
    cat "$run/coverage.diff" >&2
    fail "manifest.tsv does not classify each construct of SPEC.md Annex X exactly once"
fi
[ "$(wc -l < "$run/specified" | tr -d ' ')" = 151 ] || fail "SPEC.md Annex X no longer lists 151 constructs"

# Every fixture belongs to a row.
rows | cut -f3,4 | while IFS=$'\t' read -r verdict fixture; do
    printf 'negative/subset/%s\n' "$fixture"
    if [ "$verdict" = verified ]; then
        printf 'subset/%s\n' "$fixture"
    fi
done | sort > "$run/listed"
(cd "$FIXTURES" && find subset negative/subset -name '*.cpp' | sort) > "$run/present"
if ! diff -u "$run/listed" "$run/present" > "$run/fixtures.diff"; then
    cat "$run/fixtures.diff" >&2
    fail "the fixtures of the matrix are not exactly those manifest.tsv names"
fi

# compile <source> <stem>: succeeds when the compiler accepts the program.
compile() {
    "$CPPL" -std=c++23 -c "$1" -o "$run/$2.o" --cppl-trust-report > "$run/$2.report" 2> "$run/$2.err"
}

verified=0
refused=0
while IFS=$'\t' read -r id construct verdict fixture diagnostic; do
    stem="${fixture%.cpp}"
    case "$verdict" in
        verified)
            if ! compile "$FIXTURES/subset/$fixture" "$stem"; then
                cat "$run/$stem.err" >&2
                fail "$id ($construct): subset/$fixture is refused"
            fi
            grep -Eq '^Unresolved obligations: +0$' "$run/$stem.report" ||
                fail "$id ($construct): subset/$fixture leaves an obligation unresolved"
            "$CPPL" -std=c++23 "$FIXTURES/subset/$fixture" -o "$run/$stem" 2> "$run/$stem.link.err" ||
                fail "$id ($construct): subset/$fixture does not link"
            "$run/$stem" || fail "$id ($construct): subset/$fixture does not run as its main expects"
            verified=$((verified + 1))
            ;;
        refused)
            refused=$((refused + 1))
            ;;
        *)
            fail "$id ($construct): unknown verdict '$verdict'"
            ;;
    esac
    if compile "$FIXTURES/negative/subset/$fixture" "$stem.refused"; then
        fail "$id ($construct): negative/subset/$fixture is accepted"
    fi
    [ ! -e "$run/$stem.refused.o" ] || fail "$id ($construct): negative/subset/$fixture wrote an object"
    if ! grep -Eq -- "$diagnostic" "$run/$stem.refused.err"; then
        cat "$run/$stem.refused.err" >&2
        fail "$id ($construct): negative/subset/$fixture is refused, but not with '$diagnostic'"
    fi
done < <(rows)

echo "safety subset: $verified constructs verified, $refused refused"
