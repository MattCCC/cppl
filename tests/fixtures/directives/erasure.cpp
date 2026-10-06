// SPEC: ERASE-005, ERASE-017 (tests/e2e/erasure_directives.sh)
// A preprocessor directive written inside a C++L construct is not C++L, and
// erasure keeps it where it stands: a `#pragma` between a Law's clauses, one a
// macro's `_Pragma` writes there, one in a proof's body, in a refinement's
// declaration, in a loop's clauses and between ghost declarations all still
// apply to what follows them, and the line markers the preprocessor writes for
// a long comment inside a Law or a proof still say which line comes next.
//
// Each packed record's size is checked by a contract too, so the program
// verified is packed exactly as the program run. `erasure.reference.cpp`
// is this unit erased by hand, line for line.
#include <cstdio>

#define PACK_FROM_HERE _Pragma("pack(push, 1)")

template <unsigned N>
verified unsigned packed(unsigned y)
    ensures (result == 5u)
{
    return N;
}

law identity(unsigned x)
    expects (x < 100u)
#pragma pack(push, 1)
    proves (x == x);

struct InLaw {
    char c;
    int i;
};
#pragma pack(pop)

law same(unsigned x)
    expects (x < 100u) PACK_FROM_HERE
    proves (x + 0u == x);

struct InMacro {
    char c;
    int i;
};
#pragma pack(pop)

proof same_holds(unsigned x)
    proves (same(x))
{
#pragma pack(push, 1)
    // A comment long enough that the preprocessor writes a line marker after
    // it rather than a blank line for each of its lines.
    //
    //
    //
    //
    //
    //
    //
    //
    //
    refl;
}

struct InProof {
    char c;
    int i;
};
#pragma pack(pop)

unsigned after_proof() {
    return __builtin_LINE();
}

law commented(unsigned x)
    // A comment long enough that the preprocessor writes a line marker after
    // it rather than a blank line for each of its lines.
    //
    //
    //
    //
    //
    //
    //
    //
    //
    proves (x == x);

unsigned after_law() {
    return __builtin_LINE();
}

type Small = unsigned where
#pragma pack(push, 1)
    (self < 10u);

struct InRefinement {
    char c;
    int i;
};
#pragma pack(pop)

verified unsigned count(unsigned n)
    expects (n < 100u)
    ensures (result == n)
{
    unsigned i = 0u;
    while (i < n)
        invariant (i <= n)
#pragma pack(push, 1)
    {
        i = i + 1u;
    }
    ghost unsigned seen = n;
#pragma pack(push, 2)
    ghost unsigned twice = n + n;
    return i;
}

// Packed to two by the directive between the ghost declarations, then to one by
// the directive in the loop's clauses.
struct InGhost {
    char c;
    int i;
};
#pragma pack(pop)

struct InLoop {
    char c;
    int i;
};
#pragma pack(pop)

template <unsigned N>
verified unsigned packed_by_two(unsigned y)
    ensures (result == 6u)
{
    return N;
}

int main() {
    std::printf("%u %u %u %u %u %u %u %u %u %u\n", packed<sizeof(InLaw)>(0u), packed<sizeof(InMacro)>(0u),
                packed<sizeof(InProof)>(0u), packed<sizeof(InRefinement)>(0u), packed<sizeof(InLoop)>(0u),
                packed_by_two<sizeof(InGhost)>(0u), count(3u), after_proof(), after_law(),
                static_cast<unsigned>(sizeof(InLaw)));
}
