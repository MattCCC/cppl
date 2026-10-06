// Ordinary C++: `erasure.cpp` with its C++L constructs erased, as SPEC.md
// Annex M says they erase, line for line: each line a construct took is a
// comment here, so every line below stands where it stood. Every directive the
// constructs held stays on its line, and still applies to what follows it.
//
//
//
//
//
//
//
#include <cstdio>

#define PACK_FROM_HERE _Pragma("pack(push, 1)")

template <unsigned N>
unsigned packed(unsigned y)
// erased: a postcondition
{
    return N;
}

// erased: a Law
// erased: its premise
#pragma pack(push, 1)
// erased: its conclusion

struct InLaw {
    char c;
    int i;
};
#pragma pack(pop)

// erased: a Law
PACK_FROM_HERE
// erased: its conclusion

struct InMacro {
    char c;
    int i;
};
#pragma pack(pop)

// erased: a proof
// erased: its proposition
// erased: its body
#pragma pack(push, 1)
// erased: a comment
//
//
//
//
//
//
//
//
//
//
// erased: a statement
// erased: the end of its body

struct InProof {
    char c;
    int i;
};
#pragma pack(pop)

unsigned after_proof() {
    return __builtin_LINE();
}

// erased: a Law
// erased: a comment
//
//
//
//
//
//
//
//
//
//
// erased: its conclusion

unsigned after_law() {
    return __builtin_LINE();
}

using Small = unsigned;
#pragma pack(push, 1)
// erased: its predicate

struct InRefinement {
    char c;
    int i;
};
#pragma pack(pop)

unsigned count(unsigned n)
// erased: a precondition
// erased: a postcondition
{
    unsigned i = 0u;
    while (i < n)
    // erased: an invariant
#pragma pack(push, 1)
    {
        i = i + 1u;
    }
    // erased: a ghost declaration
#pragma pack(push, 2)
    // erased: a ghost declaration
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
unsigned packed_by_two(unsigned y)
// erased: a postcondition
{
    return N;
}

int main() {
    std::printf("%u %u %u %u %u %u %u %u %u %u\n", packed<sizeof(InLaw)>(0u), packed<sizeof(InMacro)>(0u),
                packed<sizeof(InProof)>(0u), packed<sizeof(InRefinement)>(0u), packed<sizeof(InLoop)>(0u),
                packed_by_two<sizeof(InGhost)>(0u), count(3u), after_proof(), after_law(),
                static_cast<unsigned>(sizeof(InLaw)));
}
