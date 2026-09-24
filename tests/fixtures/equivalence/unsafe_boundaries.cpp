// Unsafe boundaries on runtime code (SPEC.md 26, UNSAFE-001, ERASE-003,
// Annex M).
//
// Erased, this unit must compile to exactly the code
// `unsafe_boundaries.reference.cpp` compiles to. `unsafe` leaves a function
// declaration and a block alike; the declaration, the block's braces and every
// statement inside the block stay, and run as the ordinary C++ they are
// (SPEC.md 26.4).
#include <cstdio>

unsigned device_reads = 0u;

unsafe unsigned read_device();

inline unsafe unsigned read_register(const unsigned* registers, unsigned index) {
    return registers[index];
}

unsafe unsigned read_device() {
    ++device_reads;
    return 42u;
}

// The block's effects are unknown to the proof, so the contract does not rest
// on what it computed: the branch after it establishes the bound.
verified unsigned clamped_reading()
    ensures (result <= 100u)
{
    unsigned x = 0u;
    unsafe {
        x = read_device();
    }
    if (x > 100u) {
        return 100u;
    }
    return x;
}

// A loop counter the block never names keeps its invariant across the block.
verified unsigned count_readings(unsigned n)
    ensures (result == n)
{
    unsigned i = 0u;
    unsigned last = 0u;
    while (i < n)
        invariant (i <= n)
    {
        unsafe {
            last = read_device();
            unsafe {
                last = last + 1u;
            }
        }
        ++i;
    }
    return i;
}

int main() {
    const unsigned registers[3] = {7u, 8u, 9u};
    unsigned raw = 0u;
    unsafe {
        raw = read_register(registers, 2u);
    }
    std::printf("%u %u %u %u\n", clamped_reading(), count_readings(3u), raw, device_reads);
}
