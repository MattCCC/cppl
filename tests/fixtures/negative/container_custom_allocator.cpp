// SPEC: STDMODEL-010
// The summaries are stated for std::allocator. A vector allocating otherwise is
// not modeled.
#include <cstddef>
#include <memory>
#include <vector>

template <typename T>
struct Arena : std::allocator<T> {
    template <typename U>
    struct rebind {
        using other = Arena<U>;
    };
};

verified std::size_t custom()
    ensures (result == 1ul)
{
    std::vector<unsigned, Arena<unsigned>> v{1u};
    return v.size();
}

int main() {
    return 0;
}
