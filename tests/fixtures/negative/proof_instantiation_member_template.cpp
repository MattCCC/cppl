// SPEC: ERASE-019, LAW-008
// Holder is no template, but its member is, and only the Law instantiates it:
// at <0>, which completes Set<0> before E::A is initialized.
#include "../include/stateful_friend.hpp"

#include <cstdio>

struct Holder {
    unsigned value;

    template <int N>
    constexpr unsigned plus() const
    {
        return value + static_cast<unsigned>(sizeof(Set<N>)) * 0u;
    }
};

law held(Holder h)
    proves (h.plus<0>() == h.value);

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
