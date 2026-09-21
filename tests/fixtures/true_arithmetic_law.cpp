// Unsigned addition is modular in C++, so it is exactly the core's wrapping
// primitive and the Law is provable by computation alone.

pure unsigned int add_one(unsigned int x) {
    return x + 1u;
}

law add_one_adds_one(unsigned int x)
    proves (add_one(x) == x + 1u);

int main() {
    return 0;
}
