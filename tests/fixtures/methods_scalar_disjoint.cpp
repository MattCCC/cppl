// SPEC: CLASS-010
//
// Two scalar members never share storage: objects that are not bit-fields
// overlap only when one is nested in the other or one has no size (C++
// [intro.object]), and `[[no_unique_address]]` lets an empty member share an
// address, never a scalar's storage. So a write to `second` keeps what is known
// of `first`, beside an empty member and a bit-field, which has no place of its
// own. The forms whose storage may overlap are refused in
// `negative/methods_overlapping_members.cpp`.
#include <cstdio>

struct Empty {};

struct Packed {
    [[no_unique_address]] Empty tag;
    [[no_unique_address]] unsigned first;
    unsigned second;
    unsigned low : 4;

    verified unsigned keep()
        expects (first == 1u)
        ensures (result == 1u && second == 5u)
    {
        second = 5u;
        return first;
    }
};

int main() {
    Packed packed{};
    packed.first = 1u;
    const unsigned kept = packed.keep();
    std::printf("%u %u\n", kept, packed.second);
    return 0;
}
