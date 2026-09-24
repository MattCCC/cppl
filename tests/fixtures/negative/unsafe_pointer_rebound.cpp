// SPEC: UNSAFE-005, VERIFIED-043
// A pointer parameter an unsafe block may rebind is not followed after it, so
// the capability the contract states of it could be one for another object.
verified void writes_after(unsigned* p)
    expects (writable(p))
{
    unsafe {
        p = nullptr;
    }
    *p = 7u;
}

int main() {
    unsigned cell = 0u;
    writes_after(&cell);
    return 0;
}
