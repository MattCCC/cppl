// Refused twin of fixtures/switch_statements.cpp (tests/negative/refused_twins.sh): the same
// program, except that fall_through claims 1 for 1, as if 1 did not fall into 2.
// SPEC: STMT-003, CONSTRUCT-091, CONSTRUCT-052, CONSTRUCT-085, CONSTRUCT-096, CONSTRUCT-097
// `switch` statements and statement-level comma operators in verified bodies,
// each contract exact enough that a lowering which got the C++ semantics wrong
// would not prove it (C++ [stmt.switch], [expr.comma]). e2e/switch_statements.sh
// verifies and runs this; negative/switch_statements.sh refuses its false twins.
#include <cstdio>

enum class Color : unsigned char { red, green, blue };

enum Plain : unsigned { one = 1, two = 2 };

// Each way in runs to the end of the body unless a `break` leaves it: 1 falls
// into 2, and 3 is left at the end of the body.
verified unsigned fall_through(unsigned x)
    ensures ((x == 1u && result == 1u) || (x == 2u && result == 10u) || (x == 3u && result == 100u) ||
            (x != 1u && x != 2u && x != 3u && result == 0u))
{
    unsigned y = 0u;
    switch (x) {
        case 1u:
            y = y + 1u;
            [[fallthrough]];
        case 2u:
            y = y + 10u;
            break;
        case 3u:
            y = y + 100u;
    }
    return y;
}

// `default:` written first is still taken only when no case matches.
verified unsigned default_first(unsigned x)
    ensures ((x == 0u && result == 1u) || (x == 5u && result == 2u) || (x != 0u && x != 5u && result == 7u))
{
    unsigned y = 0u;
    switch (x) {
        default:
            y = 7u;
            break;
        case 0u:
            y = 1u;
            break;
        case 5u:
            y = 2u;
            break;
    }
    return y;
}

// `default:` in the middle falls into the case after it.
verified unsigned default_middle(unsigned x)
    ensures ((x == 1u && result == 1u) || (x == 9u && result == 3u) || (x != 1u && x != 9u && result == 5u))
{
    unsigned y = 0u;
    switch (x) {
        case 1u:
            y = 1u;
            break;
        default:
            y = 2u;
            [[fallthrough]];
        case 9u:
            y = y + 3u;
    }
    return y;
}

// No `default:`: a value no case matches goes on after the switch.
verified unsigned no_default(unsigned x)
    ensures ((x == 4u && result == 40u) || (x != 4u && result == x))
{
    switch (x) {
        case 4u:
            return 40u;
    }
    return x;
}

// A scoped enumeration is compared as it is, without promotion; an unscoped
// enumerator is a case value of the promoted condition's type.
verified unsigned colour(Color c)
    ensures ((c != Color::red || result == 1u) && (c != Color::green || result == 2u) &&
                                          (c != Color::blue || result == 3u) && result <= 3u)
{
    switch (c) {
        case Color::red:
            return 1u;
        case Color::green:
            return 2u;
        case Color::blue:
            return 3u;
    }
    return 0u;
}

verified unsigned plain(unsigned x)
    ensures ((x == 1u && result == 10u) || (x == 2u && result == 20u) || (x != 1u && x != 2u && result == 0u))
{
    switch (x) {
        case one:
            return 10u;
        case two:
            return 20u;
        default:
            return 0u;
    }
}

// In a loop, `break` leaves the switch and `continue` the iteration: a
// multiple of three adds nothing, one more adds one, and two more adds two.
verified unsigned loop_with_switch(unsigned n)
    expects (n <= 1000u)
    ensures (result <= 2u * n)
{
    unsigned total = 0u;
    for (unsigned i = 0u; i < n; ++i)
        invariant (i <= n && total <= 2u * i)
        decreases (n - i)
    {
        switch (i % 3u) {
            case 0u:
                continue;
            case 1u:
                total = total + 1u;
                break;
            default:
                total = total + 2u;
        }
    }
    return total;
}

// A `break` in a loop inside the switch leaves that loop, not the switch.
verified unsigned loop_in_switch(unsigned x)
    ensures ((x == 1u && result == 5u) || (x != 1u && result == 0u))
{
    unsigned y = 0u;
    switch (x) {
        case 1u: {
            unsigned k = 0u;
            while (k < 3u)
                invariant (k <= 3u)
                decreases (3u - k)
            {
                k = k + 1u;
                break;
            }
            y = 5u;
            break;
        }
        default:
            break;
    }
    return y;
}

