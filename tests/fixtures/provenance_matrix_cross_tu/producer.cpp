// The producing unit of the cross-unit provenance matrix: one contract resting
// on each kind of dependency, and one resting on none
// (`e2e/provenance_matrix.sh`, TRUST.md 36.3, TCB-XTU-010).
#include "producer.hpp"

#include <vector>

type Positive = int where (self > 0);

pure unsigned zero() {
    return 0u;
}

trusted law broken_counter()
    proves (zero() == 1u);

proof counter_is_one()
    proves (zero() == 1u)
{
    exact broken_counter;
}

unsigned pokes = 0u;

void poke() {
    ++pokes;
}

verified unsigned imported_trusted(unsigned x)
    expects (x < 10u)
    ensures (result != 7u)
{
    if (x == 7u) {
        contradiction counter_is_one;
    }
    return x;
}

verified unsigned imported_unsafe(unsigned x)
    ensures (result == x)
{
    unsafe {
        poke();
    }
    return x;
}

verified unsigned imported_model()
    ensures (result == 2u)
{
    std::vector<unsigned> v{1u, 2u};
    return static_cast<unsigned>(v.size());
}

verified int imported_validation(int raw)
    ensures (result > 0)
{
    if (validate<Positive>(raw)) {
        Positive p = raw;
        return p;
    }
    return 1;
}

verified unsigned imported_plain(unsigned x)
    ensures (result == x)
{
    return x;
}
