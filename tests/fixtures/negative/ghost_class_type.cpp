// SPEC: GHOST-001
// A ghost of class type would construct and destroy an object whose
// destructor prints; erased, it would print nothing.
#include <cstdio>

struct Noisy {
    unsigned value = 0u;
    ~Noisy() {
        std::puts("destroyed");
    }
};

verified unsigned constructs(unsigned x)
    ensures (result == x)
{
    ghost Noisy n{};
    return x;
}

int main() {
    return static_cast<int>(constructs(2u));
}
