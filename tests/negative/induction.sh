#!/usr/bin/env bash
# Induction that must be refused (SPEC.md 21, INDUCT-001 to INDUCT-005).
#
# Each case either removes one thing the accepted proofs in
# `fixtures/induction.cpp` rely on -- the hypothesis, the range premise, a case,
# the subject's principle -- or would let the principle establish something
# false. None may produce a program, and none may be described as proven.
set -euo pipefail

CPPL="$1"
FIXTURES="$2/negative"
WORK="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/induction.XXXXXX")

# Checks a failed compile of "$run/<name>.cpp" or a fixture: no program, no
# PROVEN, and the stated reason among the errors.
refused() {
    local name="$1" status="$2"
    shift 2
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
    if grep -q "internal error" "$run/$name.log"; then
        echo "$name aborted the compiler" >&2
        cat "$run/$name.log" >&2
        exit 1
    fi
    local diagnostic
    for diagnostic in "$@"; do
        if ! grep -qF "$diagnostic" "$run/$name.log"; then
            echo "$name did not fail for the stated reason: $diagnostic" >&2
            cat "$run/$name.log" >&2
            exit 1
        fi
    done
}

# refuse <fixture> <diagnostic>...: a fixture under fixtures/negative.
refuse() {
    local name="$1"
    shift
    local status=0
    "$CPPL" -std=c++17 "$FIXTURES/$name.cpp" -o "$run/$name" > "$run/$name.log" 2>&1 || status=$?
    refused "$name" "$status" "$@"
}

# reject <name> <diagnostic>... < source: a program written here.
reject() {
    local name="$1"
    shift
    {
        cat <<'CPP'
pure unsigned add(unsigned x, unsigned y) {
    return x + y;
}
CPP
        cat
        echo 'int main() { return 0; }'
    } > "$run/$name.cpp"
    local status=0
    "$CPPL" -std=c++17 "$run/$name.cpp" -o "$run/$name" > "$run/$name.log" 2>&1 || status=$?
    refused "$name" "$status" "$@"
}

# SPEC: INDUCT-001, INDUCT-005, CORPUS-021
# The refused half of `constant_from_steps`: the successor case without the
# hypothesis. The kernel refuses the evidence written for it.
refuse induction_without_hypothesis \
    "error [kernel-rejection]: proof 'constant_from_steps_by_induction' does not establish law 'constant_from_steps'"

# SPEC: INDUCT-003, INTERACT-026, EDGECASE-082
# A claim true everywhere but where `x + 1` wraps. The successor case is owed
# only below the maximum, and at `max - 1` it fails, written or automatic.
refuse induction_wraps \
    "induction_wraps.cpp:21:13: error [proof-failure]: 'hypothesis' does not prove what proof 'never_wraps_by_induction' claims" \
    "induction_wraps.cpp:32:5: error [proof-failure]: automation does not establish the 'successor' case of induction over 'x'"

# SPEC: INDUCT-002, INDUCT-003
# A claim false only at the maximum: the principle covers every value.
refuse induction_false_at_maximum \
    "error [proof-failure]: 'below' does not prove what proof 'below_maximum_by_induction' claims"

# SPEC: INDUCT-005
# A claim preserved by every step and false at 0: the zero case is owed too.
refuse induction_false_base \
    "error [proof-failure]: automation does not establish the 'zero' case of induction over 'x'"

# SPEC: INDUCT-001
# The subject is out of scope in its arms, and the hypothesis outside its own.
refuse induction_subject_in_arm "error [elaboration]: 'x' is not in scope inside the arms of its own induction"
refuse induction_hypothesis_outside_step \
    "error [elaboration]: no proof or assumed premise named 'hypothesis' is in scope here"

# SPEC: INDUCT-004
# Only an unsigned machine integer has a principle: not a signed integer, an
# enumeration, a refinement, a pointer or a class, whatever is claimed of it.
refuse induction_signed_subject "error [proof-failure]: induction over 'x' has no principle"
reject enumeration_subject "induction over 'm' has no principle: its type 'Mode' is not an unsigned integer type" <<'CPP'
enum class Mode : unsigned { idle = 0u, busy = 1u };
proof p(Mode m) proves (add(0u, 1u) == 1u) {
    induction m;
}
CPP
reject refined_subject "induction over 'x' has no principle" <<'CPP'
type Small = unsigned where (self < 10u);
proof p(Small x) proves (add(0u, 1u) == 1u) {
    induction x;
}
CPP
reject pointer_subject "induction over 'p' has no principle" <<'CPP'
proof q(unsigned* p) proves (add(0u, 1u) == 1u) {
    induction p;
}
CPP
reject record_subject "induction over 'r' has no principle" <<'CPP'
struct Row {
    unsigned first;
};
proof q(Row r) proves (add(0u, 1u) == 1u) {
    induction r;
}
CPP
reject char_subject "induction over 'c' has no principle" <<'CPP'
proof q(signed char c) proves (add(0u, 1u) == 1u) {
    induction c;
}
CPP

