// SPEC: CONSTRUCT-132
// RFC 0022: the refused twin of subset/x132_template_declaration.cpp (template declaration), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

template <typename T>
verified T probe(T x)
    ensures (result != x)
{
    return x;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
