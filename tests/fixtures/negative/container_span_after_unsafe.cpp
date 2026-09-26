// SPEC: STDMODEL-016, UNSAFE-003
// An unsafe block may end the storage a span parameter views, so the
// capability does not survive it, even for an element read before the block.
#include <cstddef>
#include <span>

void release(std::span<const unsigned> in);

verified unsigned after_block(std::span<const unsigned> in)
    expects (readable(in) && 0ul < in.size())
    ensures (result == result)
{
    unsigned first = in[0];
    unsafe {
        release(in);
    }
    return first + in[0];
}

void release(std::span<const unsigned>) {}

int main() {
    return 0;
}
