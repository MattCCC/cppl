// SPEC: ARITH-011
// A pure function is a total definition the core unfolds wherever it is called,
// with nothing owed at the call, so one whose body adds two signed values is
// not admitted: the sum would run unchecked. A law stated with it has no
// proposition.
pure int twice(int x) {
    return x + x;
}

law twice_is_even(int x)
    proves (twice(x) % 2 == 0);

int main() {
    return 0;
}
