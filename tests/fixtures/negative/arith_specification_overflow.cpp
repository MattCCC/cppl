// SPEC: ARITH-010, ADMISSIBLE-005
// A postcondition means what C++ would compute, so `result + 1 > result` holds
// only where `result + 1` is defined: it is not true of `INT_MAX`, which this
// body may return. The twin whose precondition excludes `INT_MAX` is
// int_below_max_plus_one's style of bound.
verified int successor_is_larger(int x)
    ensures (result + 1 > result)
{
    return x;
}

int main() {
    return successor_is_larger(0);
}
