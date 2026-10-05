// SPEC: CONSTRUCT-094
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// range-for statement, is refused.

verified unsigned probe(const unsigned (&values)[3])
    ensures (result == result)
{
    unsigned total = 0u;
    for (unsigned value : values) {
        total += value;
    }
    return total;
}

int main() {
    const unsigned values[3] = {1u, 2u, 3u};
    return probe(values) == 6u ? 0 : 1;
}
