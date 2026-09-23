// SPEC: ERASE-003, WORD-008
// An `unsafe` boundary is specified and not implemented. The word is then an
// ordinary name and the block is not C++, so the unit is refused rather than
// having its marker erased around operations nothing checked.
verified unsigned bumped(unsigned x)
    expects (x < 10u)
    ensures (result == x + 1u)
{
    unsigned y = x;
    unsafe {
        y = y + 1u;
    }
    return y;
}

int main() {
    return static_cast<int>(bumped(0u));
}
