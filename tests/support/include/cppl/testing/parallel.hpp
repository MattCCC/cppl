#pragma once

#include <cstddef>
#include <functional>

namespace cppl::testing {

// Calls body(index) for every index in [0, count), on several threads at once:
// as many as CPPL_TEST_JOBS names, by default one per processor, the way
// tests/support/parallel.sh runs a script's cases. CPPL_TEST_JOBS=1 calls them
// in order on this thread.
//
// For an exhaustive loop whose iterations share nothing: each reads only its
// own inputs, and writes nothing another reads. Every index is still visited;
// only when each is visited changes.
//
// A failure reports as the loop in order would have. When an iteration throws,
// every iteration before the first that throws still runs, and that first
// one's exception is rethrown here, with its own message. Iterations after it
// may already have run; what they would have reported is dropped, since the
// loop in order would not have reached them.
void for_each_index(std::size_t count, const std::function<void(std::size_t)>& body);

} // namespace cppl::testing
