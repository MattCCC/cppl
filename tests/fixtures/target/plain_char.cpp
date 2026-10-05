// True where plain `char` is signed, as it is by default on x86, and false
// where it is unsigned and holds values up to 255, as `-funsigned-char` makes
// it. Whatever selects the signedness, the proof must be made under the one
// the program is compiled with (TRUST.md TCB-CLANG-006).
verified int widened_char(char c)
    ensures (result < 128)
{
    return c;
}
