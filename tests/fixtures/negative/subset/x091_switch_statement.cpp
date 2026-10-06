// SPEC: CONSTRUCT-091
// RFC 0022: the refused twin of subset/x091_switch_statement.cpp (switch statement), the same program
// with one thing changed, so the construct is shown modeled rather than passed over: the claim for 1
// leaves out the fall-through into the next case.

verified unsigned probe(unsigned x)
    ensures ((x == 0u && result == 10u) || (x == 1u && result == 1u) || (x > 1u && result == 2u))
{
    unsigned y = 0u;
    switch (x) {
    case 0u:
        y = 10u;
        break;
    case 1u:
        y = 1u;
        [[fallthrough]];
    case 7u:
        y = y + 10u;
        break;
    default:
        return 2u;
    }
    if (x == 7u) {
        return 2u;
    }
    return y;
}

int main() { return probe(0u) == 10u && probe(1u) == 11u && probe(7u) == 2u && probe(9u) == 2u ? 0 : 1; }
