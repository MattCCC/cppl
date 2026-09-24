// Ordinary C++: `termination.cpp` erased by hand as SPEC.md Annex M says it
// erases. Every clause, measures included, is gone, and so is `verified`; the
// loops and the recursion are untouched.
#include <cstdio>

unsigned odd_steps(unsigned n);

unsigned even_steps(unsigned n) {
    if (n == 0u) {
        return 0u;
    }
    return odd_steps(n - 1u);
}

unsigned odd_steps(unsigned n) {
    if (n == 0u) {
        return 0u;
    }
    return even_steps(n - 1u);
}

unsigned ackermann(unsigned m, unsigned n) {
    if (m == 0u) {
        return n + 1u;
    }
    if (n == 0u) {
        return ackermann(m - 1u, 1u);
    }
    return ackermann(m - 1u, ackermann(m, n - 1u));
}

unsigned drain(unsigned n) {
    unsigned i = n;
    do {
        i = i - 1u;
    } while (i > 0u);
    return i;
}

unsigned search(unsigned n) {
    unsigned i = 0u;
    for (;;) {
        if (i == n) {
            break;
        }
        ++i;
    }
    return i;
}

unsigned grid(unsigned rows, unsigned cols) {
    unsigned r = rows;
    unsigned c = cols;
    while (r > 0u) {
        if (c > 0u) {
            c = c - 1u;
        } else {
            r = r - 1u;
            c = cols;
        }
    }
    return r;
}

int main() {
    std::printf("%u %u %u %u %u\n", even_steps(5u), ackermann(2u, 3u), drain(3u), search(6u), grid(2u, 2u));
}
