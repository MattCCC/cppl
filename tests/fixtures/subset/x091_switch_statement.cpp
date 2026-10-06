// SPEC: CONSTRUCT-091
// RFC 0022, the V1 verified subset: a verified body may use this construct, switch statement,
// and it is modeled. Its refused twin is negative/subset/x091_switch_statement.cpp.

verified unsigned probe(unsigned x)
    ensures ((x == 0u && result == 10u) || (x == 1u && result == 11u) || (x > 1u && result == 2u))
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

int main() {
    return probe(0u) == 10u && probe(1u) == 11u && probe(7u) == 2u && probe(9u) == 2u ? 0 : 1;
}
