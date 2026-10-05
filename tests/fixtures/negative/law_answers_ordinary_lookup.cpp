// SPEC: LAW-008, ERASE-005, ERASE-006
// A Law has no ordinary runtime callable identity, so ordinary C++ never finds
// one: not as the better overload, not as a declaration hiding a function, and
// not as what satisfies a detection idiom. Each template below claims the
// value it would be instantiated at if a Law answered ordinary lookup, and is
// refused, because the program instantiates it at another: the verifier checks
// the specialization the program runs.
#include <cstdio>
#include <type_traits>
#include <utility>

struct Seven {
    char bytes[7];
};

Seven pick(long) {
    return Seven{};
}

law pick(int x)
    proves (x == x);

template <unsigned N>
verified unsigned resolved(unsigned y)
    ensures (result == 1u)
{
    return N;
}

Seven area(long) {
    return Seven{};
}

namespace geo {

law area(int x)
    proves (x == x);

template <unsigned N>
verified unsigned hidden(unsigned y)
    ensures (result == 1u)
{
    return N;
}

unsigned measure_area() {
    return hidden<sizeof(area(0L))>(0u);
}

} // namespace geo

law covers(unsigned x)
    proves (x == x);

template <class T, class = void> struct coverable : std::integral_constant<unsigned, 2u> {};

template <class T>
struct coverable<T, decltype(void(covers(std::declval<T>())))> : std::integral_constant<unsigned, 1u> {};

template <unsigned N>
verified unsigned detected(unsigned y)
    ensures (result == 1u)
{
    return N;
}

int main() {
    std::printf("%u %u %u\n", resolved<sizeof(pick(0))>(0u), geo::measure_area(),
                detected<coverable<unsigned>::value>(0u));
}
