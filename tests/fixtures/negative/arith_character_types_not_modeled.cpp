// SPEC: ARITH-003, ARITH-008
// `char32_t` promotes to `unsigned int`, since `int` cannot hold all its
// values, so its sum is modular; `char16_t` and `wchar_t` promote to `int`, so
// theirs owes representability. Which one applies depends on the target, and
// these types are not modeled, so each is refused where it is named rather than
// given a promotion derived here.
verified unsigned char32_sum(char32_t a, char32_t b)
    ensures (result == a + b)
{
    return a + b;
}

verified int char16_sum(char16_t a, char16_t b)
    ensures (result == a + b)
{
    return a + b;
}

verified int wide_sum(wchar_t a, wchar_t b)
    ensures (result == a + b)
{
    return a + b;
}

int main() {
    return 0;
}
