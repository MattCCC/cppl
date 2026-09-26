// SPEC: TEMPLATE-001, CLASS-015
// A member function template would have its contract checked at every
// specialization of the member, which nothing forces here, so it is refused
// where it is written rather than reported verified for no specialization.
// A member of a class template is the same case one level up
// (`methods_class_template.cpp`).
struct Picker {
    template <typename T>
    verified unsigned pick(T x)
        ensures (result == 0u)
    {
        return 0u;
    }
};

int main() {
    return 0;
}
