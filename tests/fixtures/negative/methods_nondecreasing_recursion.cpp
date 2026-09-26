// SPEC: TERMINATION-007, CLASS-011
// A recursive member function is measured like any recursive function: the
// call on the object is made with the object's member where it was entered,
// so the measure does not fall. The accepted half lowers `remaining` first:
// `Tank::drain` in `fixtures/verified_methods.cpp`.
struct Tank {
    unsigned remaining;

    verified void drain()
        ensures (remaining == 0u)
        decreases (remaining)
    {
        if (remaining == 0u) {
            return;
        }
        drain();
    }
};

int main() {
    return 0;
}
