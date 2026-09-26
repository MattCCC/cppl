// SPEC: CLASS-010, REFINEOBL-007
// A write to a refined member owes the member's predicate where the value
// enters it. Nothing bounds `to` here. The accepted half states `expects
// (to < 10u)`: `Gauge::set` in `fixtures/verified_methods.cpp`.
type Small = unsigned where (self < 10u);

struct Gauge {
    Small level;

    verified void set(unsigned to)
        ensures (level == to)
    {
        level = to;
    }
};

int main() {
    return 0;
}
