#!/usr/bin/env bash
# SPEC: ERASE-005, ERASE-016, ERASEMATRIX-003
# ARCHITECTURE.md ARCH-ERASE-003; TRUST.md TCB-ERASE-006
#
# Erasure keeps the runtime program where the author wrote it.
#
# `fixtures/erasure_lines.cpp` spans several lines with a law, a trusted law, a
# proof, refinements, a contract, loop clauses and a claim, and after each one
# asks Clang, while it compiles the erased program, which line it stands on.
# Every answer must be the line in the fixture, a Clang warning on a line that
# lost its `verified` specifier must point at the column it was written at, and
# the erased program must have as many lines as the preprocessed source.
#
# Debug information must name the user's source rather than the scratch file
# the runtime program is compiled from, which is gone once the build ends and
# sits in a directory named at random, and two builds of the same source must
# produce the same object.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"
CLANG="$4"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/erasure-source-mapping.XXXXXX")
fixture="$FIXTURES/erasure_lines.cpp"

# The line after each `unsigned after_...() {` holds its `return __builtin_LINE();`.
expected_lines=""
for probe in after_law after_trusted_law after_proof after_refinements after_contract; do
    line=$(awk -v probe="unsigned $probe() {" 'index($0, probe) == 1 { print NR + 1; exit }' "$fixture")
    test -n "$line"
    expected_lines="$expected_lines$line "
done
expected="${expected_lines}10 3 $fixture"

warning_at=$(awk '/unused_second\)$/ { print NR ":" index($0, "unused_second"); exit }' "$fixture")
test -n "$warning_at"

for standard in c++17 c++20 c++23; do
    base="$run/lines-$standard"
    "$CPPL" "-std=$standard" -Wunused-parameter "$fixture" -o "$base" --cppl-trust-report \
        "--cppl-emit-projection=$base.runtime.ii" > "$base.report" 2> "$base.err"
    grep -Eq '^Unresolved obligations: +0$' "$base.report"
    grep -Eq '^Impossible paths proven: +1$' "$base.report"

    output=$("$base")
    if [ "$output" != "$expected" ]; then
        printf 'the erased program placed itself at\n%s\ninstead of\n%s\n(%s)\n' "$output" "$expected" "$standard" >&2
        exit 1
    fi

    if ! grep -Fq "$fixture:$warning_at: warning: unused parameter 'unused_second'" "$base.err"; then
        echo "Clang's warning on the erased program is not at $fixture:$warning_at ($standard)" >&2
        cat "$base.err" >&2
        exit 1
    fi

    "$CLANG" "-std=$standard" -E -w "$fixture" -o "$base.preprocessed"
    if [ "$(wc -l < "$base.preprocessed")" -ne "$(wc -l < "$base.runtime.ii")" ]; then
        echo "erasure changed the number of lines in the program ($standard)" >&2
        exit 1
    fi
done

# Debug information refers to the user's file, and never to the scratch copy
# the runtime program was compiled from.
"$CPPL" -std=c++17 -g -S "$fixture" -o "$run/debug.s"
if grep -Fq '.runtime.' "$run/debug.s"; then
    echo 'debug information names the scratch runtime file instead of the source' >&2
    grep -F '.runtime.' "$run/debug.s" | head -n 5 >&2
    exit 1
fi
grep -Fq 'erasure_lines.cpp' "$run/debug.s"

# The same source builds the same object, debug information included.
"$CPPL" -std=c++17 -g -c "$fixture" -o "$run/first.o"
"$CPPL" -std=c++17 -g -c "$fixture" -o "$run/second.o"
if ! cmp -s "$run/first.o" "$run/second.o"; then
    echo 'two builds of the same source produced different objects' >&2
    exit 1
fi

echo 'erasure keeps every runtime line and column, and debug information names the source'
