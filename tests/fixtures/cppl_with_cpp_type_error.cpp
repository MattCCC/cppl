// A file with valid C++L (a law with a valid proposition) and a wholly
// unrelated, genuine ordinary-C++ type error elsewhere in the same file.
//
// This exists so tooling that runs C++L source through Clang (the driver's
// analysis projection, and cppl-lsp's buffer-compile pipeline) is checked
// against the case where a real ordinary-C++ diagnostic must still surface
// even though the file also contains valid, unrelated C++L syntax: the two
// concerns must not mask each other (tools/cppl-lsp/README.md, "Diagnostics").

pure int identity(int x) {
    return x;
}

law identity_returns_input(int x)
    ensures(identity(x) == x);

int main() {
    int x = "not an int"; // genuine C++ type error, unrelated to the law above
    return x;
}
