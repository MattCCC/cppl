// SPEC: CONSTRUCT-131
// RFC 0022: the refused twin of subset/x131_extern_linkage_declaration.cpp (extern linkage declaration), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

extern "C" {
verified unsigned cppl_probe(unsigned x)
    ensures (result == x + 1u)
{
    return x;
}
}

int main() { return cppl_probe(2u) == 2u ? 0 : 1; }
