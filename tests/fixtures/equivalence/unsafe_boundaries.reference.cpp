// Ordinary C++: `unsafe_boundaries.cpp` erased by hand as SPEC.md Annex M says
// it erases. `unsafe`, `verified` and every clause are gone; each unsafe block
// is the compound statement it delimited, and every function is untouched.
#include <cstdio>

unsigned device_reads = 0u;

unsigned read_device();

inline unsigned read_register(const unsigned* registers, unsigned index) {
    return registers[index];
}

unsigned read_device() {
    ++device_reads;
    return 42u;
}

unsigned clamped_reading() {
    unsigned x = 0u;
    {
        x = read_device();
    }
    if (x > 100u) {
        return 100u;
    }
    return x;
}

unsigned count_readings(unsigned n) {
    unsigned i = 0u;
    unsigned last = 0u;
    while (i < n) {
        {
            last = read_device();
            {
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
    {
        raw = read_register(registers, 2u);
    }
    std::printf("%u %u %u %u\n", clamped_reading(), count_readings(3u), raw, device_reads);
}
