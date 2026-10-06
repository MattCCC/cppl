// SPEC: STMT-002, CONSTRUCT-089, CONSTRUCT-090
// `if` statements with an init-statement, a condition variable, and `if
// constexpr`, each contract exact enough that a lowering which got C++'s
// semantics wrong would not prove it (C++ [stmt.if]). e2e/if_statements.sh
// verifies and runs this; negative/if_statements.sh refuses its false twins.
#include <cstdio>

// The init-statement runs first, and what it declares is visible in the
// condition and in both branches.
verified unsigned declared_init(unsigned x)
    expects (x < 100u)
    ensures ((x > 4u && result == x + 1u) || (x <= 4u && result == 2u * (x + 1u)))
{
    if (unsigned v = x + 1u; v > 5u) {
        return v;
    } else {
        return v + v;
    }
}

// An init-statement that is an expression runs before the condition reads
// what it wrote.
verified unsigned expression_init(unsigned x)
    expects (x < 100u)
    ensures ((x == 0u && result == 7u) || (x != 0u && result == x))
{
    unsigned y = 0u;
    if (y = 7u; x == 0u) {
        return y;
    }
    return x;
}

verified bool above(unsigned x)
    ensures (true)
{
    return x > 3u;
}

// The init-statement is not the condition: `above(x)`, which nothing here
// knows the value of, decides, and either branch may run.
verified unsigned init_then_call(unsigned x)
    ensures (result == 0u || result == 7u)
{
    unsigned y = 1u;
    if (y = 7u; above(x)) {
        y = 0u;
    }
    return y;
}

// Hands back the value `c` had and advances it by one.
verified unsigned bump(unsigned& c, unsigned before)
    expects (c == before && before < 100u)
    ensures (c == before + 1u && result == before)
{
    c = c + 1u;
    return before;
}

// A call with an effect in the init-statement runs once, before the condition.
verified unsigned call_init(unsigned start)
    expects (start < 10u)
    ensures ((start == 0u && result == 1u) || (start != 0u && result == start + 11u))
{
    unsigned c = start;
    if (unsigned was = bump(c, c); was == 0u) {
        return c;
    }
    return c + 10u;
}

// A condition variable is a local the condition reads.
verified unsigned condition_variable(unsigned x)
    expects (x < 100u)
    ensures ((x == 0u && result == 50u) || (x != 0u && result == x))
{
    if (unsigned copy = x) {
        return copy;
    }
    return 50u;
}

// Only the branch a constant condition selects runs; in a specialization the
// other is not instantiated, and here it would not return.
template <unsigned N>
verified unsigned pick(unsigned x)
    expects (x < 100u)
    ensures (result == x + N)
{
    if constexpr (N > 1u) {
        return x + N;
    } else if constexpr (N == 1u) {
        return x + 1u;
    } else {
        return x;
    }
}

// `if constexpr` with an init-statement.
verified unsigned constant_init(unsigned x)
    expects (x < 100u)
    ensures (result == x + 2u)
{
    if constexpr (constexpr unsigned two = 2u; two > 1u) {
        return x + two;
    } else {
        return x;
    }
}

int main() {
    std::printf("%u %u %u %u\n", declared_init(9u), declared_init(1u), expression_init(0u), expression_init(5u));
    std::printf("%u %u %u %u\n", call_init(0u), call_init(3u), condition_variable(0u), condition_variable(8u));
    std::printf("%u %u %u %u %u %u\n", pick<0u>(5u), pick<1u>(5u), pick<4u>(5u), constant_init(5u), init_then_call(2u),
                init_then_call(9u));
    return 0;
}
