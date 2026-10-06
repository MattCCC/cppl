// SPEC: ERASE-019, CASE-007
// Crate is a template of data alone, which a proof may instantiate: what it
// holds is fixed by its arguments. Its argument here is Set<0>. Decomposing the
// crate completes Crate<Set<0>>, and with it its member of type Set<0>, before
// E::A is initialized, and which() would be verified to return 1 and run to
// print 0.
#include "../include/stateful_friend.hpp"

#include <cstdio>

template <class T>
struct Crate {
    T item;
    bool sealed;
};

proof carried(const Crate<Set<0>>& c)
    proves (Eq<bool>(true, true))
{
    decompose c {
        components(item, sealed) => {
            refl;
        }
    }
}

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
