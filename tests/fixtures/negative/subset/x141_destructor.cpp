// SPEC: CONSTRUCT-141
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// destructor, is refused.

struct Meter {
    unsigned reading;

    verified ~Meter()
        ensures (true)
    {
    }
};

int main() {
    const Meter meter{2u};
    return meter.reading == 2u ? 0 : 1;
}
