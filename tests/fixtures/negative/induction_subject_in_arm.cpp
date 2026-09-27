// SPEC: INDUCT-001
// Inside its arms the subject is out of scope: the zero arm proves the claim at
// 0, and the successor arm at the predecessor plus one. A hypothesis stated
// about `x` would state the claim itself, so naming `x` there is refused rather
// than read as the value it stands for in either case.
pure unsigned add(unsigned x, unsigned y) {
    return x + y;
}

law add_zero(unsigned x)
    proves (add(x, 0u) == x);

proof add_zero_by_induction(unsigned x)
    proves (add_zero(x))
{
    induction x {
        zero => {
            refl;
        }

        successor(pred) => {
            assume hypothesis : add(x, 0u) == x;
            exact hypothesis;
        }
    }
}

int main() {
    return static_cast<int>(add(0u, 0u));
}
