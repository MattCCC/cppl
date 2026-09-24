// SPEC: UNSAFE-005
// TRUST.md TCB-UNSAFE-002: an unsafe block may keep the address of what it
// names and write through it later, from a block that never names it. The first
// block hands `x` to code that keeps its address; the second writes 99 through
// it. The value assigned between them is not known after the second.
unsafe void keep(unsigned* p) {
    static unsigned* kept = nullptr;
    if (p != nullptr) {
        kept = p;
    } else if (kept != nullptr) {
        *kept = 99u;
    }
}

verified unsigned stale(unsigned a)
    expects (a < 10u)
    ensures (result < 10u)
{
    unsigned x = a;
    unsafe {
        keep(&x);
    }
    x = 3u;
    unsafe {
        keep(nullptr);
    }
    return x;
}

int main() {
    return static_cast<int>(stale(1u));
}
