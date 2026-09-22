// Propositions that state more than a single equality: universal
// quantification (SPEC.md 8), implication (SPEC.md 8.2), and conjunction (SPEC.md 7.6).
//
// Both are written in the source and carried to the kernel as the quantifier
// and implication it already had. Nothing here introduces a C++ declaration
// named `forall`, `exists` or `->`: the names below are the program's own.

#include <cstdio>

// Ordinary C++ names stay ordinary outside the formal forms.
struct Holder {
    unsigned value;
};
pure unsigned forall(unsigned x) {
    return x;
}
pure unsigned exists(unsigned x) {
    return x + 1u;
}

pure unsigned identity(unsigned x) {
    return x;
}
pure unsigned add_one(unsigned x) {
    return x + 1u;
}

// A quantified law holds for every value of its binder, not only for the
// arguments a caller happens to supply.
law identity_everywhere(unsigned x)
    proves (forall (unsigned y) { Eq<unsigned>(identity(y), y) });
proof identity_everywhere_holds(unsigned x)
    proves (identity_everywhere(x))
{
    refl;
}

// Several binders, and a binder that shadows the law's own parameter. The
// proposition speaks about the binder; the parameter is out of reach of that
// name, exactly as in C++.
law order_is_free(unsigned x)
    proves (forall (unsigned a, unsigned b) { Eq<unsigned>(a + b, b + a) });
proof order_is_free_holds(unsigned x)
    proves (order_is_free(x))
{
    refl;
}

law shadowed(unsigned x)
    proves (forall (unsigned x) { Eq<unsigned>(identity(x), x) });
proof shadowed_holds(unsigned x)
    proves (shadowed(x))
{
    refl;
}

// Nesting, and an ordinary C++ comparison as the quantified body.
law nested(unsigned x)
    proves (forall (unsigned a) { forall (unsigned b) { a + b == b + a } });
proof nested_holds(unsigned x)
    proves (nested(x))
{
    refl;
}

// An implication states its conclusion under its premise and claims nothing
// about the premise itself.
law zero_increments(unsigned x)
    proves (Eq<unsigned>(x, 0u) -> Eq<unsigned>(add_one(x), 1u));
proof zero_increments_holds(unsigned x)
    proves (zero_increments(x))
{
    assume h : Eq<unsigned>(x, 0u);
    rewrite h;
    refl;
}

// Implication is right associative and looser than every C++ operator, so this
// is `x == 0u -> (x == 0u -> identity(x) == 0u)`.
law chained(unsigned x)
    proves (x == 0u -> x == 0u -> identity(x) == 0u);

// Quantification over an implication: the premise is available only under the
// binder it speaks about.
law guarded(unsigned x)
    proves (forall (unsigned y) { Eq<unsigned>(y, 0u) -> Eq<unsigned>(add_one(y), 1u) });

// A premise supposed underneath a binder. `x` denotes the law's parameter here
// as it does anywhere else: the binder the proposition introduces stands
// between them without changing what the name means.
law guarded_by_the_parameter(unsigned x)
    proves (forall (unsigned y) { Eq<unsigned>(x, 0u) -> Eq<unsigned>(add_one(x), add_one(y - y)) });
proof guarded_by_the_parameter_holds(unsigned x)
    proves (guarded_by_the_parameter(x))
{
    assume h : Eq<unsigned>(x, 0u);
    rewrite h;
    refl;
}

// A proof declaration states its own quantified proposition, with no law.
proof identity_is_identity(unsigned x)
    proves (forall (unsigned y) { Eq<unsigned>(identity(y), y) })
{
    refl;
}

// Conjunction states both of its sides, and evidence for it establishes either.
law both_sides(unsigned x)
    expects (x == 0u)
    proves (add_one(x) == 1u && identity(x) == 0u);
proof both_sides_holds(unsigned x)
    proves (both_sides(x))
{
    assume h : x == 0u;
    rewrite h;
    refl;
}

// A conjunctive premise is usable as each of its sides, and a conjunctive
// conclusion is established one side at a time.
law from_one_side(unsigned x, unsigned y)
    expects (x == 1u && y == 2u)
    proves (identity(x) == 1u);

law conjunction_chain(unsigned x)
    expects (x == 0u)
    proves (x == 0u && add_one(x) == 1u && identity(x) == 0u);

// Reusable evidence retains the whole conjunction and its dependencies.
proof both_reflexive(unsigned x)
    proves (x == x && identity(x) == x)
{
    refl;
}
proof reuse_both(unsigned x)
    proves (x == x && identity(x) == x)
{
    exact both_reflexive(x);
}
proof requiring_pair(unsigned x)
    proves (x == x && x + 1u == x + 1u -> identity(x) == x && add_one(x) == x + 1u)
{
    assume h : x == x&& x + 1u == x + 1u;
    refl;
}
proof apply_pair(unsigned x)
    proves (identity(x) == x && add_one(x) == x + 1u)
{
    apply requiring_pair(x);
    refl;
}

law from_right_side(unsigned x, unsigned y)
    expects (x == 1u && y == 2u)
    proves (identity(y) == 2u);
law nested_premise(unsigned x, unsigned y, unsigned z)
    expects (x == 1u && (y == 2u && z == 3u))
    proves (z == 3u && y == 2u);

law named_conjunction(unsigned x)
    expects (x == 0u && x != 1u)
    proves (x == 0u && x != 1u);
proof named_conjunction_holds(unsigned x)
    proves (named_conjunction(x))
{
    assume h : x == 0u && x != 1u;
    exact h;
}

// Conjunction is tighter than implication (GRAMMAR.md 33), so this supposes the
// conjunction and concludes the equality.
law tighter_than_implication(unsigned x)
    proves (x == 0u && x != 1u -> add_one(x) == 1u);

// Under a binder, and of a quantified body.
law everywhere_both(unsigned x)
    proves (forall (unsigned y) { identity(y) == y && add_one(y) == y + 1u });
law conjunction_under_binder(unsigned x)
    expects (x == 0u && x != 1u)
    proves (forall (unsigned y) { x == 0u && y == y });

law conjunctive_arithmetic(unsigned x)
    expects (x < 10u && x > 0u)
    proves (x + 1u <= 10u && x + 1u > 1u);

// Contracts carry the same propositions.
verified unsigned keep(unsigned x)
    expects (Eq<unsigned>(x, 2u) -> Eq<unsigned>(x, 2u))
    ensures (forall (unsigned y) { Eq<unsigned>(identity(y), y) })
{
    return x;
}

// A contract states a conjunction of what it guarantees, and supposes a
// conjunction of what it requires.
verified unsigned twice(unsigned x)
    expects (x == 1u && x != 0u)
    ensures (result == 2u && result != 0u)
{
    return x + x;
}

verified unsigned call_twice(unsigned x)
    expects (x == 1u && x != 0u)
    ensures (result == 2u && result != 0u)
{
    return twice(x);
}

verified unsigned preserve_path(unsigned x)
    ensures (result == x && result + 1u == x + 1u)
{
    if (x == 0u)
        return 0u;
    return x;
}

int main() {
    Holder holder{40u};
    Holder* pointer = &holder;
    // Runtime `->` is untouched: verification never reaches into the program.
    std::printf("%u %u %u %u\n", pointer->value, forall(1u), exists(keep(2u)), preserve_path(call_twice(1u)));
}
