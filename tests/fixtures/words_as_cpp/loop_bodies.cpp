// Ordinary C++ whose loop bodies declare locals of types named after loop
// clauses (SPEC.md 3.1, GRAMMAR.md 25, 26).
//
// `while (c) decreases (k) {n};` is a loop whose body declares `k` with a braced
// initializer wherever `decreases` names a type, and `invariant (y) {i}, (z)
// {i + 1};` declares two. The constructors print, so a body read as a loop
// clause, and dropped from the program, changes what it prints.
#include <cstdio>

struct invariant {
    int v;
    invariant(int x) : v(x) {
        std::printf("i%d\n", x);
    }
};
struct decreases {
    int v;
    decreases(int x) : v(x) {
        std::printf("d%d\n", x);
    }
};

decreases kept{9};

int main() {
    int n = 2;
    while (n-- > 0)
        decreases(k){n};
    for (int i = 0; i < 2; ++i)
        decreases(m){i};
    do
        decreases(p){7};
    while (false);
    for (int i = 0; i < 2; ++i)
        invariant(y){i}, (z){i + 1};
    int w = 0;
    while (w < 2)
        invariant(x){w++}, (q){0};
    int r = 3;
    do
        invariant(s){r};
    while (--r > 0);
    int t = 1;
    do
        decreases(*u){nullptr}, (&v){kept};
    while (--t > 0);
    std::printf("%d %d %d\n", n, w, r);
    return 0;
}
