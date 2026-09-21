verified unsigned inc(unsigned x)
    ensures(result == x + 1u)
{
    return x + 1u;
}
verified unsigned twice(unsigned x)
    ensures(result == (x + 1u) + 1u)
{
    return inc(inc(x));
}

verified unsigned zero(unsigned x)
    expects(x == 0u)
    ensures(result == 0u)
{
    return x;
}
verified unsigned nested_zero(unsigned x)
    expects(x == 0u)
    ensures(result == 0u)
{
    return zero(zero(x));
}
verified unsigned bump_zero(unsigned x)
    expects(x == 0u)
    ensures(result == 1u)
{
    return x + 1u;
}
verified unsigned bump_one(unsigned x)
    expects(x == 1u)
    ensures(result == 2u)
{
    return x + 1u;
}
verified unsigned two_from_zero(unsigned x)
    expects(x == 0u)
    ensures(result == 2u)
{
    return bump_one(bump_zero(x));
}
verified unsigned pair(unsigned a, unsigned b)
    expects(a == b)
    ensures(result == a)
{
    return a;
}
verified unsigned swapped(unsigned x, unsigned y)
    expects(x == y)
    ensures(result == y)
{
    return pair(y, x);
}

unsigned late(unsigned x);
verified unsigned caller_before(unsigned x)
    expects(x == 0u)
    ensures(result == 0u)
{
    return late(x);
}
verified unsigned late(unsigned x)
    expects(x == 0u)
    ensures(result == 0u)
{
    return x;
}

verified unsigned sum_of_both(unsigned x, unsigned y)
    expects(x == 1u)
    expects(y == 2u)
    ensures(result == 3u)
{
    return x + y;
}
verified unsigned both_from_one(unsigned x)
    expects(x == 1u)
    ensures(result == 3u)
{
    return sum_of_both(x, x + 1u);
}

verified unsigned result_ignored(unsigned x)
    expects(x == 0u)
    ensures(x == x)
{
    return zero(x);
}
verified unsigned within_arithmetic(unsigned x)
    ensures(result == (x + 1u) + 1u)
{
    return inc(x) + 1u;
}

namespace overloaded {
verified unsigned identity(unsigned x)
    ensures(result == x)
{
    return x;
}
verified int identity(int x)
    ensures(result == x)
{
    return x;
}
} // namespace overloaded
verified unsigned use_overload(unsigned x)
    ensures(result == x)
{
    return overloaded::identity(x);
}

int main() {
    return twice(40u) != 42u || twice(~0u) != 1u || nested_zero(0u) != 0u || two_from_zero(0u) != 2u ||
           swapped(3u, 3u) != 3u || caller_before(0u) != 0u || result_ignored(0u) != 0u ||
           within_arithmetic(40u) != 42u || use_overload(5u) != 5u || overloaded::identity(6) != 6 || zero(7u) != 7u ||
           both_from_one(1u) != 3u;
}
