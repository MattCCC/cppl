#!/usr/bin/env bash
# SPEC: ERASE-005, ERASE-017
# TRUST.md TCB-ERASE-011; ARCHITECTURE.md ARCH-ERASE-003
#
# A preprocessor directive written inside a C++L construct survives erasure
# where it stands.
#
# `fixtures/directives/erasure.cpp` writes `#pragma pack` between a Law's
# clauses, through a macro's `_Pragma` there, in a proof's body, in a
# refinement's declaration, in a loop's clauses and between ghost declarations,
# and comments long enough inside a Law and a proof that the preprocessor writes
# a line marker after each. `erasure.reference.cpp` is the same unit erased by
# hand, line for line. In every supported standard the two must print the
# same, each record packed and each `__builtin_LINE()` the line it is written
# on; must be the same text once preprocessed and the same assembly at -O0 and
# -O2; and Clang must compile the C++L unit without a warning, which a
# `#pragma pack(pop)` left without its push would raise (C++L's own warning
# that `count` is proven for partial correctness only aside). The records' sizes
# are claimed by contracts too, so the program verified is packed as the
# program run (the refused twin in `negative/twins/` claims them unpacked).
set -euo pipefail

CPPL="$1"
FIXTURES="$2/directives"
WORK="$3"
CLANG="$4"

# shellcheck source=../support/equivalence.sh
source "$(dirname "$0")/../support/equivalence.sh"
# shellcheck source=../support/parallel.sh
source "$(dirname "$0")/../support/parallel.sh"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/erasure-directives.XXXXXX")
cases_begin "$run/cases"

fixture="$FIXTURES/erasure.cpp"
reference="$FIXTURES/erasure.reference.cpp"
proof_line=$(awk '/^unsigned after_proof\(\) \{$/ { print NR + 1; exit }' "$fixture")
law_line=$(awk '/^unsigned after_law\(\) \{$/ { print NR + 1; exit }' "$fixture")
test -n "$proof_line"
test -n "$law_line"
expected="5 5 5 5 5 6 3 $proof_line $law_line 5"

# compared <standard>
compared() {
    local standard="$1"
    local base="$run/$standard"
    local level output
    if ! "$CPPL" "-std=$standard" -Wall "$fixture" -o "$base.cppl" --cppl-trust-report \
        "--cppl-emit-projection=$base.runtime.ii" > "$base.report" 2> "$base.err"; then
        echo "the directives unit ($standard) was refused:" >&2
        cat "$base.err" >&2
        exit 1
    fi
    for line in 'Unresolved obligations: +0' 'Laws proven: +3' 'Function contracts proven: +3' \
        'Loop invariants proven: +2'; do
        if ! grep -Eq "^$line\$" "$base.report"; then
            echo "the directives unit ($standard) does not report '$line'" >&2
            cat "$base.report" >&2
            exit 1
        fi
    done
    # The one warning C++L itself gives: the loop in `count` states no
    # measure, so its contract is proven for partial correctness only (SPEC.md
    # CORRECT-002). Anything else on the error stream is Clang's.
    if ! grep -q "warning \[partial-correctness\]: the contract of 'count' is proven for partial correctness only" \
        "$base.err"; then
        echo "the directives unit ($standard) did not warn that 'count' is proven for partial correctness only:" >&2
        cat "$base.err" >&2
        exit 1
    fi
    grep -v -e "warning \[partial-correctness\]: the contract of 'count' is proven for partial correctness only" \
        -e "note: the loop that states no 'decreases'\$" "$base.err" > "$base.clang.err" || true
    if [ -s "$base.clang.err" ]; then
        echo "Clang warned compiling the directives unit ($standard):" >&2
        cat "$base.clang.err" >&2
        exit 1
    fi
    if grep -q '__cppl_' "$base.runtime.ii"; then
        echo "analysis scaffolding reached the runtime program ($standard)" >&2
        exit 1
    fi
    # Every directive stands on its own line, where the source wrote it.
    "$CLANG" "-std=$standard" -E -w "$fixture" -o "$base.preprocessed"
    if [ "$(wc -l < "$base.preprocessed")" -ne "$(wc -l < "$base.runtime.ii")" ]; then
        echo "erasure changed the number of lines in the program ($standard)" >&2
        exit 1
    fi
    if [ "$(grep -n '^#pragma' "$base.preprocessed")" != "$(grep -n '^#pragma' "$base.runtime.ii")" ]; then
        echo "erasure moved or dropped a directive ($standard)" >&2
        diff <(grep -n '^#pragma' "$base.preprocessed") <(grep -n '^#pragma' "$base.runtime.ii") >&2 || true
        exit 1
    fi

    tokens "$base.runtime.tokens" "$CLANG" "$standard" "$base.runtime.ii"
    tokens "$base.reference.tokens" "$CLANG" "$standard" "$reference"
    same_text "directives ($standard)" "$base.runtime.tokens" "$base.reference.tokens"

    "$CLANG" "-std=$standard" "$reference" -o "$base.reference"
    output=$("$base.cppl")
    if [ "$output" != "$expected" ]; then
        printf 'the directives unit (%s) printed\n%s\nexpected\n%s\n' "$standard" "$output" "$expected" >&2
        exit 1
    fi
    if [ "$("$base.reference")" != "$output" ]; then
        echo "the directives unit ($standard) and its reference behave differently" >&2
        exit 1
    fi
    for level in -O0 -O2; do
        assembly "$base$level.cppl" "$CPPL" "-std=$standard" "$level" "$fixture"
        assembly "$base$level.reference" "$CLANG" "-std=$standard" "$level" "$reference"
        same_code "directives ($standard, $level)" "$base$level.cppl" "$base$level.reference"
    done
}

for standard in c++17 c++20 c++23; do
    case_run compared "$standard"
done
cases_end

echo 'every directive inside a C++L construct stays where it stands, in the program run and the program verified'
