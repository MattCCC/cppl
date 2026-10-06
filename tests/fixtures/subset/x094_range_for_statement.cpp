// SPEC: CONSTRUCT-094
// RFC 0022, the V1 verified subset: a verified body may use this construct, range-for statement,
// and it is modeled. Its refused twin is negative/subset/x094_range_for_statement.cpp.

verified unsigned probe(const unsigned (&values)[3])
    ensures (result <= 9u)
{
    unsigned best = 0u;
    for (unsigned value : values)
        invariant (best <= 9u)
    {
        if (value <= 9u && value > best) {
            best = value;
        }
    }
    return best;
}

int main() {
    const unsigned values[3] = {1u, 7u, 3u};
    return probe(values) == 7u ? 0 : 1;
}
