// SPEC: CONSTRUCT-119
// RFC 0022: the refused twin of subset/x119_enum_declaration.cpp (enum declaration), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

enum class Mode { Off, On };

verified unsigned probe(Mode mode)
    ensures (result <= 1u)
{
    if (mode == Mode::On) {
        return 2u;
    }
    return 0u;
}

int main() { return probe(Mode::On) == 1u ? 0 : 1; }
