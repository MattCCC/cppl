#include "closure.hpp"

#include <vector>

pure unsigned closure_zero() {
    return 0u;
}

// False, and trusted: whatever rests on it is PROVEN only relative to it, in
// this unit and in every unit that uses a contract proven through it.
trusted law counter_broken()
    proves (closure_zero() == 1u);

proof counter_one()
    proves (closure_zero() == 1u)
{
    exact counter_broken;
}

unsafe unsigned read_register();

unsigned plain(unsigned x) {
    return x + 1u;
}

verified unsigned trusting(unsigned x)
    expects (x < 10u)
    ensures (result != 7u)
{
    if (x == 7u) {
        contradiction counter_one;
    }
    return x;
}

unsigned modeled() {
    const std::vector<unsigned> values{4u, 5u};
    return static_cast<unsigned>(values.size());
}

verified unsigned unsafe_read()
    ensures (result <= 100u)
{
    unsigned value = 0u;
    unsafe {
        value = read_register();
    }
    if (value > 100u) {
        return 100u;
    }
    return value;
}

unsafe unsigned read_register() {
    return 42u;
}
