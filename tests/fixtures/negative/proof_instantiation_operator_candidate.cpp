// SPEC: ERASE-019, LAW-008
// The Law compares an enumeration value with an integer, which C++ does with
// its built-in operator after promoting it. Overload resolution considers the
// operator template as well, and deducing it instantiates Inject<Color>, which
// defines adl(Tag<0>), before the deduction fails: in the program verified,
// before E::A is initialized. The enumeration is modeled, and the Law would be
// read as the comparison it states; the instantiation is refused before that,
// at the operator that makes it.
#include "../include/stateful_friend.hpp"

#include <cstdio>
#include <type_traits>

enum Color : unsigned { red = 0u, green = 1u };

template <class T>
struct Inject {
    friend constexpr int adl(Tag<std::is_enum_v<T> ? 0 : 1>) { return 1; }
};

template <class T>
constexpr bool operator==(T, typename Inject<T>::missing)
{
    return true;
}

law red_is_zero(Color c)
    proves ((c == 0u) == (c == 0u));

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
