#!/usr/bin/env bash
# SPEC: INDUCT-001, INDUCT-002, INDUCT-003, INDUCT-005, ERASE-002, CORPUS-021
#
# Induction over unsigned machine integers, end to end: every proof in
# `fixtures/induction.cpp` is established by evidence the kernel checks with its
# unsigned induction rule, nothing rests on an assumption, and the program runs.
# Then the erasure: `fixtures/equivalence/induction.cpp` and its erasure by hand,
# `induction.reference.cpp`, behave the same and are the same code, in every
# supported standard, at -O0 and -O2. The refusals are `negative/induction.sh`.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"
CLANG="$4"
# shellcheck source=../support/equivalence.sh
source "$(dirname "$0")/../support/equivalence.sh"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/induction.XXXXXX")

expect() {
    local pattern="$1" report="$2"
    if ! grep -Eq "$pattern" "$report"; then
        echo "the report does not state: $pattern" >&2
        cat "$report" >&2
        exit 1
    fi
}

for standard in c++17 c++20 c++23; do
    base="$run/induction-$standard"
    "$CPPL" "-std=$standard" "$FIXTURES/induction.cpp" -o "$base" --cppl-trust-report \
        "--cppl-emit-projection=$base.runtime.ii" > "$base.report"

    # Six laws, each by its written proof, and one proposition proved directly;
    # the two short forms among them were closed by automation's evidence for
    # each case, checked by the kernel like the written arms.
    expect '^Laws proven: +6$' "$base.report"
    expect '^  by a written proof: +6$' "$base.report"
    expect '^Proof declarations proven: +1$' "$base.report"
    expect '^Unresolved obligations: +0$' "$base.report"
    expect '^Laws trusted: +0$' "$base.report"
    expect '^Trust-dependent claims: +0$' "$base.report"
    expect '^Trusted external axioms: +0$' "$base.report"
    expect '^Trusted solvers: +0$' "$base.report"
    expect '^Formal core version: +cppl-core-0\.9\.0$' "$base.report"

    output=$("$base")
    if [ "$output" != "2 3 18 42" ]; then
        echo "expected the verified program to print '2 3 18 42', got '$output' ($standard)" >&2
        exit 1
    fi

    # No induction statement, arm or premise name reaches the runtime program.
    # Line markers name source files, whose paths may contain any word.
    if program=$(grep -v '^# [0-9]' "$base.runtime.ii") &&
        grep -Eq '(^|[^_[:alnum:]])induction +[A-Za-z_]|(zero|successor)(\([A-Za-z_]+\))? *=>|proves \(|__cppl_' \
            <<< "$program"; then
        echo "a proof construct survived into the erased translation unit ($standard)" >&2
        exit 1
    fi
done

for standard in c++17 c++20 c++23; do
    base="$run/equivalence-$standard"
    "$CPPL" "-std=$standard" "$FIXTURES/equivalence/induction.cpp" -o "$base.cppl" --cppl-trust-report \
        > "$base.report"
    expect '^Laws proven: +3$' "$base.report"
    expect '^Unresolved obligations: +0$' "$base.report"

    "$CLANG" "-std=$standard" "$FIXTURES/equivalence/induction.reference.cpp" -o "$base.reference"
    output=$("$base.cppl")
    if [ "$output" != "5 12 45" ] || [ "$("$base.reference")" != "$output" ]; then
        echo "the induction program and its erasure by hand behave differently ($standard)" >&2
        exit 1
    fi
    for level in -O0 -O2; do
        assembly "$base$level.cppl" "$CPPL" "-std=$standard" "$level" "$FIXTURES/equivalence/induction.cpp"
        assembly "$base$level.reference" "$CLANG" "-std=$standard" "$level" \
            "$FIXTURES/equivalence/induction.reference.cpp"
        same_code "induction ($standard, $level)" "$base$level.cppl" "$base$level.reference"
    done
done

echo 'induction over unsigned integers is kernel-checked, assumption-free, and erases to the same code'
