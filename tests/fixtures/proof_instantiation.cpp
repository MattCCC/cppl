// SPEC: ERASE-019, ERASE-005, CASE-007
// The accepted half of the matched pairs `negative/proof_instantiation.sh`
// refuses. Proof-only text of every kind here -- a Law, a proof, a contract, a
// case split, a refinement predicate, a ghost declaration, a loop invariant --
// uses standard templates, templates of data alone and the program's own
// classes, none of which can change what ordinary C++ means where it is
// instantiated. E::A is therefore 0 in the program verified as in the program
// run, which instantiates Set<0> only after it.
#include "include/stateful_friend.hpp"

#include <cstdio>
#include <optional>

struct Point {
    unsigned x;
    unsigned y;
};

template <class T> struct Crate {
    T item;
    bool sealed;
};

template <class T> struct Machine {
    enum class Mode : unsigned { off = 0u, on = 1u };
};

// A Law and a proof over a template of data, at a class of the program's own.
law same_crate(unsigned x)
    proves (x + static_cast<unsigned>(Machine<int>::Mode::on) == x + 1u);

proof crate_components(const Crate<Point>& c)
    proves (Eq<bool>(true, true))
{
    decompose c {
        components(item, sealed) => {
            refl;
        }
    }
}

// A contract and a case split over a standard template at the program's own
// class: the payload binder is a Point.
verified unsigned first(const std::optional<Point>& o)
    ensures (result == 0u)
{
    cases o {
        some(p) => {
        }

        none => {
        }
    }
    return 0u;
}

// A refinement predicate, a ghost declaration and a loop invariant naming
// member enumerations of templates of data.
type Small = unsigned where (self < static_cast<unsigned>(Machine<int>::Mode::on) + 9u);

verified unsigned count(unsigned n)
    expects (n < 10u)
    ensures (result == n)
{
    ghost unsigned start = static_cast<unsigned>(Machine<long>::Mode::off);
    unsigned i = 0u;
    while (i < n)
        invariant (i <= n && n < static_cast<unsigned>(Machine<short>::Mode::on) + 9u)
        decreases (n - i)
    {
        i = i + 1u;
    }
    return i;
}

enum class E : unsigned { A = defined<0>() ? 1u : 0u };

verified unsigned which()
    ensures (result == 0u)
{
    return static_cast<unsigned>(E::A);
}

int main() {
    const Set<0> set{0};
    std::printf("%u %u %u %u\n", which(), count(3u), first(std::nullopt), static_cast<unsigned>(set.unused));
    return 0;
}
