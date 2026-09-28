// Refused twin of fixtures/cross_tu/sequences.cpp (tests/negative/refused_twins.sh): the same
// program, except that grown_copy appends two elements.
#include "sequences.hpp"

verified std::size_t grown_copy(std::vector<unsigned> v)
    ensures (result == v.size() + 1ul)
{
    v.push_back(1u);
    v.push_back(2u);
    return v.size();
}

verified std::size_t three_listed()
    ensures (result == 3ul)
{
    std::vector<unsigned> v{1u, 2u, 3u};
    return v.size();
}

verified std::size_t three_counted()
    ensures (result == 3ul)
{
    return 3ul;
}
