// SPEC: VERIFIED-030, VERIFIED-031
//
// An object a parameter designates by reference is caller storage, and another
// reference parameter may designate one of its members: called as
// `stale(x.a, x)`, the write through `r` writes `other.a`. A read of `other.a`
// after the write must not be the value `other.a` arrived with, or this claim,
// false at run time, would be proven. The accepted half reads before the write:
// `read_first` in `fixtures/untracked_members.cpp`.
struct Pair {
    unsigned a;
    unsigned b;
};

verified unsigned stale(unsigned& r, const Pair& other)
    expects (other.a == 1u)
    ensures (result == 1u)
{
    r = 5u;
    return other.a;
}

int main() {
    Pair x{1u, 2u};
    return static_cast<int>(stale(x.a, x));
}
