#include "storage.hpp"

verified void append_one(std::vector<unsigned>& v)
    ensures (true)
{
    v.push_back(1u);
}

verified std::size_t last_index(const std::vector<unsigned>& v)
    expects (0ul < v.size())
    ensures (result < v.size())
{
    return v.size() - 1ul;
}

verified std::size_t end_index(const std::vector<unsigned>& v)
    ensures (result <= v.size())
{
    return v.size();
}
