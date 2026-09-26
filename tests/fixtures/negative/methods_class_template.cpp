// SPEC: TEMPLATE-001, CLASS-015
// A member of a class template would have its contract checked at every
// specialization of the class, which nothing forces here, so it is refused
// rather than reported verified for no specialization. Its declaration is the
// template's pattern, which is what Clang resolves the contract against.
template <typename T> struct Box {
    unsigned value;

    verified unsigned get() const
        ensures (result == value)
    {
        return value;
    }
};

int main() {
    const Box<int> box{1u};
    return static_cast<int>(box.get());
}
