// SPEC: CLASS-008, CONTRACT-009
// A member function's contract is proved from its body like any other: the
// member it returns is not the member plus one.
struct Counter {
    unsigned value;

    verified unsigned get() const
        ensures (result == value + 1u)
    {
        return value;
    }
};

int main() {
    const Counter counter{1u};
    return static_cast<int>(counter.get());
}
