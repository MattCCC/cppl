// SPEC: ERASE-019, LAW-008
// Crate holds data alone, but deducing its arguments from `Crate{x, true}`
// deduces every guide written for it: the first, never chosen, instantiates
// Inject<unsigned>, which defines adl(Tag<0>), before its deduction fails. In
// the program verified that happens before E::A is initialized. Constructing a
// class in a Law is not modeled, so the Law would be refused further on in any
// case; the instantiation is refused first, where the deduction happens.
#include "../include/stateful_friend.hpp"

#include <cstdio>

template <class T>
struct Inject {
    friend constexpr int adl(Tag<0>) { return 1; }
};

template <class T>
struct Crate {
    T item;
    bool sealed;
};

template <class T>
Crate(T, typename Inject<T>::missing) -> Crate<T>;

template <class T>
Crate(T, bool) -> Crate<T>;

law crated(unsigned x)
    proves (Crate{x, true}.item == x);

enum class E : unsigned { A = defined<0>() ? 1u : 0u };

verified unsigned which()
    ensures (result == 1u)
{
    return static_cast<unsigned>(E::A);
}

int main()
{
    std::printf("%u\n", which());
    return 0;
}
