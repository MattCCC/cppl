// SPEC: UNSAFE-003, UNSAFE-005
// What an unsafe block computes is not known after it. The block does add one,
// but nothing checked that, so the postcondition that says so is not proven.
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
