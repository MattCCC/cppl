// SPEC: CLASS-010, CONTRACT-010
// A reference parameter may designate a member of the implicit object: called
// as `counter.through_alias(counter.value)`, the write through `other` is a
// write to `value`, and the precondition's fact about `value` does not survive
// it. The accepted half reads `value` before the write:
// `Counter::read_before_alias` in `fixtures/verified_methods.cpp`.
struct Counter {
    unsigned value;

    verified unsigned through_alias(unsigned& other)
        expects (value == 3u)
        ensures (result == 3u)
    {
        other = 0u;
        return value;
    }
};

int main() {
    Counter counter{3u};
    return static_cast<int>(counter.through_alias(counter.value));
}
