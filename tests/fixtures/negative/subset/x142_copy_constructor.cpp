// SPEC: CONSTRUCT-142
// RFC 0022: the refused twin of subset/x142_copy_constructor.cpp (copy constructor), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

struct Meter {
    unsigned reading;

    Meter(unsigned value) : reading(value) {}
    Meter(const Meter& other) : reading(other.reading) {}
};

verified unsigned probe(Meter meter)
    ensures (result == meter.reading)
{
    return meter.reading + 1u;
}

int main() {
    const Meter meter(2u);
    return probe(meter) == 2u ? 0 : 1;
}
