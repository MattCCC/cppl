#!/usr/bin/env bash
# SPEC: CASE-011, CASE-014
# `contradiction e;` closes a goal of any shape from a premise no value
# satisfies, adding no axiom and no trusted mechanism (GRAMMAR.md 5.6, SPEC.md
# CASE-011): the premise is refuted into `False` and the goal eliminated from it.
# The rejections matter as much as the acceptance, and are written out as source
# in `fixtures/negative/`, driven by `negative/contradictions.sh`.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/contradiction.XXXXXX")

binary="$run/contradiction"
report="$run/contradiction.report"

"$CPPL" "$FIXTURES/contradiction.cpp" -o "$binary" --cppl-trust-report \
    "--cppl-emit-projection=$run/runtime.cpp" > "$report"

# Every law with a written proof is closed by the evidence the author wrote, the
# one without is closed by automation the same way, and the contradiction is
# derived rather than trusted. Two of the goals equate records, which no
# arithmetic states. A plain `contradiction` statement makes no separate claim,
# so no omitted case or impossible path is counted.
grep -Eq "^Laws proven: +5$" "$report"
grep -Eq "^ +by a written proof: +4$" "$report"
grep -Eq "^Omitted cases proven: +0$" "$report"
grep -Eq "^Impossible paths proven: +0$" "$report"
grep -Eq "^Unresolved obligations: +0$" "$report"
grep -Eq "^Laws trusted: +0$" "$report"
grep -Eq "^Trusted external axioms: +0$" "$report"
grep -Eq "^Trusted solvers: +0$" "$report"

output=$("$binary")
if [ "$output" != "1" ]; then
    echo "expected the verified program to print 1, got '$output'" >&2
    exit 1
fi

# The statement erases completely: no proof construct reaches the runtime. The
# filename appears in '#line' directives, so the statement keyword is matched
# where it would actually be written rather than anywhere in the text.
if grep -qE '(^|[^_[:alnum:]])contradiction +[A-Za-z_]|proves \(|(^|[^_[:alnum:]])assume +[A-Za-z_]+ *:' \
    "$run/runtime.cpp"; then
    echo 'a proof construct survived into the erased translation unit' >&2
    exit 1
fi

echo 'contradiction discharges a goal from a false premise and erases completely'
