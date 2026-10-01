// Refused twin of `provenance_matrix_cross_tu/producer.cpp`: `imported_trusted`
// leaves out the contradiction through `counter_is_one`, the step that makes
// its path with `x == 7u` impossible (`negative/refused_twins.sh`).
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
