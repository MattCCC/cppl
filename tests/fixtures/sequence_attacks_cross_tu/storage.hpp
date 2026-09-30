// Contracts over a caller's vector, proven in `storage.cpp` and used by
// `client.cpp` through a verification interface: the storage attacks of
// `tests/negative/sequence_attacks.sh`, made across translation units (RFC 0017,
// RFC 0020). `tests/e2e/sequence_attacks.sh` drives them.
#pragma once

#include <cstddef>
#include <vector>

// May reallocate the caller's vector: the importer learns nothing about its
// storage from the contract, only that the call may replace it.
verified void append_one(std::vector<unsigned>& v)
    ensures (true);

verified std::size_t last_index(const std::vector<unsigned>& v)
    expects (0ul < v.size())
    ensures (result < v.size());

// One past the last index: a bound, not an index.
verified std::size_t end_index(const std::vector<unsigned>& v)
    ensures (result <= v.size());
