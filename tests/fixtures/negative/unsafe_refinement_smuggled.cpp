// SPEC: UNSAFE-003
// TRUST.md TCB-UNSAFE-003: a value written in an unsafe block does not keep its
// declared refinement, since nothing charged the predicate at that write. `t`
// is declared `Small`, and the block stores 50 in it.
type Small = unsigned where (self < 10u);

verified unsigned smuggled(Small s)
    ensures (result < 10u)
{
    Small t = s;
    unsafe {
        t = 50u;
    }
    return t;
}

int main() {
    return static_cast<int>(smuggled(3u));
}
