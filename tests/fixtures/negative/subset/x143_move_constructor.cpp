// SPEC: CONSTRUCT-143
// RFC 0022: the refused twin of subset/x143_move_constructor.cpp (move constructor), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

#include <utility>

struct Meter {
    unsigned reading;

    Meter(unsigned value) : reading(value) {}
    Meter(Meter&& other) noexcept : reading(other.reading) {}
};

verified unsigned probe(Meter meter)
    ensures (result == meter.reading)
{
    return meter.reading + 1u;
}

int main() {
    Meter meter(2u);
    return probe(std::move(meter)) == 2u ? 0 : 1;
}
