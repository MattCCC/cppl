// Ordinary C++: `proofs.cpp` erased by hand as SPEC.md Annex M says it erases.
// Every law and proof declaration is gone, and `pure` leaves its function
// untouched.
#include <cstdio>

enum class State : int { idle = -1, running = 3 };

struct Point {
    int x;
    int y;
};

unsigned zero() {
    return 0u;
}

unsigned identity(unsigned x) {
    return x;
}

unsigned add(unsigned a, unsigned b) {
    return a + b;
}

int main() {
    const State state = State::running;
    const Point point{3, 4};
    std::printf("%u %u %u %d %d %d\n", zero(), identity(7u), add(identity(2u), 5u), static_cast<int>(state), point.x,
                point.y);
}
