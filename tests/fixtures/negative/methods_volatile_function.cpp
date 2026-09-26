// SPEC: CLASS-009, CLASS-015
//
// A volatile-qualified member function observes its object as volatile, whose
// value may change by means no statement shows. That is not modeled, so the
// function is refused by name even where its body reads nothing.
struct Port {
    unsigned value;

    verified unsigned zero() volatile
        ensures (result == 0u)
    {
        return 0u;
    }

    verified unsigned read() const volatile
        ensures (result == result)
    {
        return 0u;
    }
};

int main() {
    return 0;
}
