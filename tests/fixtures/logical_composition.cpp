// Formal propositions composing through conjunction and equivalence
// (SPEC.md 7.6-7.7, GRAMMAR.md 30, 33). Equivalence is the conjunction of both
// implications, so nothing here needs a kernel rule that conjunction and
// implication did not already have.

#include <cstdio>

pure unsigned identity(unsigned x) {
    return x;
}

// An ordinary C++ template may be named `Eq`. It keeps its runtime meaning
// below; inside a proposition the spelling is the formal form.
template <class T> bool Eq(T a, T b) {
    return a == b;
}

proof formal_pair(unsigned x)
    proves(Eq<unsigned>(identity(x), x) && Eq<unsigned>(x, x))
{
    refl;
}
proof reuse_pair(unsigned x)
    proves(Eq<unsigned>(identity(x), x) && Eq<unsigned>(x, x))
{
    exact formal_pair(x);
}
proof applied_pair(unsigned x)
    proves(Eq<unsigned>(identity(x), x) && Eq<unsigned>(x, x))
{
    apply formal_pair(x);
}

// One side written formally and the other as an ordinary C++ predicate.
proof mixed_pair(unsigned x)
    proves(Eq<unsigned>(identity(x), x) && x == x)
{
    refl;
}

proof quantified_pair(unsigned x)
    proves((forall (unsigned y) { Eq<unsigned>(y, y) }) && Eq<unsigned>(x, x))
{
    refl;
}
proof rewrite_pair(unsigned x)
    proves(Eq<unsigned>(x, 0u) -> (Eq<unsigned>(identity(x), 0u) && Eq<unsigned>(x + 1u, 1u)))
{
    assume h : Eq<unsigned>(x, 0u);
    rewrite h;
    refl;
}

law equivalent(unsigned x)
    ensures(Eq<unsigned>(x, 0u) <-> Eq<unsigned>(x, 0u));
law resolved_equivalence(unsigned x)
    ensures(x == 0u <-> identity(x) == 0u);
law precedence(unsigned x)
    ensures(x == 0u -> x == 0u <-> x == x);

// Provable only because `&&` binds tighter than `<->`: the looser reading
// `x == 0u && (x == x <-> x == 0u)` is false at every other value.
law conjunction_binds_tighter(unsigned x)
    ensures(x == 0u && x == x <-> x == 0u);

law quantified_equivalence(unsigned x)
    ensures(forall (unsigned y) { Eq<unsigned>(y, 0u) <-> Eq<unsigned>(y, 0u) });
proof reflexive_equivalence(unsigned x)
    proves(Eq<unsigned>(x, x) <-> Eq<unsigned>(identity(x), x))
{
    refl;
}
proof reuse_equivalence(unsigned x)
    proves(Eq<unsigned>(x, x) <-> Eq<unsigned>(identity(x), x))
{
    exact reflexive_equivalence(x);
}

verified unsigned keep(unsigned x)
    ensures(Eq<unsigned>(result, x) && (Eq<unsigned>(result, x) <-> Eq<unsigned>(x, result)))
{
    return x;
}

int main() {
    std::printf("%u %d\n", keep(7u), Eq<int>(2, 2) ? 1 : 0);
}
