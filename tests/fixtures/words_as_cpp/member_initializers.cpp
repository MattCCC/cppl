// Ordinary C++ constructors initializing members named after contract clauses
// (SPEC.md 3.1, GRAMMAR.md 38).
//
// The mem-initializers after `:` belong to the constructor's body. Whatever
// specifier leads the constructor, `expects(a)` there initializes the member
// `expects` and states no precondition.
#include <cstdio>

struct Range {
    int expects;
    int ensures;
    int decreases;
    constexpr Range() : expects(1), ensures(2), decreases(3) {}
    explicit Range(int a) : expects(a), ensures(a + 1), decreases(a + 2) {}
    inline Range(int a, int b) : ensures(a), expects(b), decreases(0) {}
    explicit constexpr Range(int a, int b, int c) noexcept : expects(a), ensures(b), decreases(c) {}
};

struct Derived : Range {
    int invariant;
    explicit Derived(int v) : Range(v), invariant(v * 10) {}
};

struct Single {
    int ensures;
    constexpr Single(int v) noexcept : ensures(v) {}
};

constexpr Single single(5);

int main() {
    const Range first;
    const Range second(5);
    const Range third(6, 7);
    const Range fourth(8, 9, 10);
    const Derived derived(4);
    std::printf("%d %d %d\n", first.expects, first.ensures, first.decreases);
    std::printf("%d %d %d\n", second.expects, second.ensures, second.decreases);
    std::printf("%d %d %d\n", third.expects, third.ensures, third.decreases);
    std::printf("%d %d %d\n", fourth.expects, fourth.ensures, fourth.decreases);
    std::printf("%d %d %d\n", derived.expects, derived.invariant, single.ensures);
    return 0;
}
