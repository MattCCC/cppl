// SPEC: CONSTRUCT-055
// RFC 0022: the refused twin of subset/x055_built_in_subscript.cpp (built-in subscript), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

template <unsigned N>
verified unsigned probe(const unsigned (&values)[N], unsigned index)
    ensures (result == result)
{
    return values[index];
}

int main() {
    const unsigned values[4] = {1u, 2u, 3u, 4u};
    return probe(values, 1u) == 2u ? 0 : 1;
}
