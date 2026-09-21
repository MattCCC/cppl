// The implementation is fully modeled, and the Law it claims is false.
// The kernel must refuse the evidence offered for it.

pure unsigned int add_one(unsigned int x) {
    return x + 1u;
}

law add_one_changes_nothing(unsigned int x)
    proves (add_one(x) == x);

int main() {
    return 0;
}
