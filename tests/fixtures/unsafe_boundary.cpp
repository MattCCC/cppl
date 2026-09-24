// Unsafe boundaries and what rests on them (SPEC.md 26, UNSAFE-001 to
// UNSAFE-005; TRUST.md TCB-UNSAFE-001 to TCB-UNSAFE-003, TCB-REPORT-005).
//
// An unsafe block runs as the ordinary C++ it is and establishes nothing: every
// place it could have written is unknown after it, and no capability of the
// contract survives it. A contract proven across one holds only as far as that
// block is sound, which nothing checked, so the trust report lists it with every
// block it rests on, its own and those of every verified function it calls.
// `tests/e2e/unsafe_boundary.sh` checks the whole report against what is
// written here; each refused counterpart is written out in
// `negative/unsafe_*.cpp`.
#include <cstdio>

pure unsigned zero() {
    return 0u;
}

// False, and trusted: what rests on it is PROVEN only relative to it.
trusted law broken_counter()
    proves (zero() == 1u);

proof counter_is_one()
    proves (zero() == 1u)
{
    exact broken_counter;
}

unsigned device_reads = 0u;

// --- Unsafe functions --------------------------------------------------------

// Declared twice, and listed once.
unsafe unsigned read_device();

unsafe unsigned read_device() {
    ++device_reads;
    return 42u;
}

unsafe void log_write(const unsigned* p) {
    std::printf("wrote %u\n", *p);
}

// --- Blocks in verified bodies -----------------------------------------------

// What the block read is not known, so the bound comes from the check after it
// (SPEC.md BOUNDARYEX-010).
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

// Proven through a verified call, so it rests on the block its callee holds.
verified unsigned passes_reading()
    ensures (result <= 100u)
{
    return clamped_reading();
}

// A parameter the block never names keeps what the precondition says of it.
verified unsigned keeps_untouched(unsigned a)
    expects (a < 10u)
    ensures (result < 10u)
{
    unsigned scratch = 0u;
    unsafe {
        scratch = read_device();
    }
    return a;
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
        }
        ++i;
    }
    return i;
}

// The capability is used before the block, where it still holds.
verified void store_then_log(unsigned* p)
    expects (writable(p))
{
    *p = 7u;
    unsafe {
        log_write(p);
    }
}

// Rests on a trusted law and on an unsafe block, and is listed under both.
verified unsigned never_seven_reading(unsigned x)
    expects (x < 10u)
    ensures (result != 7u)
{
    unsigned seen = 0u;
    unsafe {
        seen = read_device();
    }
    if (x == 7u) {
        contradiction counter_is_one;
    }
    return x;
}

// Proven outright: no unsafe block is on its path or on its callees'.
verified unsigned add_one(unsigned a)
    expects (a < 10u)
    ensures (result == a + 1u)
{
    return a + 1u;
}

// --- A block outside every verified body -------------------------------------

int main() {
    unsigned cell = 0u;
    store_then_log(&cell);
    unsigned raw = 0u;
    unsafe {
        raw = read_device();
    }
    const unsigned clamped = clamped_reading();
    const unsigned passed = passes_reading();
    const unsigned kept = keeps_untouched(3u);
    const unsigned counted = count_readings(2u);
    const unsigned seven = never_seven_reading(4u);
    std::printf("%u %u %u %u %u %u %u %u\n", clamped, passed, kept, counted, seven, add_one(1u), raw, device_reads);
}
