// The middle unit of the cross-unit provenance matrix: it reaches the
// producer's dependencies only through the producer's records, and passes them
// on through its own.
#include "middle.hpp"

#include "producer.hpp"

verified unsigned middle_trusted(unsigned x)
    expects (x < 10u)
    ensures (result != 7u)
{
    return imported_trusted(x);
}

verified unsigned middle_all(unsigned x, int raw)
    expects (x < 10u)
    ensures (result == result)
{
    const unsigned counted = imported_trusted(x) + imported_unsafe(x) + imported_model() + imported_plain(x);
    const int checked = imported_validation(raw);
    return checked > 0 ? counted + 1u : counted;
}

verified unsigned middle_plain(unsigned x)
    ensures (result == x)
{
    return imported_plain(x);
}
