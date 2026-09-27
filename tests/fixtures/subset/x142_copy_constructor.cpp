// SPEC: CONSTRUCT-142
// RFC 0022, the V1 verified subset: a verified body may use this construct, copy constructor,
// and it is modeled. Its refused twin is negative/subset/x142_copy_constructor.cpp.

struct Meter {
    unsigned reading;

    Meter(unsigned value) : reading(value) {}
    Meter(const Meter& other) : reading(other.reading) {}
};

verified unsigned probe(Meter meter)
    ensures (result == meter.reading)
{
    return meter.reading;
}

int main() {
    const Meter meter(2u);
    return probe(meter) == 2u ? 0 : 1;
}
