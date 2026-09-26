// Contracts over the verified sequence subset, proven in `sequences.cpp` and
// used by `sequences_client.cpp` through a verification interface (RFC 0017,
// RFC 0020). `tests/e2e/containers.sh` drives them.
#pragma once

#include <cstddef>
#include <vector>

// A vector parameter: the declaration itself uses the std::vector model.
verified std::size_t grown_copy(std::vector<unsigned> v)
    ensures (result == v.size() + 1ul);

// No container in the declaration; the body uses one.
verified std::size_t three_listed()
    ensures (result == 3ul);
