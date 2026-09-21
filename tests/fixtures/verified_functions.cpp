#include "include/verified_contract.hpp"

pure unsigned input(unsigned x) {
    return x;
}

verified unsigned identity(unsigned x)
    ensures(result == x)
{
    return x;
}

verified unsigned inc(unsigned x)
    ensures(result == x + 1u)
{
    return x + 1u;
}

verified unsigned zero_if_zero(unsigned x)
    expects(x == 0u)
    ensures(result == 0u)
{
    return x;
}

verified unsigned one_if_zero(unsigned x)
    expects(x == 0u)
    ensures(result == 1u)
{
    return x + 1u;
}

verified unsigned first(unsigned x, unsigned y)
    ensures(result == x)
{
    return x;
}

verified unsigned second(unsigned x, unsigned y)
    ensures(result == y)
{
    return y;
}

verified unsigned equal_inputs(unsigned x, unsigned y)
    expects(x == y)
    ensures(result == y + 1u)
{
    return x + 1u;
}

verified unsigned from_pure(unsigned x)
    ensures(result == x)
{
    return input(x);
}

verified unsigned constant(void)
    ensures(result == 7u)
{
    return 7u;
}

namespace nested {
verified pure int identity(int x)
    ensures(result == x)
{
    return x;
}
verified unsigned identity(unsigned x)
    ensures(result == x)
{
    return x;
}
} // namespace nested

law signed_identity(int x)
    ensures(nested::identity(x) == x);
law constant_equality(unsigned x)
    expects(0u == x)
    ensures(0u == 0u + 0u);

int main() {
    unsigned result = identity(41u);
    return result != 41u || inc(41u) != 42u || inc(~0u) != 0u || zero_if_zero(0u) != 0u || one_if_zero(0u) != 1u ||
           first(2u, 3u) != 2u || second(2u, 3u) != 3u || equal_inputs(4u, 4u) != 5u || from_pure(6u) != 6u ||
           constant() != 7u || nested::identity(8) != 8 || nested::identity(9u) != 9u || zero_if_zero(5u) != 5u ||
           imported::from_header(10u) != 10u || imported::next(11u) != 11u;
}
