// The twin of signed_char_program.cpp: its verified function is proven to
// return 0, which holds where plain `char` is unsigned, as it is by default on
// AArch64 Linux or under `-funsigned-char`, and is false where it is signed.
// The program prints what was proven only if the proof was made under the
// `char` it is compiled with (TRUST.md TCB-CLANG-006).
#include <cstdio>

verified int negative_char()
    ensures (result == 0)
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
