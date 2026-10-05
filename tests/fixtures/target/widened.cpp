// True where `unsigned long` is wider than 32 bits, and false where it is 32
// bits wide, as on i686: there 4294967295 is its largest value and the sum
// wraps to 0. Proven for the one target and compiled for the other, it would
// be a false claim about the program that runs (TRUST.md TCB-CLANG-006).
verified unsigned long widened(unsigned long x)
    expects (x == 4294967295ul)
    ensures (result > 4294967295ul)
{
    return x + 1ul;
}
