// SPEC: CONSTRUCT-062
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// constructor call, is refused.

struct Meter {
    unsigned reading;

    explicit Meter(unsigned value) : reading(value) {}
};

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    Meter meter(x);
    (void)meter;
    return x;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
