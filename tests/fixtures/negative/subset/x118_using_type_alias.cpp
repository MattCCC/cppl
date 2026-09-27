// SPEC: CONSTRUCT-118
// RFC 0022: the refused twin of subset/x118_using_type_alias.cpp (using type alias), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

using Count = unsigned;

verified Count probe(Count x)
    ensures (result == x)
{
    return x + 1u;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
