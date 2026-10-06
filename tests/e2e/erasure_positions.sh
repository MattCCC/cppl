#!/usr/bin/env bash
# SPEC: ERASE-005, ERASE-018
# TRUST.md TCB-ERASE-012; ARCHITECTURE.md ARCH-ERASE-003
#
# Erasure moves no code to another line or column, and the program verified
# observes the positions the program run observes.
#
# `fixtures/positions/columns.cpp` writes ordinary C++ after a Law, a proof, a
# refinement on one line and one across lines, a validation and a verified
# body, a loop's clauses and a ghost declaration, all on the same line, and
# after an explicit instantiation of a verified template; each observes
# `__builtin_COLUMN()` or `__builtin_LINE()` as a template argument whose
# contract holds only at the position written beside it. The unit verifies only
# if the analysed program observes those positions, and it must print them as
# it runs. Each position written in the fixture must be where the preprocessed
# program has the observation, `columns.reference.cpp` (the unit erased by hand,
# column for column) must print the same and be the same text and code.
set -euo pipefail

CPPL="$1"
FIXTURES="$2/positions"
WORK="$3"
CLANG="$4"

# shellcheck source=../support/equivalence.sh
source "$(dirname "$0")/../support/equivalence.sh"
# shellcheck source=../support/parallel.sh
source "$(dirname "$0")/../support/parallel.sh"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/erasure-positions.XXXXXX")
cases_begin "$run/cases"

fixture="$FIXTURES/columns.cpp"
reference="$FIXTURES/columns.reference.cpp"
expected='72 92 83 55 3 131 86 4 51 46'

# Every column written beside a `__builtin_COLUMN()` is the column the
# preprocessed program has it at, and every line beside a `__builtin_LINE()` is
# its line, so the fixture states positions rather than numbers that happen to
# verify.
"$CLANG" -std=c++17 -E -P -w "$fixture" -o "$run/flat.ii"
written=$(awk '{
    rest = $0; offset = 0
    while ((at = index(rest, "at<__builtin_COLUMN(), ")) > 0) {
        tail = substr(rest, at + 23); split(tail, number, "u")
        printf "%d=%d\n", offset + at + 3, number[1]
        offset += at + 22; rest = substr(rest, at + 23)
    }
}' "$run/flat.ii")
test -n "$written"
while IFS='=' read -r column stated; do
    [ "$column" = "$stated" ] || {
        echo "a column written in the fixture, $stated, is not where the program observes it, $column" >&2
        exit 1
    }
done <<< "$written"
line=$(awk '/at<__builtin_LINE\(\), / { print NR; exit }' "$fixture")
grep -q "at<__builtin_LINE(), ${line}u>" "$fixture"

# compared <standard>
compared() {
    local standard="$1"
    local base="$run/$standard"
    local level output
    if ! "$CPPL" "-std=$standard" "$fixture" -o "$base.cppl" --cppl-trust-report \
        "--cppl-emit-projection=$base.runtime.ii" > "$base.report" 2> "$base.err"; then
        echo "the positions unit ($standard) was refused:" >&2
        cat "$base.err" >&2
        exit 1
    fi
    for line in 'Unresolved obligations: +0' 'Laws proven: +1' 'Function contracts proven: +13' \
        'Loop invariants proven: +2' 'Runtime validation sites: +1'; do
        if ! grep -Eq "^$line\$" "$base.report"; then
            echo "the positions unit ($standard) does not report '$line'" >&2
            cat "$base.report" >&2
            exit 1
        fi
    done
    output=$("$base.cppl")
    if [ "$output" != "$expected" ]; then
        printf 'the positions unit (%s) printed\n%s\nexpected\n%s\n' "$standard" "$output" "$expected" >&2
        exit 1
    fi
    "$CLANG" "-std=$standard" "$reference" -o "$base.reference"
    if [ "$("$base.reference")" != "$output" ]; then
        echo "the positions unit ($standard) and its reference behave differently" >&2
        exit 1
    fi
    tokens "$base.runtime.tokens" "$CLANG" "$standard" "$base.runtime.ii"
    tokens "$base.reference.tokens" "$CLANG" "$standard" "$reference"
    same_text "positions ($standard)" "$base.runtime.tokens" "$base.reference.tokens"
    for level in -O0 -O2; do
        assembly "$base$level.cppl" "$CPPL" "-std=$standard" "$level" "$fixture"
        assembly "$base$level.reference" "$CLANG" "-std=$standard" "$level" "$reference"
        same_code "positions ($standard, $level)" "$base$level.cppl" "$base$level.reference"
    done
}

for standard in c++17 c++20 c++23; do
    case_run compared "$standard"
done
cases_end

echo 'every position after a C++L construct is the same in the program verified, the program run and its erasure by hand'
