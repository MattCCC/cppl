// Refused twin of `provenance_matrix_cross_tu/middle.cpp`: `middle_trusted`
// returns through `imported_plain`, whose record states nothing about 7, in
// place of `imported_trusted`, whose record rests on the trusted law that
// excludes it (`negative/refused_twins.sh`).
#include "middle.hpp"

#include "producer.hpp"

verified unsigned middle_trusted(unsigned x)
    expects (x < 10u)
    ensures (result != 7u)
{
    return imported_plain(x);
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
