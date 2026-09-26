// SPEC: ARITH-006, REFINE-008
// A refinement bounds its values to [-1000, 1000], and 3000000 * 1000 does not
// fit `int`. The twin multiplying by 3 is tripled.
type Small = int where (self >= -1000 && self <= 1000);

verified int scaled_small(Small x)
    ensures (result == 3000000 * x)
{
    return 3000000 * x;
}

int main() {
    return scaled_small(0);
}
