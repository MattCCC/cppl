// SPEC: LAW-005, LAW-008
// A Law in a class would be a member that member lookup could find: the
// detection idiom below would see it in the program verified and not in the
// program run, and the specialization of `g` verified would not be the one run.
// This implementation recognizes Laws at namespace scope only, where they are
// projected out of ordinary lookup, so one in a class is refused by name.
#include <cstdio>
#include <type_traits>
#include <utility>
struct Box {
    int v;
    law positive(int x)
        proves (x == x);
};
template <class T, class = void> struct has_positive : std::integral_constant<unsigned, 2u> {};
template <class T> struct has_positive<T, decltype(void(std::declval<T>().positive(0)))> : std::integral_constant<unsigned, 1u> {};
template <unsigned N>
verified unsigned g(unsigned y)
    ensures (result == 1u)
{
    return N;
}
int main() { std::printf("%u\n", g<has_positive<Box>::value>(0u)); }
