// SPEC: CONSTRUCT-143
// RFC 0022, the V1 verified subset: a verified body may use this construct, move constructor,
// and it is modeled. Its refused twin is negative/subset/x143_move_constructor.cpp.

#include <utility>

struct Meter {
    unsigned reading;

    Meter(unsigned value) : reading(value) {}
    Meter(Meter&& other) noexcept : reading(other.reading) {}
};

verified unsigned probe(Meter meter)
    ensures (result == meter.reading)
{
    return meter.reading;
}

int main() {
    Meter meter(2u);
    return probe(std::move(meter)) == 2u ? 0 : 1;
}
