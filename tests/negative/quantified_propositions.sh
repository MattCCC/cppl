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
    if grep -q 'PROVEN' "$run/$name.log"; then
        echo "invalid quantified proposition described as proven: $name" >&2
        cat "$run/$name.log" >&2
        exit 1
    fi
    grep -q 'error' "$run/$name.log"
}

# False propositions, stated with a quantifier or an implication.
reject false_universal <<'CPP'
proof wrong(unsigned x) proves (forall (unsigned y) { Eq<unsigned>(y, x) }) { refl; }
CPP
reject false_implication <<'CPP'
law wrong(unsigned x) proves (Eq<unsigned>(x, 0u) -> Eq<unsigned>(x, 1u));
CPP
reject false_under_a_binder <<'CPP'
law wrong(unsigned x) proves (forall (unsigned y) { Eq<unsigned>(y, 0u) -> Eq<unsigned>(y, 1u) });
CPP

# A binder shadows the parameter, so a premise about the parameter says nothing
# about the variable the goal quantifies over.
reject shadowed_premise <<'CPP'
law wrong(unsigned x)
    expects (Eq<unsigned>(x, 0u))
    proves (forall (unsigned x) { Eq<unsigned>(x, 0u) });
proof wrong_holds(unsigned x) proves (wrong(x)) {
    assume h : Eq<unsigned>(x, 0u);
    rewrite h;
    refl;
}
CPP

# A binder a proposition writes for itself has no name a statement can use.
reject binder_named_in_a_statement <<'CPP'
law l(unsigned x) proves (forall (unsigned y) { Eq<unsigned>(y, y) });
proof l_holds(unsigned x) proves (l(x)) {
    assume h : Eq<unsigned>(y, y);
    exact h;
}
CPP

# Evidence that stays quantified cannot be instantiated at the goal's binder.
reject quantified_evidence <<'CPP'
pure unsigned identity(unsigned x) { return x; }
proof everywhere (unsigned x) proves (forall (unsigned y) { Eq<unsigned>(identity(y), y) }) { refl; }
proof wrong(unsigned x) proves (Eq<unsigned>(identity(x), x)) { exact everywhere (x); }
CPP

# Wrong evidence, and evidence at the wrong quantifier prefix.
reject wrong_evidence <<'CPP'
proof first(unsigned x) proves (Eq<unsigned>(x, x)) { refl; }
proof wrong(unsigned x) proves (forall (unsigned y) { Eq<unsigned>(y, y) }) { exact first(x); }
CPP

# Binder types the formal core does not model.
reject unmodeled_binder <<'CPP'
law wrong(unsigned x) proves (forall (double y) { Eq<unsigned>(x, x) });
CPP
reject unmodeled_binder_class <<'CPP'
struct S {};
law wrong(unsigned x) proves (forall (S y) { Eq<unsigned>(x, x) });
CPP
reject reference_binder <<'CPP'
law wrong(unsigned x) proves (forall (unsigned& y) { Eq<unsigned>(y, y) });
CPP

# Malformed quantifier and implication syntax. None of it is guessed at.
reject no_binders <<'CPP'
law wrong(unsigned x) proves (forall () { Eq<unsigned>(x, x) });
CPP
reject no_block <<'CPP'
law wrong(unsigned x) proves (forall (unsigned y) Eq<unsigned>(y, y));
CPP
reject empty_block <<'CPP'
law wrong(unsigned x) proves (forall (unsigned y) { });
CPP
reject missing_conclusion <<'CPP'
law wrong(unsigned x) proves (Eq<unsigned>(x, 0u) ->);
CPP
reject missing_premise <<'CPP'
law wrong(unsigned x) proves (-> Eq<unsigned>(x, 0u));
CPP
reject nested_in_an_argument <<'CPP'
pure unsigned g(unsigned x) { return x; }
law wrong(unsigned x) proves (g(forall (unsigned y) { Eq<unsigned>(y, y) }) == x);
CPP

# Stated but not implemented, and refused rather than approximated.
reject existential <<'CPP'
law wrong(unsigned x) proves (exists (unsigned y) { Eq<unsigned>(y, x) });
CPP
reject invariant <<'CPP'
verified unsigned wrong(unsigned x) ensures (result == x) {
    while (x == 1u) invariant (forall (unsigned y) { Eq<unsigned>(y, y) }) { }
    return x;
}
CPP

