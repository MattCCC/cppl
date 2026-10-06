// Ordinary C++ using entities an imported module declares under C++L words
// (SPEC.md MODULE-001, WORD-019).
//
// The module's declarations are not in this unit's text, so nothing here shows
// that the words name anything. They do, and every statement keeps its C++
// meaning: `contradiction verdict;` declares a local, `cases c{3};` another,
// `unsafe{};` constructs a temporary and `validate<int>(5)` calls a function.
import words;
#include <cstdio>

int main() {
    contradiction verdict;
    cases c{3};
    decompose d{4};
    ghost g;
    unsafe{};
    const int doubled = validate<int>(5);
    (void)verdict;
    std::printf("%d %d %d %d\n", c.v, d.v, g.v, doubled);
    return 0;
}
