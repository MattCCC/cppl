// SPEC: CONSTRUCT-003
// RFC 0022: the refused twin of subset/x003_character_literal.cpp (character literal), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

verified int probe()
    ensures (result == 98)
{
    return 'a';
}

int main() { return probe() == 97 ? 0 : 1; }
