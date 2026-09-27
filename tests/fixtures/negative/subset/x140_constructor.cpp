// SPEC: CONSTRUCT-140
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// constructor, is refused.

struct Meter {
    unsigned reading;

    verified explicit Meter(unsigned value)
        ensures (reading == value)
        : reading(value)
    {
    }
};

int main() {
    const Meter meter(2u);
    return meter.reading == 2u ? 0 : 1;
}
