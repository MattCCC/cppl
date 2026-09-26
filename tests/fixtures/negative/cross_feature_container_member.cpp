// SPEC: CLASS-008, CLASS-015, STDMODEL-019
// A member function whose object holds a container. The receiver model makes
// each modeled scalar member a place; a `std::vector` member is not one, and
// the function is refused naming it rather than verified over a model of the
// wrong object. The accepted shape passes the container as a parameter
// (`fixtures/cross_feature/buffers.hpp`, `Cursor::length_of`).
#include <cstddef>
#include <vector>

struct Buffer {
    std::vector<unsigned> items;

    verified std::size_t count() const
        ensures (result == items.size())
    {
        return items.size();
    }
};

int main() {
    const Buffer buffer{{1u, 2u}};
    return static_cast<int>(buffer.count()) - 2;
}
