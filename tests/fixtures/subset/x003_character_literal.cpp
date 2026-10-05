// SPEC: CONSTRUCT-003
// RFC 0022, the V1 verified subset: a verified body may use this construct, character literal,
// and it is modeled. Its refused twin is negative/subset/x003_character_literal.cpp.

verified int probe()
    ensures (result == 97)
{
    return 'a';
}

int main() {
    return probe() == 97 ? 0 : 1;
}
