// The twin of widened.cpp: true where `unsigned long` is 32 bits wide, as on
// i686, where the sum wraps to 0, and false where it is wider. It verifies
// only when the analysis is made for the 32-bit target the program is compiled
// for (TRUST.md TCB-CLANG-006).
verified unsigned long wrapped(unsigned long x)
    expects (x == 4294967295ul)
    ensures (result == 0ul)
{
    return x + 1ul;
}
