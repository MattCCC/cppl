#!/usr/bin/env bash
# SPEC: CASE-004, CASE-005, CASE-011, CASE-013, CASE-016
# A case accounted for without an arm (GRAMMAR.md 5.7).
#
# The rejections carry the weight here. Accepting an omission is only sound if a
# case cannot be dropped when the evidence does not refute it, and if a merely
# absent arm is still non-exhaustive: otherwise an accidental omission and a
# proved impossibility would be indistinguishable, which is what CASE-005
# forbids.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/omitted_case.XXXXXX")

binary="$run/omitted_case"
report="$run/omitted_case.report"

"$CPPL" "$FIXTURES/omitted_case.cpp" -o "$binary" --cppl-trust-report \
    "--cppl-emit-projection=$run/runtime.cpp" > "$report"

# The omissions are discharged by the evidence the author wrote, not trusted.
# Each is an obligation of its own, counted under its own origin and apart from
# the laws it occurs in (SPEC.md CASE-012), and none is mistaken for an
# unreachable runtime path.
grep -Eq "^Laws proven: +5$" "$report"
grep -Eq "^ +by a written proof: +5$" "$report"
grep -Eq "^Omitted cases proven: +8$" "$report"
grep -Eq "^Impossible paths proven: +0$" "$report"
grep -Eq "^Unresolved obligations: +0$" "$report"
grep -Eq "^Laws trusted: +0$" "$report"
grep -Eq "^Trusted external axioms: +0$" "$report"
grep -Eq "^Trusted solvers: +0$" "$report"

output=$("$binary")
if [ "$output" != "0" ]; then
    echo "expected the verified program to print 0, got '$output'" >&2
    exit 1
fi

# The construct erases completely. The filename appears in '#line' directives,
# so the keywords are matched where they would actually be written.
if grep -qE '(^|[^_[:alnum:]])omit +[A-Za-z_]|(^|[^_[:alnum:]])cases +[A-Za-z_]|proves \(' \
    "$run/runtime.cpp"; then
    echo 'a proof construct survived into the erased translation unit' >&2
    exit 1
fi

# Every rejection - the refused half of the matched pair, an omission under a
# provable goal or a satisfiable premise, an absent arm, an arm and an omission
# for one case, and an unknown label - is written out as source in
# `fixtures/negative/`, driven by `negative/case_omissions.sh`, rather than
# produced here by substitution: a rewrite that matches more than it meant to
# silently tests a different program.

echo 'omitted cases are discharged by checked contradiction, and refused otherwise'
