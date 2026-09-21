// A Law over a function whose purity was never claimed or checked. Its value is
// not a mathematical function of its arguments as far as C++L knows, so the Law
// must not be proven.

int identity(int x) {
    return x;
}

law identity_returns_input(int x)
    proves (identity(x) == x);

int main() {
    return 0;
}
