// SPEC: CONSTRUCT-006
// RFC 0022: the refused twin of subset/x006_nullptr_literal.cpp (nullptr literal), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified unsigned probe(const unsigned* p)
    ensures (result == 0u)
{
    if (p == nullptr) {
        return 0u;
    }
    return 1u;
}

int main() { return probe(nullptr) == 0u ? 0 : 1; }
