#!/usr/bin/env bash
# Contradictions and case omissions that must be refused (SPEC.md CASE-004,
# CASE-005, CASE-011).
#
# `contradiction e;` and `omit label by contradiction e;` are written claims
# that a context cannot occur. None of these establishes that, so none may
# produce a program. The one that matters most is the refused half of a matched
# pair whose accepted half lives in `fixtures/omitted_case.cpp`; the two differ
# only in which case is omitted.
set -euo pipefail

CPPL="$1"
# These fixtures exist only to be refused, so they live apart from the ones that
# must compile: nothing here is ever expected to produce a program.
FIXTURES="$2/negative"
WORK="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/contradictions.XXXXXX")

# Compiles a fixture that must fail, then checks it failed for the stated
# reason rather than for some unrelated mismatch an "it errored" test would
# also accept.
refuse() {
    local name="$1"
    local diagnostic="$2"
    local status=0
    "$CPPL" -std=c++17 "$FIXTURES/$name.cpp" -o "$run/$name" > "$run/$name.log" 2>&1 || status=$?

    if [ "$status" -eq 0 ]; then
        echo "$name was accepted" >&2
        cat "$run/$name.log" >&2
        exit 1
    fi
    if [ -e "$run/$name" ]; then
        echo "a program was produced for $name" >&2
        exit 1
    fi
    if grep -q "PROVEN" "$run/$name.log"; then
        echo "$name was described as proven" >&2
        exit 1
    fi
    if ! grep -q "$diagnostic" "$run/$name.log"; then
        echo "$name did not fail for the stated reason" >&2
        cat "$run/$name.log" >&2
        exit 1
    fi
}

# SPEC: CASE-004, CASE-013
# The refused half of the matched pair. Same law, premise and evidence as the
# accepted half in `fixtures/omitted_case.cpp`; only which case is omitted
# differs. The failure is reported as the omission's, at the omission.
refuse rejected_omissions "omitted case 'State::running' is not shown to be impossible"
grep -q "rejected_omissions.cpp:35:9" "$run/rejected_omissions.log"

# SPEC: CASE-005, CASE-013
# An omission claims the case cannot occur, not that its goal can be proved.
# The goal here holds everywhere, so only that difference refuses this.
refuse omission_under_a_provable_goal "omitted case 'null' is not shown to be impossible"

# SPEC: CASE-015
# A premise that is merely satisfiable rules no case out.
refuse omission_without_contradiction "omitted case 'State::running' is not shown to be impossible"

# SPEC: CASE-004
# An omission names a case of the subject's own partition, or nothing.
refuse omission_of_unknown_case "a case label must be qualified"

# SPEC: CASE-005
# A case that is simply absent stays non-exhaustive, even with the evidence
# that would discharge it in scope. The engine never scans the context to
# decide a missing arm was meant.
refuse omission_absent "non-exhaustive cases: 'State::running' has no arm"

# SPEC: CASE-004
# A case is accounted for by an arm or an omission, never both.
refuse omission_and_arm "duplicate case 'State::running'"

# SPEC: CASE-011, CASE-015
# The statement form. A satisfiable premise closes nothing, and evidence that is
# not an equality states no contradiction.
refuse contradiction_satisfiable "'possible' does not state a contradiction"
refuse contradiction_not_an_equality "'quantified_evidence' does not establish an equality"

# SPEC: CASE-014
# A recorded limit of the formal core rather than of this implementation: a
# contradiction closes a goal only where it is built from equalities of
# integers, because no existing rule derives an equality of records from a
# false fact. Refusing is the safe direction; closing it would need a new rule.
refuse contradiction_structured_value_goal "a contradiction closes a goal only where that goal is built from equalities of integers"

echo 'contradictions and case omissions are refused unless the context genuinely cannot occur'
