// SPEC: RUNTIMECHECK-004, UNSAFE-003, UNSAFE-005
// An unsafe block may have written the checked local, so what the check
// established does not survive it. Twin of `positive_or_one`.
type Positive = int where (self > 0);

unsafe void clobber(int* target);

verified int stale_after_unsafe(int raw)
    ensures (result > 0)
{
    int value = raw;
    if (value <= 0) {
        return 1;
    }
    unsafe {
        clobber(&value);
    }
    Positive p = value;
    return p;
}

unsafe void clobber(int* target) {
    *target = -1;
}

int main() {
    return stale_after_unsafe(5);
}
