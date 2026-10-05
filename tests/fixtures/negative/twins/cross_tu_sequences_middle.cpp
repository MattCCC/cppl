// Refused twin of fixtures/cross_tu/sequences_middle.cpp (tests/negative/refused_twins.sh): the same
// program, except that listed_in_the_middle returns one more than three_listed.
#include "sequences_middle.hpp"

#include "sequences.hpp"

verified std::size_t listed_in_the_middle()
    ensures (result == 3ul)
{
    return three_listed() + 1ul;
}
