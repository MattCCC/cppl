#include <cstdio>

type Positive = int where (self > 0);
type Small = unsigned where (self < 10u);

verified void set(int& x)
    ensures (x == 1)
{
    x = 1;
}
verified int observe(const Positive& x)
    ensures (result > 0)
{
    return x;
}
verified void refined_set(Positive& x, int y)
    expects (y > 0)
    ensures (x > 0)
{
    x = y;
}
verified void early(int& x, bool choose)
    ensures (x > 0)
{
    if (choose) {
        x = 1;
        return;
    }
    x = 2;
}
verified int mutation()
    ensures (result == 1)
{
    int x = 0;
    set(x);
    return x;
}
verified Positive restore() {
    Positive x = 2;
    set(x);
    return x;
}
verified void reference_loop(Small& x)
    expects (x == 0u)
    ensures (x == 9u)
{
    while (x < 9u)
        invariant (x <= 9u)
    {
        ++x;
    }
}
verified int parameter_alias(int x)
    ensures (result == 1)
{
    int& alias = x;
    alias = 1;
    return x;
}
verified int copy_before_write(int& x)
    ensures (result == 2)
{
    x = 2;
    int snapshot = x;
    x = 1;
    return snapshot;
}
verified int rvalue_reference(int&& x)
    ensures (x == 1 && result == 1)
{
    x = 1;
    return x;
}
verified int choose(bool b)
    ensures (result > 0)
{
    return b ? 1 : 2;
}
using Nothing = void;
verified Nothing noop()
    ensures (true)
{
    return;
}
verified void call_noop()
    ensures (true)
{
    noop();
}
verified void by_value_does_not_mutate(int x)
    expects (x == 2)
    ensures (x == 2)
{
    x = 1;
}
verified int shared(const int& observed, int& changed)
    ensures (changed == 1)
{
    changed = 1;
    return changed;
}
verified int same_argument()
    ensures (result == 1)
{
    int x = 0;
    int result = shared(x, x);
    return x;
}
verified int call_in_loop()
    ensures (result == 1)
{
    int x = 0;
    while (x < 1)
        invariant (x <= 1)
    {
        set(x);
    }
    return x;
}

int main() {
    int x = 0;
    early(x, false);
    if (x != 2 || copy_before_write(x) != 2 || x != 1)
        return 1;
    if (rvalue_reference(0) != 1 || same_argument() != 1 || call_in_loop() != 1)
        return 2;
    call_noop();
    unsigned small = 0u;
    reference_loop(small);
    if (small != 9u || parameter_alias(8) != 1)
        return 3;
    std::printf("%d %d %d\n", mutation(), restore(), choose(false));
}
