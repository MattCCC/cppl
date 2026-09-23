// A verified body may call only functions the formal core can state: pure
// definitions and verified contracts (SPEC.md VERIFIED-018). The call here
// stands after a subscript, whose index bound is an obligation of its own, and
// it is refused for what it is wherever it stands.
unsigned twice(unsigned x) {
    return x + x;
}

verified unsigned read_then_call(const unsigned (&values)[4], unsigned i)
    expects (i < 4u)
    ensures (result == result)
{
    unsigned v = values[i];
    return twice(v);
}

int main() {
    const unsigned values[4] = {1u, 2u, 3u, 4u};
    return static_cast<int>(read_then_call(values, 2u));
}
