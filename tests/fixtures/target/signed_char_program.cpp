// A program that prints what its verified function is proven to return: 1,
// which holds where plain `char` is signed, as it is by default on x86, and
// is false where it is unsigned, as `-funsigned-char` makes it. Wherever that
// option comes from, the proof must be made under the `char` the program is
// compiled with, and the program must print what was proven (TRUST.md
// TCB-CLANG-006).
#include <cstdio>

verified int negative_char()
    ensures (result == 1)
{
    const char c = '\xff';
    if (c < 0) {
        return 1;
    }
    return 0;
}

int main() {
    std::printf("%d\n", negative_char());
    return 0;
}
