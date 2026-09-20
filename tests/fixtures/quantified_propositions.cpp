// Propositions that state more than a single equality: universal
// quantification (SPEC.md 8) and implication (SPEC.md 7.2.2).
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
law identity_everywhere(unsigned x) ensures(forall (unsigned y) { Eq<unsigned>(identity(y), y) });
proof identity_everywhere_holds(unsigned x) proves(identity_everywhere(x)) { refl; }

// Several binders, and a binder that shadows the law's own parameter. The
// proposition speaks about the binder; the parameter is out of reach of that
// name, exactly as in C++.
law order_is_free(unsigned x) ensures(forall (unsigned a, unsigned b) { Eq<unsigned>(a + b, b + a) });
proof order_is_free_holds(unsigned x) proves(order_is_free(x)) { refl; }

law shadowed(unsigned x) ensures(forall (unsigned x) { Eq<unsigned>(identity(x), x) });
proof shadowed_holds(unsigned x) proves(shadowed(x)) { refl; }

// Nesting, and an ordinary C++ comparison as the quantified body.
law nested(unsigned x) ensures(forall (unsigned a) { forall (unsigned b) { a + b == b + a } });
proof nested_holds(unsigned x) proves(nested(x)) { refl; }

// An implication states its conclusion under its premise and claims nothing
// about the premise itself.
law zero_increments(unsigned x) ensures(Eq<unsigned>(x, 0u) -> Eq<unsigned>(add_one(x), 1u));
proof zero_increments_holds(unsigned x) proves(zero_increments(x)) {
    assume h : Eq<unsigned>(x, 0u);
    rewrite h;
    refl;
}

// Implication is right associative and looser than every C++ operator, so this
// is `x == 0u -> (x == 0u -> identity(x) == 0u)`.
law chained(unsigned x) ensures(x == 0u -> x == 0u -> identity(x) == 0u);

// Quantification over an implication: the premise is available only under the
// binder it speaks about.
law guarded(unsigned x) ensures(forall (unsigned y) { Eq<unsigned>(y, 0u) -> Eq<unsigned>(add_one(y), 1u) });

// A premise supposed underneath a binder. `x` denotes the law's parameter here
// as it does anywhere else: the binder the proposition introduces stands
// between them without changing what the name means.
law guarded_by_the_parameter(unsigned x)
    ensures(forall (unsigned y) { Eq<unsigned>(x, 0u) -> Eq<unsigned>(add_one(x), add_one(y - y)) });
proof guarded_by_the_parameter_holds(unsigned x) proves(guarded_by_the_parameter(x)) {
    assume h : Eq<unsigned>(x, 0u);
    rewrite h;
    refl;
}

// A proof declaration states its own quantified proposition, with no law.
proof identity_is_identity(unsigned x) proves(forall (unsigned y) { Eq<unsigned>(identity(y), y) }) { refl; }

// Contracts carry the same propositions.
verified unsigned keep(unsigned x)
    expects(Eq<unsigned>(x, 2u) -> Eq<unsigned>(x, 2u))
    ensures(forall (unsigned y) { Eq<unsigned>(identity(y), y) }) {
    return x;
}

int main() {
    Holder holder{40u};
    Holder* pointer = &holder;
    // Runtime `->` is untouched: verification never reaches into the program.
    std::printf("%u %u %u\n", pointer->value, forall(1u), exists(keep(2u)));
}
