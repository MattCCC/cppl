#!/usr/bin/env bash
set -euo pipefail
CPPL="$1"
WORK="$3"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/quantified-propositions-negative.XXXXXX")
reject() {
    local name="$1"
    cat > "$run/$name.cpp"
    echo 'int main() { return 0; }' >> "$run/$name.cpp"
    if "$CPPL" -std=c++17 "$run/$name.cpp" -o "$run/$name" > "$run/$name.log" 2>&1; then
        echo "invalid quantified proposition accepted: $name" >&2
        exit 1
    fi
    test ! -e "$run/$name"
    ! grep -q 'PROVEN' "$run/$name.log"
    grep -q 'error' "$run/$name.log"
}

# False propositions, stated with a quantifier or an implication.
reject false_universal <<'CPP'
proof wrong(unsigned x) proves(forall (unsigned y) { Eq<unsigned>(y, x) }) { refl; }
CPP
reject false_implication <<'CPP'
law wrong(unsigned x) ensures(Eq<unsigned>(x, 0u) -> Eq<unsigned>(x, 1u));
CPP
reject false_under_a_binder <<'CPP'
law wrong(unsigned x) ensures(forall (unsigned y) { Eq<unsigned>(y, 0u) -> Eq<unsigned>(y, 1u) });
CPP

# A binder shadows the parameter, so a premise about the parameter says nothing
# about the variable the goal quantifies over.
reject shadowed_premise <<'CPP'
law wrong(unsigned x)
    expects(Eq<unsigned>(x, 0u))
    ensures(forall (unsigned x) { Eq<unsigned>(x, 0u) });
proof wrong_holds(unsigned x) proves(wrong(x)) {
    assume h : Eq<unsigned>(x, 0u);
    rewrite h;
    refl;
}
CPP

# A binder a proposition writes for itself has no name a statement can use.
reject binder_named_in_a_statement <<'CPP'
law l(unsigned x) ensures(forall (unsigned y) { Eq<unsigned>(y, y) });
proof l_holds(unsigned x) proves(l(x)) {
    assume h : Eq<unsigned>(y, y);
    exact h;
}
CPP

# Evidence that stays quantified cannot be instantiated at the goal's binder.
reject quantified_evidence <<'CPP'
pure unsigned identity(unsigned x) { return x; }
proof everywhere(unsigned x) proves(forall (unsigned y) { Eq<unsigned>(identity(y), y) }) { refl; }
proof wrong(unsigned x) proves(Eq<unsigned>(identity(x), x)) { exact everywhere(x); }
CPP

# Wrong evidence, and evidence at the wrong quantifier prefix.
reject wrong_evidence <<'CPP'
proof first(unsigned x) proves(Eq<unsigned>(x, x)) { refl; }
proof wrong(unsigned x) proves(forall (unsigned y) { Eq<unsigned>(y, y) }) { exact first(x); }
CPP

# Binder types the formal core does not model.
reject unmodeled_binder <<'CPP'
law wrong(unsigned x) ensures(forall (double y) { Eq<unsigned>(x, x) });
CPP
reject unmodeled_binder_class <<'CPP'
struct S {};
law wrong(unsigned x) ensures(forall (S y) { Eq<unsigned>(x, x) });
CPP
reject reference_binder <<'CPP'
law wrong(unsigned x) ensures(forall (unsigned& y) { Eq<unsigned>(y, y) });
CPP

# Malformed quantifier and implication syntax. None of it is guessed at.
reject no_binders <<'CPP'
law wrong(unsigned x) ensures(forall () { Eq<unsigned>(x, x) });
CPP
reject no_block <<'CPP'
law wrong(unsigned x) ensures(forall (unsigned y) Eq<unsigned>(y, y));
CPP
reject empty_block <<'CPP'
law wrong(unsigned x) ensures(forall (unsigned y) { });
CPP
reject missing_conclusion <<'CPP'
law wrong(unsigned x) ensures(Eq<unsigned>(x, 0u) ->);
CPP
reject missing_premise <<'CPP'
law wrong(unsigned x) ensures(-> Eq<unsigned>(x, 0u));
CPP
reject nested_in_an_argument <<'CPP'
pure unsigned g(unsigned x) { return x; }
law wrong(unsigned x) ensures(g(forall (unsigned y) { Eq<unsigned>(y, y) }) == x);
CPP

# Stated but not implemented, and refused rather than approximated.
reject existential <<'CPP'
law wrong(unsigned x) ensures(exists (unsigned y) { Eq<unsigned>(y, x) });
CPP
reject invariant <<'CPP'
verified unsigned wrong(unsigned x) ensures(result == x) {
    while (x == 1u) invariant(forall (unsigned y) { Eq<unsigned>(y, y) }) { }
    return x;
}
CPP

# Nesting beyond what the projection carries is refused, not truncated.
{
    printf 'law wrong(unsigned x) ensures('
    for _ in $(seq 1 200); do printf 'Eq<unsigned>(x, x) -> '; done
    printf 'Eq<unsigned>(x, x));\n'
} > "$run/deep.in"
reject deep_nesting < "$run/deep.in"

grep -q 'existential quantification is not supported yet' "$run/existential.log"
grep -q 'formal syntax in a loop invariant' "$run/invariant.log"
grep -q 'unsupported-semantics' "$run/no_block.log"
grep -q 'forall requires at least one binder' "$run/no_binders.log"
grep -q 'nested or malformed formal syntax' "$run/nested_in_an_argument.log"
grep -q 'binder type' "$run/unmodeled_binder.log"
grep -q 'no statement here can name' "$run/quantified_evidence.log"
grep -qE 'nesting|deeply' "$run/deep_nesting.log"
grep -q 'kernel-rejection' "$run/false_universal.log"
grep -q 'undeclared identifier' "$run/binder_named_in_a_statement.log"
