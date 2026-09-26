// SPEC: CONTRACT-011, CONTRACT-012, CLASS-004, CLASS-005, CLASS-015
// A constructor begins its object's lifetime and a destructor ends it. The
// receiver model verifies member functions that run on a live object, and does
// not follow either transition, so a contract on one is refused rather than
// checked against a model of the wrong object.
struct Zero {
    unsigned value;

    verified Zero()
        ensures (value == 0u)
        : value(0u)
    {
    }

    verified ~Zero()
        ensures (true)
    {
    }
};

int main() {
    return 0;
}
