// SPEC: UNSAFE-003, VERIFIED-043
// TRUST.md TCB-UNSAFE-003: a place read before an unsafe block is not readable
// after it on that account: the block may have ended the storage `p` points
// to, and no capability survives it.
void release(unsigned* p);

verified unsigned read_twice(unsigned* p)
    expects (readable(p))
    ensures (result == result)
{
    unsigned first = *p;
    unsafe {
        release(p);
    }
    unsigned second = *p;
    return first + second;
}

void release(unsigned*) {}

int main() {
    return 0;
}
