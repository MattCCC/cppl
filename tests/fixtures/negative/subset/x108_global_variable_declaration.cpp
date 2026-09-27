// SPEC: CONSTRUCT-108
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// global variable declaration, is refused.

unsigned configured = 3u;

verified unsigned probe()
    ensures (result == result)
{
    return configured;
}

int main() { return probe() == 3u ? 0 : 1; }
