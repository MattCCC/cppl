// SPEC: ERASE-019, LAW-008
// The Law names no template. Its call's overload resolution considers the
// template `twice` as well, and deducing it instantiates Inject<unsigned>,
// which defines adl(Tag<0>), before the deduction fails and the function is
// selected. In the program verified that happens before E::A is initialized,
// and which() would be verified to return 1 and run to print 0.
#include "../include/stateful_friend.hpp"

#include <cstdio>

template <class T>
struct Inject {
    friend constexpr int adl(Tag<0>) { return 1; }
};

pure unsigned long twice(unsigned long x)
{
    return x + x;
}

template <class T>
unsigned long twice(T x, typename Inject<T>::missing = 0)
{
    return x;
}

law doubled(unsigned x)
    expects (x < 1000u)
    proves (twice(x) == twice(x));

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
