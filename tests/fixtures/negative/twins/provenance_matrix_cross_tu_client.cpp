// Refused twin of `provenance_matrix_cross_tu/client.cpp`: `through_trusted`
// returns through `imported_plain` in place of `imported_trusted`, and nothing
// it imports then excludes 7 (`negative/refused_twins.sh`).
#include "middle.hpp"
#include "producer.hpp"

#include <cstdio>

verified unsigned through_trusted(unsigned x)
    expects (x < 10u)
    ensures (result != 7u)
{
    return imported_plain(x);
}

verified unsigned through_unsafe(unsigned x)
    ensures (result == x)
{
    return imported_unsafe(x);
}

verified unsigned through_model()
    ensures (result == 2u)
{
    return imported_model();
}

verified int through_validation(int raw)
    ensures (result > 0)
{
    return imported_validation(raw);
}

verified unsigned through_plain(unsigned x)
    ensures (result == x)
{
    return imported_plain(x);
}

verified unsigned through_middle_trusted(unsigned x)
    expects (x < 10u)
    ensures (result != 7u)
{
    return middle_trusted(x);
}

verified unsigned through_middle_all(unsigned x, int raw)
    expects (x < 10u)
    ensures (result == result)
{
    return middle_all(x, raw);
}

verified unsigned through_middle_plain(unsigned x)
    ensures (result == x)
{
    return middle_plain(x);
}

verified unsigned client_diamond(unsigned x)
    expects (x < 10u)
    ensures (result == result)
{
    return imported_trusted(x) + middle_trusted(x);
}

verified unsigned client_local(unsigned x)
    ensures (result == x)
{
    return x;
}

int main() {
    std::printf("%u %u %u %d %u %u %u %u %u %u\n", through_trusted(3u), through_unsafe(4u), through_model(),
                through_validation(-2), through_plain(5u), through_middle_trusted(6u), through_middle_all(1u, 8),
                through_middle_plain(7u), client_diamond(2u), client_local(9u));
    return 0;
}
