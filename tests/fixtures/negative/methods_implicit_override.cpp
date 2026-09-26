// SPEC: CONTRACT-014, CLASS-014
// A function overriding a virtual one is virtual whether or not it says so.
// Clang resolves that, and the refusal follows what Clang resolved rather than
// the words written: this override carries neither `virtual` nor `override`.
struct Base {
    unsigned value;

    virtual unsigned get() const {
        return value;
    }

    virtual ~Base() = default;
};

struct Derived : Base {
    verified unsigned get() const
        ensures (result == 2u)
    {
        return 2u;
    }
};

int main() {
    Derived derived;
    return static_cast<int>(derived.get());
}
