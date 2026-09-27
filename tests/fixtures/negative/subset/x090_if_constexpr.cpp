// SPEC: CONSTRUCT-090
// RFC 0022: the refused twin of subset/x090_if_constexpr.cpp (if constexpr), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    if constexpr (2u < 1u) {
        return x;
    } else {
        return 0u;
    }
}

int main() { return probe(2u) == 2u ? 0 : 1; }
