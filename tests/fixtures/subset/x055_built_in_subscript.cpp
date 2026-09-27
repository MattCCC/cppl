// SPEC: CONSTRUCT-055
// RFC 0022, the V1 verified subset: a verified body may use this construct, built-in subscript,
// and it is modeled. Its refused twin is negative/subset/x055_built_in_subscript.cpp.

template <unsigned N>
verified unsigned probe(const unsigned (&values)[N], unsigned index)
    expects (index < N)
    ensures (result == result)
{
    return values[index];
}

int main() {
    const unsigned values[4] = {1u, 2u, 3u, 4u};
    return probe(values, 1u) == 2u ? 0 : 1;
}
