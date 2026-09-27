// SPEC: CONSTRUCT-119
// RFC 0022, the V1 verified subset: a verified body may use this construct, enum declaration,
// and it is modeled. Its refused twin is negative/subset/x119_enum_declaration.cpp.

enum class Mode { Off, On };

verified unsigned probe(Mode mode)
    ensures (result <= 1u)
{
    if (mode == Mode::On) {
        return 1u;
    }
    return 0u;
}

int main() {
    return probe(Mode::On) == 1u ? 0 : 1;
}
