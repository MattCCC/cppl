// SPEC: UNSAFE-003
// A `break` of the enclosing loop leaves the block for a place the verified
// path never reaches from it. A `break` of a loop inside the block stays in it.
verified unsigned breaks_out(unsigned n)
    ensures (result <= n)
{
    unsigned i = 0u;
    while (i < n)
        invariant (i <= n)
    {
        unsafe {
            for (unsigned j = 0u; j < 3u; ++j) {
                break;
            }
            break;
        }
        ++i;
    }
    return i;
}

int main() {
    return static_cast<int>(breaks_out(2u));
}
