#!/usr/bin/env bash
set -euo pipefail
CPPL="$1"
FIXTURES="$2"
WORK="$3"
CLANG="$4"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/logical-composition.XXXXXX")
for standard in c++17 c++20 c++23; do
    "$CPPL" "-std=$standard" "$FIXTURES/logical_composition.cpp" -o "$run/program" \
        --cppl-trust-report "--cppl-emit-projection=$run/runtime.cpp" > "$run/report"
    grep -Eq '^Laws proven: +5$' "$run/report"
    grep -Eq '^Proof declarations proven: +8$' "$run/report"
    grep -Eq '^Function contracts proven: +1$' "$run/report"
    grep -Eq '^Unresolved obligations: +0$' "$run/report"
    test "$("$run/program")" = '7 1'
    "$CLANG" "-std=$standard" -x c++-cpp-output "$run/runtime.cpp" -o "$run/erased"
    test "$("$run/erased")" = '7 1'
done

reject() {
    local name="$1"
    cat > "$run/$name.cpp"
    echo 'int main() { return 0; }' >> "$run/$name.cpp"
    if "$CPPL" -std=c++17 "$run/$name.cpp" -o "$run/$name" > "$run/$name.log" 2>&1; then
        echo "invalid logical composition accepted: $name" >&2
        exit 1
    fi
    test ! -e "$run/$name"
    ! grep -q PROVEN "$run/$name.log"
    grep -q error "$run/$name.log"
}
reject false_conjunction <<'CPP'
proof wrong(unsigned x) proves(Eq<unsigned>(x, x) && Eq<unsigned>(0u, 1u)) { refl; }
CPP
reject one_way_equivalence <<'CPP'
law wrong(unsigned x) ensures(Eq<unsigned>(x, 0u) <-> Eq<unsigned>(x, x));
CPP
reject reverse_one_way <<'CPP'
law wrong(unsigned x) ensures(Eq<unsigned>(x, x) <-> Eq<unsigned>(x, 0u));
CPP
reject captured_conjunct <<'CPP'
law wrong(unsigned x) expects(Eq<unsigned>(x, 0u))
    ensures(Eq<unsigned>(x, 0u) && (forall (unsigned x) { Eq<unsigned>(x, 0u) }));
CPP
reject wrong_evidence <<'CPP'
proof first(unsigned x) proves(Eq<unsigned>(x, x)) { refl; }
proof wrong(unsigned x) proves(Eq<unsigned>(x, x) <-> Eq<unsigned>(x, x)) { exact first(x); }
CPP
reject wrong_type <<'CPP'
proof wrong(unsigned x) proves(Eq<unsigned>(x, x) && 7u) { refl; }
CPP
reject missing_side <<'CPP'
law wrong(unsigned x) ensures(Eq<unsigned>(x, x) <->);
CPP
reject formal_as_value <<'CPP'
pure bool take(bool x) { return x; }
law wrong(unsigned x) ensures(take(Eq<unsigned>(x, x) && Eq<unsigned>(x, x)));
CPP
reject written_failure <<'CPP'
law valid(unsigned x) ensures(Eq<unsigned>(x, x) <-> Eq<unsigned>(x, x));
proof wrong(unsigned x) proves(valid(x)) { exact missing; }
CPP

# Equivalence is looser than implication, so this states
# `(x == 0u -> x == x) <-> x == 0u`, which is false at every other value. The
# tighter reading `x == 0u -> (x == x <-> x == 0u)` would hold.
reject equivalence_is_looser <<'CPP'
law wrong(unsigned x) ensures(x == 0u -> x == x <-> x == 0u);
CPP

# A boundary, not a soundness claim: no written spelling projects one side out
# of a conjunctive premise. Automation does it, and the kernel checks it.
reject written_conjunction_side <<'CPP'
law valid(unsigned x) expects(Eq<unsigned>(x, 0u) && Eq<unsigned>(x + 1u, 1u)) ensures(Eq<unsigned>(x, 0u));
proof wrong(unsigned x) proves(valid(x)) {
    assume h : Eq<unsigned>(x, 0u) && Eq<unsigned>(x + 1u, 1u);
    exact h;
}
CPP

# A short chain must not expand exponentially before a resource refusal.
{
    printf 'law large(unsigned x) ensures(Eq<unsigned>(x, x)'
    for _ in $(seq 1 25); do printf ' <-> Eq<unsigned>(x, x)'; done
    printf ');\n'
} > "$run/large.in"
reject bounded_expansion < "$run/large.in"
grep -q 'expansion exceeds' "$run/bounded_expansion.log"
grep -q 'kernel-rejection' "$run/equivalence_is_looser.log"
grep -q 'does not prove what proof' "$run/written_conjunction_side.log"
