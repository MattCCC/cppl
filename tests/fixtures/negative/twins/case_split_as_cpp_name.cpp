// Refused twin of fixtures/case_split_as_cpp_name.cpp (tests/negative/refused_twins.sh): the same
// program, except that as_cpp claims x + 1u.
#include <cstdio>

// SPEC: WORD-012
// `cases` names a type here, so `cases c {3};` declares a local with a braced
// initializer. It keeps that meaning inside a verified body too.
struct cases {
    int v;
};

verified unsigned as_cpp(unsigned x)
    ensures (result == x + 1u)
{
    cases c{3};
    return x;
}

int main() {
    cases c{4};
    std::printf("%d %u\n", c.v, as_cpp(5u));
}