# SPEC: INDUCT-004
# The subject is a quantifier the claim states, so it is a parameter of the
# proof: never a quantifier binder, which has no name a statement can use, and
# never an arm's own binder.
reject binder_subject "use of undeclared identifier 'n'" <<'CPP'
proof q() proves (forall (unsigned n) { add(n, 0u) == n }) {
    induction n;
}
CPP
reject predecessor_subject "induction is over a parameter of proof 'q', and 'pred' is not one" <<'CPP'
proof q(unsigned x) proves (add(x, 0u) == x) {
    induction x {
        zero => {
            refl;
        }

        successor(pred) => {
            induction pred;
        }
    }
}
CPP

# The quantifier must still stand in the goal. A statement that introduced it
# leaves a variable, and induction over a variable would need the premises
# about it generalized first, which is not done.
reject after_assume "error [proof-failure]: induction over 'x' comes after a statement that already introduced it" <<'CPP'
law l(unsigned x) expects (x < 5u) proves (add(x, 0u) == x);
proof q(unsigned x) proves (l(x)) {
    assume small : x < 5u;
    induction x;
}
CPP

# SPEC: INDUCT-003, INDUCT-005
# The cases are `zero` and `successor(pred)`, each exactly once, and nothing
# else: no other label, no wildcard, no omitted case, no binder in `zero` and
# exactly one in `successor`.
reject other_label "the cases 'zero' and 'successor(pred)', and 'next' is not one" <<'CPP'
proof q(unsigned x) proves (add(x, 0u) == x) {
    induction x {
        zero => {
            refl;
        }

        next(pred) => {
            refl;
        }
    }
}
CPP
reject missing_successor "non-exhaustive induction: 'successor' has no arm" <<'CPP'
proof q(unsigned x) proves (add(x, 0u) == x) {
    induction x {
        zero => {
            refl;
        }
    }
}
CPP
reject missing_zero "non-exhaustive induction: 'zero' has no arm" <<'CPP'
proof q(unsigned x) proves (add(x, 0u) == x) {
    induction x {
        successor(pred) => {
            refl;
        }
    }
}
CPP
reject duplicate_zero "duplicate case 'zero'" <<'CPP'
proof q(unsigned x) proves (add(x, 0u) == x) {
    induction x {
        zero => {
            refl;
        }

        zero => {
            refl;
        }

        successor(pred) => {
            refl;
        }
    }
}
CPP
reject zero_binds "'zero' binds nothing" <<'CPP'
proof q(unsigned x) proves (add(x, 0u) == x) {
    induction x {
        zero(z) => {
            refl;
        }

        successor(pred) => {
            refl;
        }
    }
}
CPP
reject successor_binds_two "'successor' binds exactly one name, the predecessor" <<'CPP'
proof q(unsigned x) proves (add(x, 0u) == x) {
    induction x {
        zero => {
            refl;
        }

        successor(pred, hypothesis) => {
            refl;
        }
    }
}
CPP
reject binder_shadows "induction binder 'y' duplicates an enclosing value name" <<'CPP'
proof q(unsigned x, unsigned y) proves (add(x, y) == add(y, x)) {
    induction x {
        zero => {
            refl;
        }

        successor(y) => {
            refl;
        }
    }
}
CPP

# SPEC: INDUCT-001, INDUCT-003
# `assume` names a premise the case supplies, exactly: the zero case supplies
# none, and the successor case's hypothesis is the claim at `pred`, not at
# `pred + 1`.
reject hypothesis_in_zero "'hypothesis' has no matching premise to stand for" <<'CPP'
proof q(unsigned x) proves (add(x, 0u) == x) {
    induction x {
        zero => {
            assume hypothesis : add(0u, 0u) == 0u;
            refl;
        }

        successor(pred) => {
            refl;
        }
    }
}
CPP
reject hypothesis_at_the_successor "'hypothesis' has no matching premise to stand for" <<'CPP'
proof q(unsigned x) proves (add(x, 0u) == x) {
    induction x {
        zero => {
            refl;
        }

        successor(pred) => {
            assume hypothesis : add(pred + 1u, 0u) == pred + 1u;
            exact hypothesis;
        }
    }
}
CPP
reject weaker_range "'below' has no matching premise to stand for" <<'CPP'
proof q(unsigned x) proves (add(x, 0u) == x) {
    induction x {
        zero => {
            refl;
        }

        successor(pred) => {
            assume below : pred < 4294967296u;
            refl;
        }
    }
}
CPP

# SPEC: PROOFSRC-007, INDUCT-001
# A proof never obtains a hypothesis by naming itself, inside an arm or not.
reject self_reference "proof 'q' uses itself as its own evidence" <<'CPP'
proof q(unsigned x) proves (add(x, 0u) == x) {
    induction x {
        zero => {
            refl;
        }

        successor(pred) => {
            exact q(pred);
        }
    }
}
CPP

# An arm closes its goal and nothing follows it.
reject statement_after_close "induction arm has already closed its goal" <<'CPP'
proof q(unsigned x) proves (add(x, 0u) == x) {
    induction x {
        zero => {
            refl;
            refl;
        }

        successor(pred) => {
            refl;
        }
    }
}
CPP

echo 'induction is refused without its hypothesis, its range premise, a case or a principle, and cannot prove wrapping claims'
