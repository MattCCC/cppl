// A Law has no ordinary runtime callable identity (SPEC.md LAW-008, ERASE-005,
// ERASE-006). Each Law below is spelled so that, were it a function ordinary
// C++ could see, ordinary C++ would choose it: as the better overload, as a
// declaration hiding the only function a namespace sees, and as what makes a
// detection idiom succeed. The program never contains it, so the verifier must
// not see it either: every template is instantiated at the arguments the
// program instantiates it at, and only proofs and Laws name a Law.
//
// Erased, this unit must compile to exactly what `law_lookup.reference.cpp`
// compiles to.
#include <cstdio>
#include <type_traits>
#include <utility>

struct Seven {
    char bytes[7];
};

Seven pick(long) {
    return Seven{};
}

// `pick(0)` matches this Law exactly, and the function only by conversion.
law pick(int x)
    proves (x == x);

// A proof names the Law through C++ lookup, where the Law is better matched
// than the function.
proof pick_holds(int x)
    proves (pick(x))
{
    refl;
}

law shared(unsigned x)
    proves (x + 0u == x);

template <unsigned N>
verified unsigned resolved(unsigned y)
    ensures (result == 7u)
{
    return N;
}

// An explicit instantiation names its specialization in ordinary C++ too.
template unsigned resolved<sizeof(pick(0))>(unsigned);

Seven area(long) {
    return Seven{};
}

namespace geo {

// Declared here, a function named `area` would hide `::area` from this
// namespace. The Law hides nothing.
law area(int x)
    proves (x == x);

proof area_holds(int x)
    proves (area(x))
{
    refl;
}

// A proof in a namespace names a Law of an enclosing one.
proof shared_holds(unsigned x)
    proves (shared(x))
{
    refl;
}

template <unsigned N>
verified unsigned hidden(unsigned y)
    ensures (result == 7u)
{
    return N;
}

unsigned measure_area() {
    return hidden<sizeof(area(0L))>(0u);
}

} // namespace geo

// Were `covers` a function, the specialization below would be chosen for any
// type it accepts.
law covers(unsigned x)
    proves (x == x);

template <class T, class = void> struct coverable : std::integral_constant<unsigned, 2u> {};

template <class T>
struct coverable<T, decltype(void(covers(std::declval<T>())))> : std::integral_constant<unsigned, 1u> {};

template <unsigned N>
verified unsigned detected(unsigned y)
    ensures (result == 2u)
{
    return N;
}

unsigned detect() {
    return detected<coverable<unsigned>::value>(0u);
}

// A Law in an unnamed namespace is the unit's own, and a proof beside it names
// it (SPEC.md LAW-007).
namespace {

law local_identity(unsigned x)
    proves (x + 0u == x);

proof local_identity_holds(unsigned x)
    proves (local_identity(x))
{
    refl;
}

} // namespace

int main() {
    std::printf("%u %u %u\n", resolved<sizeof(pick(0))>(0u), geo::measure_area(), detect());
}
