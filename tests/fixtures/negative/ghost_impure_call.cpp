// SPEC: GHOST-001
// A ghost initializer may call only a function the formal core defines. An
// ordinary function could do I/O, and would not be run.
#include <cstdio>

unsigned read_sensor() {
    std::puts("read");
    return 3u;
}

verified unsigned observes(unsigned x)
    ensures (result == x)
{
    ghost unsigned g = read_sensor();
    return x;
}

int main() {
    return static_cast<int>(observes(2u));
}
