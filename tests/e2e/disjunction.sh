#!/usr/bin/env bash
set -euo pipefail
CPPL="$1"
FIXTURES="$2"
WORK="$3"
CLANG="$4"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/disjunction.XXXXXX")
for standard in c++17 c++20 c++23; do
    "$CPPL" "-std=$standard" "$FIXTURES/disjunction.cpp" -o "$run/program" \
        --cppl-trust-report "--cppl-emit-projection=$run/runtime.cpp" > "$run/report"
    grep -Eq '^Laws proven: +9$' "$run/report"
    grep -Eq '^  by a written proof: +1$' "$run/report"
    grep -Eq '^Proof declarations proven: +1$' "$run/report"
    grep -Eq '^Function contracts proven: +2$' "$run/report"
    grep -Eq '^Call preconditions proven: +1$' "$run/report"
    grep -Eq '^Laws trusted: +0$' "$run/report"
    grep -Eq '^Unresolved obligations: +0$' "$run/report"
    grep -Eq '^Trusted external axioms: +0$' "$run/report"
    test "$("$run/program")" = '2 1'
    "$CLANG" "-std=$standard" -x c++-cpp-output "$run/runtime.cpp" -o "$run/erased"
    test "$("$run/erased")" = '2 1'
done

reject() {
    local name="$1"
    cat > "$run/$name.cpp"
    echo 'int main() { return 0; }' >> "$run/$name.cpp"
    if "$CPPL" -std=c++17 "$run/$name.cpp" -o "$run/$name" > "$run/$name.log" 2>&1; then
        echo "invalid disjunction accepted: $name" >&2
        exit 1
    fi
    test ! -e "$run/$name"
    ! grep -q PROVEN "$run/$name.log"
    grep -q error "$run/$name.log"
}

# Neither side holds, so the disjunction does not.
reject no_side_holds <<'CPP'
law wrong(unsigned x) proves (x == 0u || x == 1u);
CPP

# A disjunction is not granted because one of its sides must be true. There is
# no excluded middle in this core, and none is assumed to close a goal.
reject excluded_middle <<'CPP'
law wrong(unsigned x) proves (x == 0u || x != 0u);
CPP

# Supposing a disjunction does not establish either of its sides.
reject side_from_a_premise <<'CPP'
law wrong(unsigned x) expects (x == 0u || x == 1u) proves (x == 0u);
CPP
reject right_side_from_a_premise <<'CPP'
law wrong(unsigned x) expects (x == 0u || x == 1u) proves (x == 1u);
CPP

# The conclusion has to follow from every side: here the second case fails.
reject one_case_fails <<'CPP'
law wrong(unsigned x) expects (x == 0u || x == 5u) proves (x <= 1u);
CPP

# A disjunctive precondition is not discharged by a call site that establishes
# neither side.
reject unproved_disjunctive_precondition <<'CPP'
verified unsigned f(unsigned x) expects (x == 1u || x == 2u) ensures (result != 0u) { return x; }
verified unsigned wrong(unsigned x) ensures (result == 0u) { return f(x) - f(x); }
CPP

# A false side of a contract's conclusion.
reject false_disjunctive_contract <<'CPP'
verified unsigned wrong(unsigned x) ensures (result == 1u || result == 2u) { return x; }
CPP

# Wrong evidence, and a written failure that does not fall back to automation.
reject wrong_evidence <<'CPP'
proof first(unsigned x) proves (Eq<unsigned>(x, x)) { refl; }
proof wrong(unsigned x) proves (Eq<unsigned>(x, x) || Eq<unsigned>(x, 1u)) { exact first(x); }
CPP
reject written_failure <<'CPP'
law valid(unsigned x) proves (x == x || x == 1u);
proof wrong(unsigned x) proves (valid(x)) { exact missing; }
CPP

# A binder a side quantifies over is not the law's parameter.
reject capture <<'CPP'
law wrong(unsigned x)
    expects (Eq<unsigned>(x, 0u))
    proves ((forall (unsigned x) { Eq<unsigned>(x, 0u) }) || Eq<unsigned>(x, 1u));
CPP

# Malformed operands, and a proposition where a value is required.
reject missing_side <<'CPP'
law wrong(unsigned x) proves (x == 0u ||);
CPP
reject wrong_type <<'CPP'
proof wrong(unsigned x) proves (Eq<unsigned>(x, x) || 7u) { refl; }
CPP
reject as_a_value <<'CPP'
verified bool wrong(unsigned x) ensures (result == x) { return x == x || x == x; }
CPP
# A condition is not a value position: `||` there is elaborated into the routes
# it selects between. The true route is the union of the two sides, so it
# establishes neither of them on its own.
reject as_a_condition_establishes_neither_side <<'CPP'
verified unsigned wrong(unsigned x) ensures (result == 0u) {
    if (x == 0u || x != 1u) return x;
    return 0u;
}
CPP
reject in_an_invariant <<'CPP'
verified unsigned wrong(unsigned x) ensures (result == x) {
    while (x != x) invariant (x == x || x != x) { }
    return x;
}
CPP
reject in_an_argument <<'CPP'
pure unsigned g(unsigned x) { return x; }
law wrong(unsigned x) proves (g(Eq<unsigned>(x, x) || Eq<unsigned>(x, 1u)) == x);
CPP

grep -q 'kernel-rejection' "$run/excluded_middle.log"
grep -q 'kernel-rejection' "$run/side_from_a_premise.log"
grep -q 'kernel-rejection' "$run/one_case_fails.log"
grep -q 'not modeled as a value' "$run/in_an_invariant.log"
grep -q 'nested or malformed formal syntax' "$run/in_an_argument.log"
grep -q 'no proof or assumed premise' "$run/written_failure.log"
