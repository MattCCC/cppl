// The twin of plain_char.cpp: true where plain `char` is unsigned, as it is by
// default on AArch64 Linux, and false where it is signed and holds negative
// values, as `-fsigned-char` makes it (TRUST.md TCB-CLANG-006).
verified int widened_char(char c)
    ensures (result >= 0)
{
    return c;
}
