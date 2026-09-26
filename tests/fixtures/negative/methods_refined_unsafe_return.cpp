// SPEC: CLASS-010, REFINE-061, UNSAFE-005
//
// What the unsafe block leaves in `level` is not known to hold `Small`: no
// write this body can see was charged for it. Its validity is not derived, so
// the return that hands `level` back to the caller is charged it, and cannot
// show it. The accepted twin writes `level` again after the block:
// `Gauge::rescue` in `fixtures/verified_methods.cpp`.
type Small = unsigned where (self < 10u);

struct Gauge {
    Small level;

    verified void scramble()
        ensures (level == level)
    {
        unsafe {
            level = 50u;
        }
    }
};

int main() {
    return 0;
}
