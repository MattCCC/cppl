// SPEC: ARITH-008
// A cast is the conversion it names and owes what that conversion owes: 128
// does not fit `signed char`. The twin bounded by 127 is cast_down.
verified signed char cast_down_one_past(int x)
    expects (x >= -128 && x <= 128)
    ensures (result == x)
{
    return static_cast<signed char>(x);
}

int main() {
    return cast_down_one_past(0);
}
