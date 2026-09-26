#include "sequences_middle.hpp"

#include "sequences.hpp"

verified std::size_t listed_in_the_middle()
    ensures (result == 3ul)
{
    return three_listed();
}
