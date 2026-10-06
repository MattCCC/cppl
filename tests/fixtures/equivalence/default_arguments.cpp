// Default arguments of verified functions (SPEC.md R.16).
//
// A call that relies on a default argument evaluates it where the call stands,
// as if the caller had written it there. The callee's contract is instantiated
// at its value, its refined parameter and its precondition are owed for it at
// the call, a call it makes owes what any call owes, and a recursive call
// relying on one descends by its value.
//
// None of that runs. Every default stays in the program as written, so erased,
// this unit must compile to exactly the code `default_arguments.reference.cpp`
// compiles to, where every contract was removed by hand and every default kept.
#include <cstdio>

type Small = int where (self >= 0 && self < 100);

pure unsigned add(unsigned x, unsigned k = 2u) {
    return x + k;
}

verified pure int twice(int x)
    expects (x >= 0 && x < 1000)
    ensures (result == x + x)
{
    return x + x;
}

// A contract mentioning the parameter is instantiated at the default's value.
verified int take(int p = 50)
    ensures (result == p)
{
    return p;
}

// A refined parameter owes its predicate for the default at the call.
verified int narrow(Small s = 7)
    ensures (result == s)
{
    return s;
}

// A default that calls a pure function owes that function's precondition.
verified int doubled(int p = twice(4))
    ensures (result == p)
{
    return p;
}

// A default after a written argument.
verified int sum(Small a, Small b = 3)
    ensures (result == a + b)
{
    return a + b;
}

// A precondition the default satisfies.
verified int need(int p = 70)
    expects (p > 60)
    ensures (result == p)
{
    return p;
}

// A default whose own call relies on a default.
verified unsigned nested(unsigned p = add(5u))
    ensures (result == p)
{
    return p;
}

// A contract clause calling a pure function relying on its default.
verified unsigned bumped(unsigned x)
    ensures (result == add(x))
{
    return x + 2u;
}

// A recursive call relying on a default descends by the default's value.
verified unsigned walk(unsigned k = 0u)
    ensures (result == 0u)
    decreases (k)
{
    if (k == 0u) {
        return 0u;
    }
    return walk();
}

// A member function's default.
struct Counter {
    int base;
    verified int plus(int k = 4) const
        expects (base >= 0 && base < 100 && k >= 0 && k < 100)
        ensures (result == base + k)
    {
        return base + k;
    }
};

// A template's default, instantiated at the specialization called.
template <class T>
verified T below(T v, T hi = T(10))
    expects (v < hi)
    ensures (result == v)
{
    return v;
}

// A comma of a template-id within a parenthesized default.
template <unsigned A, unsigned B>
verified pure unsigned first()
    ensures (result == A)
{
    return A;
}
verified unsigned picked(unsigned p = (first<4u, 5u>()), unsigned q = 3u)
    ensures (result == p + q)
{
    return p + q;
}

verified int caller()
    ensures (result == 50 + 7 + 8 + 13 + 70)
{
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

verified unsigned unsigned_caller()
    ensures (result == 7u + 5u + 0u + 3u + 7u)
{
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

verified int member_caller(int base)
    expects (base >= 0 && base < 50)
    ensures (result == base + 4)
{
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
