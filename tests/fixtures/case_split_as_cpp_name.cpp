#include <cstdio>

// SPEC: WORD-012
// `cases` names a type here, so `cases c {3};` declares a local with a braced
// initializer. It keeps that meaning inside a verified body too.
struct cases {
    int v;
};

verified unsigned as_cpp(unsigned x)
    ensures (result == x)
{
    cases c{3};
    return x;
}

int main() {
    cases c{4};
    std::printf("%d %u\n", c.v, as_cpp(5u));
}
