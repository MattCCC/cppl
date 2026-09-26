// SPEC: ARITH-003
// A bit-field holds only the values of its width, and C++ promotes it by that
// width, not by its declared type: `unsigned low : 31` promotes to `int`, so
// `low + 1` is a signed addition, and `unsigned all : 32` to `unsigned int`, so
// `all + 1u` wraps. Bit-field values are not modeled, so each read is refused
// rather than read at its declared type.
struct Bits {
    unsigned low : 31;
    unsigned all : 32;
};

verified int low_successor(Bits bits)
    ensures (result == result)
{
    return bits.low + 1;
}

verified unsigned all_successor(Bits bits)
    ensures (result == result)
{
    return bits.all + 1u;
}

int main() {
    return 0;
}
