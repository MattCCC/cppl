// Ordinary C++ constructing temporaries and declaring locals of types named
// `unsafe` and `ghost`, outside any verified function (SPEC.md 3.1, WORD-018).
//
// `unsafe{};` constructs a temporary and `ghost value;` declares a local. No
// unsafe boundary or ghost state could have been meant here, so the compiler
// says nothing about either.
#include <cstdio>

struct unsafe {
    unsafe() {
        std::puts("unsafe constructed");
    }
    ~unsafe() {
        std::puts("unsafe destroyed");
    }
};

struct ghost {
    int v = 3;
};

int main() {
    unsafe{};
    ghost value;
    ghost copy = value;
    std::printf("%d %d\n", value.v, copy.v);
    return 0;
}
