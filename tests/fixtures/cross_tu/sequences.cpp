#include "sequences.hpp"

verified std::size_t grown_copy(std::vector<unsigned> v)
    ensures (result == v.size() + 1ul)
{
    v.push_back(1u);
    return v.size();
}

verified std::size_t three_listed()
    ensures (result == 3ul)
{
    std::vector<unsigned> v{1u, 2u, 3u};
    return v.size();
}