# Nesting beyond what the projection carries is refused, not truncated.
{
    printf 'law wrong(unsigned x) proves ('
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

# Both conjuncts require evidence. A true side cannot hide a false one.
reject false_left_conjunct <<'CPP'
law wrong(unsigned x) proves (x != x && x == x);
CPP
reject false_right_conjunct <<'CPP'
law wrong(unsigned x) proves (x == x && x != x);
CPP
reject false_nested_conjunct <<'CPP'
law wrong(unsigned x) proves (x == x && (x == x && x != x));
CPP
reject unrelated_conjunct <<'CPP'
law wrong(unsigned x, unsigned y) expects (x == 0u && y == 1u) proves (x == 1u);
CPP
reject conjunction_capture <<'CPP'
law wrong(unsigned x) expects (x == 0u && x != 1u)
    proves (forall (unsigned x) { x == 0u && x != 1u });
CPP
reject wrong_conjunction_evidence <<'CPP'
proof pair(unsigned x) proves (x == x && x + 1u == x + 1u) { refl; }
proof wrong(unsigned x) proves (x == x && x != x) { exact pair(x); }
CPP
reject equality_for_conjunction <<'CPP'
proof single(unsigned x) proves (x == x) { refl; }
proof wrong(unsigned x) proves (x == x && x == x) { exact single(x); }
CPP
reject wrong_written_conjunction <<'CPP'
law valid(unsigned x) proves (x == x && x == x);
proof wrong(unsigned x) proves (valid(x)) { exact missing; }
CPP
reject conjunction_cycle <<'CPP'
proof wrong(unsigned x) proves (x != x && x != x) { exact wrong(x); }
CPP
reject conjunction_wrong_type <<'CPP'
struct S {};
law wrong(S x) proves (x && x);
CPP
reject malformed_conjunction <<'CPP'
law wrong(unsigned x) proves (x == x &&);
CPP
reject false_conjunctive_contract <<'CPP'
verified unsigned wrong(unsigned x) ensures (result == x && result != x) { return x; }
CPP
reject unproved_conjunctive_precondition <<'CPP'
verified unsigned f(unsigned x) expects (x == 0u && x == 1u) ensures (result == x) { return x; }
verified unsigned wrong(unsigned x) ensures (result == x) { return f(x); }
CPP

# Value uses of a connective are refused explicitly.
reject conjunction_as_value <<'CPP'
pure bool wrong(bool x, bool y) { return x && y; }
law use(bool x, bool y) proves (wrong(x, y));
CPP
# A condition is not a value position: `&&` there is elaborated into the routes
# it selects between, so a conjunction is refused as a value and modeled as a
# condition. What it must not do is prove more than the routes state.
reject conjunction_as_condition_proves_no_more <<'CPP'
verified unsigned wrong(unsigned x) ensures (result == 0u) {
    if (x == 0u && x != 1u) return x;
    return x;
}
CPP
# A conjunctive invariant is each conjunct's own entry and preservation
# obligation, so every conjunct must hold. Here the second does not on entry.
reject false_conjunct_in_invariant <<'CPP'
verified unsigned wrong(unsigned n) ensures (result == n) {
    unsigned i = 0u;
    while (i < n) invariant (i <= n && i == 1u) { ++i; }
    return i;
}
CPP

grep -q 'kernel-rejection' "$run/false_right_conjunct.log"
grep -q 'kernel-rejection' "$run/conjunction_capture.log"
grep -q 'not modeled as a value' "$run/conjunction_as_value.log"

# SPEC: FORALL-001, REFINE-003
# A binder of a refinement type ranges over the refinement's values, never over
# its whole base type, and instantiating it at a term owes that the term is one
# of them. Each premise below is true and each conclusion false at 10u, so each
# was a false PROVEN while the binder ranged over every unsigned.
reject refined_binder_instantiated_outside <<'CPP'
type Small = unsigned where (self < 10u);
pure unsigned twice(unsigned x) { return x + x; }
law twice_bounded_everywhere(unsigned n)
    expects (forall (Small s) { twice(s) < 20u })
    proves (twice(n) < 20u)
{
    assume bounded : forall (Small s) { twice(s) < 20u };
    exact bounded(n);
}
CPP
reject refined_binder_every_unsigned <<'CPP'
type Small = unsigned where (self < 10u);
law every_unsigned_is_small(unsigned n)
    expects (forall (Small s) { s < 10u })
    proves (n < 10u)
{
    assume all_small : forall (Small s) { s < 10u };
    exact all_small(n);
}
CPP
reject refined_binder_contradiction <<'CPP'
type Small = unsigned where (self < 10u);
law small_premise()
    expects (forall (Small s) { s < 10u })
    proves (1u == 2u)
{
    assume h : forall (Small s) { s < 10u };
    contradiction h(10u);
}
CPP
reject refined_binder_contradiction_in_implication <<'CPP'
type Small = unsigned where (self < 10u);
proof small_implication()
    proves ((forall (Small s) { s < 10u }) -> 1u == 2u)
{
    assume h : forall (Small s) { s < 10u };
    contradiction h(10u);
}
CPP
# The membership an application owes is a goal like any other: left open, or
# closed by a premise that does not establish it, the proof is refused.
reject refined_binder_membership_left_open <<'CPP'
type Small = unsigned where (self < 10u);
pure unsigned twice(unsigned x) { return x + x; }
law left_open(unsigned n)
    proves ((forall (Small s) { twice(s) < 20u }) -> twice(n) < 20u)
{
    assume bounded : forall (Small s) { twice(s) < 20u };
    apply bounded(n);
}
CPP
reject refined_binder_membership_not_established <<'CPP'
type Small = unsigned where (self < 10u);
pure unsigned twice(unsigned x) { return x + x; }
law near(unsigned n)
    expects (n < 11u)
    proves ((forall (Small s) { twice(s) < 20u }) -> twice(n) < 20u)
{
    assume near : n < 11u;
    assume bounded : forall (Small s) { twice(s) < 20u };
    apply bounded(n);
    exact near;
}
CPP
reject refined_binder_membership_false_literal <<'CPP'
type Small = unsigned where (self < 10u);
pure unsigned twice(unsigned x) { return x + x; }
proof at_ten()
    proves ((forall (Small s) { twice(s) < 20u }) -> twice(10u) < 20u)
{
    assume bounded : forall (Small s) { twice(s) < 20u };
    apply bounded(10u);
    refl;
}
CPP
# Every predicate of a refinement of a refinement, and an index's value, is part
# of the range.
reject refined_binder_nested_refinement <<'CPP'
type Small = unsigned where (self < 10u);
type Tiny = Small where (self < 4u);
law tiny_outside(unsigned n)
    expects (forall (Tiny t) { t < 4u })
    proves (n < 10u -> n < 4u)
{
    assume all_tiny : forall (Tiny t) { t < 4u };
    assume small : n < 10u;
    apply all_tiny(n);
    exact small;
}
CPP
reject refined_binder_indexed <<'CPP'
type Below(unsigned n) = unsigned where (self < n);
law below_outside(unsigned n)
    expects (forall (Below<4> b) { b < 4u })
    proves (n < 4u)
{
    assume all_below : forall (Below<4> b) { b < 4u };
    exact all_below(n);
}
CPP
# A law's own refined parameter ranges over the refinement too: proven or
# trusted, the law is used at a term only once the term's membership is shown.
reject refined_parameter_proven_law_outside <<'CPP'
type Small = unsigned where (self < 10u);
proof small_below(Small s) proves (s < 10u) {
    assume small : s < 10u;
    exact small;
}
proof every_unsigned(unsigned n) proves (n < 10u) {
    exact small_below(n);
}
CPP
reject refined_parameter_trusted_law_outside <<'CPP'
type Small = unsigned where (self < 10u);
trusted law small_below(Small s) proves (s < 10u);
proof every_unsigned(unsigned n) proves (n < 10u) {
    exact small_below(n);
}
CPP
reject refined_parameter_trusted_law_contradiction <<'CPP'
type Small = unsigned where (self < 10u);
trusted law small_below(Small s) proves (s < 10u);
proof one_is_two() proves (1u == 2u) {
    contradiction small_below(10u);
}
CPP
# A path a verified body claims cannot occur, by a proof over a refined
# parameter instantiated at a value the path does not show to be a member: the
# path `x >= 10u` occurs, so the contract below is false.
reject refined_parameter_path_claim <<'CPP'
type Small = unsigned where (self < 10u);
trusted law small_below(Small s) proves (s < 10u);
proof below(Small s) proves (s < 10u) {
    exact small_below(s);
}
verified unsigned below_ten(unsigned x)
    ensures (result < 10u)
{
    if (x >= 10u) {
        contradiction below(x);
    }
    return x;
}
CPP
# `Eq<R>` equates two values of `R`. Its operands are not shown to be values of
# it, so it is refused rather than stated of values outside the type.
reject refined_formal_equality <<'CPP'
type Small = unsigned where (self < 10u);
proof eq_small() proves (Eq<Small>(20u, 20u)) { refl; }
CPP
reject refined_formal_equality_of_a_parameter <<'CPP'
type Small = unsigned where (self < 10u);
law eq_self(Small s) proves (Eq<Small>(s, s));
CPP

for name in refined_binder_instantiated_outside refined_binder_every_unsigned refined_parameter_proven_law_outside \
    refined_parameter_trusted_law_outside refined_binder_indexed; do
    grep -q 'does not prove what proof' "$run/$name.log"
done
for name in refined_binder_contradiction refined_binder_contradiction_in_implication \
    refined_parameter_trusted_law_contradiction refined_parameter_path_claim; do
    grep -q 'does not establish an equality, so it cannot state a contradiction' "$run/$name.log"
done
grep -q "leaves a goal open" "$run/refined_binder_membership_left_open.log"
grep -q "'near' does not prove what proof 'near' claims" "$run/refined_binder_membership_not_established.log"
grep -q "kernel-rejection" "$run/refined_binder_membership_false_literal.log"
grep -q "'small' does not prove what proof 'tiny_outside' claims" "$run/refined_binder_nested_refinement.log"
for name in refined_formal_equality refined_formal_equality_of_a_parameter; do
    grep -q "formal equality at 'Small' equates values of a refinement type" "$run/$name.log"
done
