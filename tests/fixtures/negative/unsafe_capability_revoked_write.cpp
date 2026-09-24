// SPEC: UNSAFE-003, VERIFIED-043
// TRUST.md TCB-UNSAFE-003: no capability the contract states survives an unsafe
// block, since what the block did to the storage was not checked. Writing
// through `q` after it has nothing to rest on.
verified void writes_after(unsigned* q)
    expects (writable(q))
{
    unsafe {
        q[0] = 1u;
    }
    *q = 2u;
}

int main() {
    unsigned cell = 0u;
    writes_after(&cell);
    return static_cast<int>(cell);
}
