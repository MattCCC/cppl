// The mandatory negative case: an implementation that does not satisfy the Law
// it is claimed to satisfy.

pure int identity(int x) {
    return x + 1;
}

law identity_returns_input(int x)
    ensures(identity(x) == x);

int main() {
    return 0;
}
