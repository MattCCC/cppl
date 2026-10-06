// Ordinary C++: `default_arguments.cpp` erased by hand as SPEC.md Annex M says it
// erases. The refinement is the alias of its base type, every contract and
// measure is gone, and every default argument stays where it was written.
#include <cstdio>

using Small = int;

unsigned add(unsigned x, unsigned k = 2u) {
    return x + k;
}

int twice(int x) {
    return x + x;
}

int take(int p = 50) {
    return p;
}

int narrow(Small s = 7) {
    return s;
}

int doubled(int p = twice(4)) {
    return p;
}

int sum(Small a, Small b = 3) {
    return a + b;
}

int need(int p = 70) {
    return p;
}

unsigned nested(unsigned p = add(5u)) {
    return p;
}

unsigned bumped(unsigned x) {
    return x + 2u;
}

unsigned walk(unsigned k = 0u) {
    if (k == 0u) {
        return 0u;
    }
    return walk();
}

struct Counter {
    int base;
    int plus(int k = 4) const {
        return base + k;
    }
};

template <class T> T below(T v, T hi = T(10)) {
    return v;
}

template <unsigned A, unsigned B> unsigned first() {
    return A;
}
unsigned picked(unsigned p = (first<4u, 5u>()), unsigned q = 3u) {
    return p + q;
}

int caller() {
    int a = take();
    int b = narrow();
    int c = doubled();
    int d = sum(10);
    int e = need();
    if (a != 50 || b != 7 || c != 8 || d != 13 || e != 70) {
        return 0;
    }
    return a + b + c + d + e;
}

unsigned unsigned_caller() {
    unsigned a = nested();
    unsigned b = bumped(3u);
    unsigned c = walk();
    unsigned d = below<unsigned>(3u);
    unsigned e = picked();
    if (a != 7u || b != 5u || c != 0u || d != 3u || e != 7u) {
        return 0u;
    }
    return 22u;
}

int member_caller(int base) {
    Counter counter{base};
    return counter.plus();
}

int main() {
    std::printf("take() == %d\n", take());
    std::printf("narrow() == %d\n", narrow());
    std::printf("doubled() == %d\n", doubled());
    std::printf("sum(10) == %d\n", sum(10));
    std::printf("need() == %d\n", need());
    std::printf("nested() == %u\n", nested());
    std::printf("bumped(3) == %u\n", bumped(3u));
    std::printf("walk() == %u\n", walk());
    std::printf("below(3) == %u\n", below<unsigned>(3u));
    std::printf("picked() == %u\n", picked());
    std::printf("caller() == %d\n", caller());
    std::printf("unsigned_caller() == %u\n", unsigned_caller());
    std::printf("member_caller(6) == %d\n", member_caller(6));
    return 0;
}
