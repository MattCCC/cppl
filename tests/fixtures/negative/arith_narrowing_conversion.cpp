// SPEC: ARITH-008
// 32768 does not fit `short`. C++17 leaves the result implementation-defined
// and C++20 reduces it; C++L requires it to fit in every mode. The twin
// bounded by `SHRT_MAX` is narrowed.
verified short narrowed_one_past(int x)
    expects (x >= -32768 && x <= 32768)
    ensures (result == x)
{
    return x;
}

int main() {
    return narrowed_one_past(0);
}