// A switch in a case: the inner `break` leaves only the inner switch.
verified unsigned nested(unsigned x, unsigned z)
    ensures ((x == 1u && z == 2u && result == 12u) || (x == 1u && z != 2u && result == 10u) ||
            (x != 1u && result == 0u))
{
    unsigned y = 0u;
    switch (x) {
        case 1u:
            switch (z) {
                case 2u:
                    y = 2u;
                    break;
            }
            y = y + 10u;
            break;
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

// The condition, a call with an effect, is evaluated once: `c` is advanced
// once whichever case is taken.
verified unsigned once(unsigned start)
    expects (start < 10u)
    ensures ((start == 0u && result == 11u) || (start == 1u && result == 22u) ||
            (start >= 2u && result == start + 31u))
{
    unsigned c = start;
    unsigned seen = 0u;
    switch (bump(c, c)) {
        case 0u:
            seen = 10u;
            break;
        case 1u:
            seen = 20u;
            break;
        default:
            seen = 30u;
    }
    return seen + c;
}

// A condition variable is a local the condition initializes.
verified unsigned declared(unsigned x)
    expects (x < 100u)
    ensures ((x == 0u && result == 1u) || (x != 0u && result == x + 1u + 100u))
{
    switch (unsigned v = x + 1u) {
        case 1u:
            return v;
        default:
            return v + 100u;
    }
}

// Statement-level commas, and a comma in a `for` loop's increment.
verified unsigned commas(unsigned n)
    expects (n < 100u)
    ensures (result == 3u * n + 6u)
{
    unsigned a = 0u;
    unsigned b = 0u;
    unsigned c = 0u;
    a = 1u, b = 2u, c = 3u;
    unsigned s = 0u;
    for (unsigned i = 0u, j = n; i < n; ++i, --j)
        invariant (i <= n && j == n - i && s == 3u * i)
        decreases (n - i)
    {
        s = s + 1u, s = s + 2u;
    }
    return s + a + b + c;
}

// A `continue` in a switch inside a `do` loop ends the iteration there, and
// what follows the loop is outside that switch: the `break` after the loop
// leaves the outer switch.
verified unsigned continue_in_do(unsigned n)
    ensures ((n == 1u && result == 5u) || (n != 1u && result == 0u))
{
    unsigned r = 0u;
    switch (n) {
        case 1u: {
            unsigned i = 0u;
            do
                invariant (i <= 3u)
                decreases (3u - i)
            {
                i = i + 1u;
                switch (i) {
                    case 2u:
                        continue;
                }
            } while (i < 3u);
            r = 5u;
            break;
        }
    }
    return r;
}

pure unsigned zero() {
    return 0u;
}

proof nothing()
    proves (zero() == 0u)
{
    refl;
}

unsafe unsigned sample();

// A case the precondition rules out, claimed not to occur where it stands, and
// an unsafe block in a case, passed through as anywhere else: what it may write
// is unknown after it, and the contract rests on it.
verified unsigned guarded(unsigned x)
    expects (x < 3u)
    ensures (result <= 3u)
{
    unsigned y = 0u;
    switch (x) {
        case 0u: {
            unsafe {
                y = sample();
            }
            return 1u;
        }
        case 7u:
            contradiction nothing;
        default:
            y = x;
    }
    return y;
}

unsigned sample() {
    return 9u;
}

int main() {
    std::printf("%u %u %u\n", guarded(0u), guarded(1u), guarded(2u));
    std::printf("%u %u %u %u %u\n", fall_through(1u), fall_through(2u), fall_through(3u), fall_through(4u),
                default_first(5u));
    std::printf("%u %u %u %u %u\n", default_first(6u), default_middle(1u), default_middle(9u), default_middle(2u),
                no_default(4u));
    std::printf("%u %u %u %u %u\n", no_default(8u), colour(Color::blue), plain(2u), loop_with_switch(7u),
                loop_in_switch(1u));
    std::printf("%u %u %u %u %u %u\n", nested(1u, 2u), nested(1u, 3u), once(0u), once(1u), once(5u), declared(4u));
    std::printf("%u %u %u\n", commas(5u), continue_in_do(1u), continue_in_do(2u));
    return 0;
}
