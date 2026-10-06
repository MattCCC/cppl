// Refused twin of fixtures/positions/columns.cpp (tests/negative/refused_twins.sh): the same
// program, except that after_law claims the column the analysed program gave
// the code after a Law on its line when it resumed that code at column one.
// Erasure moves no code to another line or column, and the program verified
// has every line and column the program run has. Each C++L construct below is
// followed, on its own line, by ordinary C++ that observes where it stands, as a
// template argument: `at<P, E>` is verified only when P, the position the
// analysed program observes, is E, the position written beside it, and the
// program prints P as it runs.
#include <cstdio>

template <unsigned P, unsigned E>
verified unsigned at(unsigned y)
    ensures (result == E)
{
    return P;
}

law same(unsigned x) proves (x == x); unsigned after_law() { return at<__builtin_COLUMN(), 35u>(0u); }

proof same_holds(unsigned x) proves (same(x)) { refl; } unsigned after_proof() { return at<__builtin_COLUMN(), 92u>(0u); }

type Small = unsigned where (self < 10u); unsigned after_refinement() { return at<__builtin_COLUMN(), 83u>(0u); }

type Big = unsigned where (self >= 10u &&
    self < 1000u); unsigned after_lines() { return at<__builtin_COLUMN(), 55u>(0u); }

type Positive = int where (self > 0);

verified int pick(int raw)
    ensures (result > 0)
{
    if (validate<Positive>(raw)) { return raw; } return at<__builtin_COLUMN(), 60u>(1u) - 59; } unsigned after_body() { return at<__builtin_COLUMN(), 131u>(0u); }

verified unsigned counted(unsigned n)
    expects (n < 10u)
    ensures (result == 86u)
{
    unsigned i = 0u;
    while (i < n) invariant (i <= n) { i = i + 1u; } ghost unsigned g = i; return at<__builtin_COLUMN(), 86u>(0u);
}

verified unsigned spaced(unsigned x)
    ensures (result == x)
{   return    x;   } unsigned after_spaced() { return at<__builtin_COLUMN(), 51u>(0u); }

template unsigned at<1u, 1u>(unsigned);
unsigned after_instantiation() { return at<__builtin_LINE(), 48u>(0u); }

int main() {
    std::printf("%u %u %u %u %d %u %u %u %u %u\n", after_law(), after_proof(), after_refinement(), after_lines(),
                pick(3), after_body(), counted(3u), spaced(4u), after_spaced(), after_instantiation());
}
