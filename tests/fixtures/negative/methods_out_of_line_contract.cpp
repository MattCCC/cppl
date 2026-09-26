// SPEC: CONTRACT-005, CLASS-008
// A member function's contract is stated on its declaration in the class, and
// the definition inherits it. One written on a qualified definition outside
// the class would be resolved where the class's members are not in scope, so
// it is refused, and the author is told where it belongs. The accepted form is
// `Meter::declared_here` in `fixtures/verified_methods.cpp`.
struct Meter {
    unsigned level;

    unsigned bump(unsigned x);
};

verified unsigned Meter::bump(unsigned x)
    ensures (result == x)
{
    return x;
}

int main() {
    return 0;
}
