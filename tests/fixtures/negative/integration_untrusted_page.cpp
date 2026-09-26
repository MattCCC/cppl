// SPEC: TRUSTED-002, ARITH-006
// `page_room` of integration/ledger.cpp without the trusted assumption: `used`
// may be 150, where the room is -51, so the postcondition is not proven.
verified long long page_room(long long used)
    expects (used >= 0ll)
    ensures (0ll <= result && result <= 99ll)
{
    return 99ll - used;
}

int main() {
    return static_cast<int>(page_room(99ll));
}
