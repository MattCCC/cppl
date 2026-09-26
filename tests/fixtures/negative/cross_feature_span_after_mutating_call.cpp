// SPEC: STDMODEL-015, CLASS-011
// `view_after_const_call` of fixtures/cross_feature/client.cpp with the
// vector handed to a member function by mutable reference instead: `emit` may
// reallocate it, so the span formed before may view freed storage, and its use
// is refused naming the call.
#include "buffers.hpp"

#include <cstddef>
#include <span>
#include <vector>

verified unsigned view_after_mutating_call()
    ensures (true)
{
    std::vector<unsigned> v{5u, 6u};
    const std::span<const unsigned> view(v);
    const Cursor cursor{0ul};
    const std::size_t length = cursor.emit(v, 7u);
    if (length == 3ul) {
        return view[1];
    }
    return 0u;
}
