// SPEC: CLASS-011
// A call on the object gives its members the versions the callee's contract
// describes, and nothing else survives it: what `value` held on entry is not
// what it holds after `reset`. The accepted half, which claims what `reset`
// promises, is `Counter::after_reset` in `fixtures/verified_methods.cpp`.
struct Counter {
    unsigned value;

    verified void reset()
        ensures (value == 0u)
    {
        value = 0u;
    }

    verified unsigned after_reset()
        expects (value == 3u)
        ensures (result == 3u)
    {
        reset();
        return value;
    }
};

int main() {
    Counter counter{3u};
    return static_cast<int>(counter.after_reset());
}
