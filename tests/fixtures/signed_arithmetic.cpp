// Signed arithmetic, division and integer conversions (SPEC.md 29, RFC 0019).
//
// Every function here owes, on each path that evaluates an operation, the
// condition C++ puts on it, and proves it from what the path knows: a guard, a
// precondition, a refinement, a loop invariant, a callee's postcondition. Each
// is the accepted twin of a program one step past the boundary in
// tests/fixtures/negative/arith_*.cpp, refused by tests/negative/signed_arithmetic.sh.
// `main` runs every function and checks what it computes, so the values the
// contracts state are the ones the program produces.

#include <climits>

// ---- representability at every width ----------------------------------------

// SPEC: ARITH-006, DEFINEDBEHAVIOR-001
verified int int_below_max_plus_one(int x)
    expects (x < INT_MAX)
    ensures (result == x + 1)
{
    return x + 1;
}

verified int int_above_min_minus_one(int x)
    expects (x > INT_MIN)
    ensures (result == x - 1)
{
    return x - 1;
}

verified long long int64_below_max_plus_one(long long x)
    expects (x < LLONG_MAX)
    ensures (result == x + 1)
{
    return x + 1;
}

verified long long int64_above_min_minus_one(long long x)
    expects (x > LLONG_MIN)
    ensures (result == x - 1)
{
    return x - 1;
}

// Narrow operands promote to `int`, which holds all their values, and their
// sum always fits there; the conversion back to the narrow type is what owes
// the bound (ARITH-008).
verified signed char int8_sum_within(signed char a, signed char b)
    expects (a + b <= 127 && a + b >= -128)
    ensures (result == a + b)
{
    return a + b;
}

verified short int16_sum_within(short a, short b)
    expects (a + b <= 32767 && a + b >= -32768)
    ensures (result == a + b)
{
    return a + b;
}

// The product of two `unsigned char` values, or of two `short` values, promoted
// to `int` always fits it. Two `unsigned short` values do not
// (arith_int16_unsigned_product) unless a factor is bounded.
verified int int8_product(unsigned char a, unsigned char b)
    ensures (result == a * b)
{
    return a * b;
}

verified int int16_product(short a, short b)
    ensures (result == a * b)
{
    return a * b;
}

// SPEC: ARITH-003, ARITH-006
// Two `unsigned char` operands promote to `int`, so `a + b` is a signed `int`
// addition that owes representability, which their types discharge: 255 + 255
// is 510, not 8-bit wrapping (arith_unsigned_char_not_wrapped).
verified int unsigned_char_sum(unsigned char a, unsigned char b)
    ensures (result == a + b && result <= 510)
{
    return a + b;
}

// Two `unsigned short` operands multiply in `int` too; a bounded factor keeps
// the product representable there.
verified int unsigned_short_doubled(unsigned short a, unsigned short b)
    expects (b == 2)
    ensures (result == a * b && result <= 131070)
{
    return a * b;
}

verified int int32_scaled(int x)
    expects (x >= -2147483 && x <= 2147483)
    ensures (result == x * 1000)
{
    return x * 1000;
}

verified long long int64_doubled(long long x)
    expects (x >= -4611686018427387904LL && x <= 4611686018427387903LL)
    ensures (result == 2 * x)
{
    return x * 2;
}

// Unsigned arithmetic is modular and owes nothing.
verified unsigned uint32_wraps(unsigned x)
    ensures (result == x + 1u)
{
    return x + 1u;
}

verified unsigned long long uint64_wraps(unsigned long long x)
    ensures (result == x * 3u)
{
    return x * 3u;
}

// ---- negation ------------------------------------------------------------------

verified int negated(int x)
    expects (x > INT_MIN)
    ensures (result == -x && result + x == 0)
{
    return -x;
}

verified unsigned negated_unsigned(unsigned x)
    ensures (result + x == 0u)
{
    return -x;
}

// ---- division and remainder -------------------------------------------------

// SPEC: ARITH-007, DEFINEDBEHAVIOR-002, DEFINEDBEHAVIOR-003
verified int divided(int x, int y)
    expects (y > 0)
    ensures (result == x / y)
{
    return x / y;
}

verified int divided_safely(int x, int y)
    expects (y != 0 && x != INT_MIN)
    ensures (result == x / y)
{
    return x / y;
}

verified int remainder_safely(int x, int y)
    expects (y != 0 && y != -1)
    ensures (result == x % y)
{
    return x % y;
}

// The quotient truncates toward zero and the remainder takes the dividend's
// sign, in every combination of signs.
verified int negative_quotient(int x)
    expects (x == -7)
    ensures (result == -3)
{
    return x / 2;
}

verified int negative_remainder(int x)
    expects (x == -7)
    ensures (result == -1)
{
    return x % 2;
}

verified int quotient_by_negative(int x)
    expects (x == 7)
    ensures (result == -3)
{
    return x / -2;
}

verified int remainder_by_negative(int x)
    expects (x == 7)
    ensures (result == 1)
{
    return x % -2;
}

verified int both_negative(int x)
    expects (x == -7)
    ensures (result == 3)
{
    return x / -2;
}

// What truncation gives for any dividend, with no value known: the division
// identity, the quotient's sign and the remainder's.
verified int halved(int x)
    ensures (result * 2 + x % 2 == x)
{
    return x / 2;
}

verified int halved_nonpositive(int x)
    expects (x <= 0)
    ensures (result <= 0 && result >= x)
{
    return x / 2;
}

verified int remainder_nonpositive(int x)
    expects (x < 0)
    ensures (result <= 0 && result > -10)
{
    return x % 10;
}

// A hash index stays below the table's size.
verified unsigned bucket(unsigned hash, unsigned size)
    expects (size > 0u)
    ensures (result < size)
{
    return hash % size;
}

// ---- conversions ----------------------------------------------------------------

// SPEC: ARITH-008
verified short narrowed(int x)
    expects (x >= SHRT_MIN && x <= SHRT_MAX)
    ensures (result == x)
{
    return x;
}

verified int from_unsigned(unsigned u)
    expects (u <= 2147483647u)
    ensures (result >= 0)
{
    return u;
}

verified signed char cast_down(int x)
    expects (x >= -128 && x <= 127)
    ensures (result == x)
{
    return static_cast<signed char>(x);
}

// A conversion to an unsigned type reduces modulo 2^width and owes nothing.
verified unsigned char to_unsigned_char(int x)
    ensures (result == static_cast<unsigned char>(x))
{
    return x;
}

verified unsigned wraps_negative(int x)
    expects (x == -1)
    ensures (result == 4294967295u)
{
    return x;
}

verified long long widened(int x)
    ensures (result == x)
{
    return x;
}

// Mixed signedness converts the signed operand to unsigned, so a negative value
// is never below `0u`.
verified unsigned never_below_zero(int i)
    ensures (result == 1u)
{
    if (i < 0u)
        return 0u;
    return 1u;
}

// A digit character's value, whatever the signedness of `char`.
verified int digit_value(char c)
    expects (c >= '0' && c <= '9')
    ensures (result >= 0 && result <= 9)
{
    return c - '0';
}

// ---- paths, refinements, loops and calls -------------------------------------

// A guard supplies the bound.
verified int saturating_increment(int x)
    ensures (result >= x)
{
    if (x < INT_MAX)
        return x + 1;
    return x;
}

// Each arm of `?:` owes only its own operation.
verified int toward_zero(int x)
    ensures (result >= -2147483647)
{
    return x > 0 ? x - 1 : x == INT_MIN ? x + 1 : x;
}

// The else arm of `?:` owes its operation where the condition fails, which the
// precondition keeps above the least value. An initializer is one expression,
// not a route of its own, so the arm's outcome is what protects it.
verified int away_from_zero_below(int x)
    expects (x > INT_MIN)
    ensures (result <= x)
{
    int y = x >= 0 ? x : x - 1;
    return y;
}

// The then arm owes its operation where the condition holds, which here is
// below the greatest value.
verified int saturated_successor(int x)
    ensures (result >= x)
{
    int y = x < INT_MAX ? x + 1 : x;
    return y;
}

// An operation on the right of `&&` runs only where the left is true.
verified int stepped_if_small(int x)
    ensures (result == x || result < -5)
{
    if (x > INT_MIN && x - 1 < -5)
        return x - 1;
    return x;
}

// SPEC: ARITH-006, ARITH-009, DEFINEDBEHAVIOR-001
// An obligation is proven from what the path knows before the operation, never
// from its result. Here the precondition proves it and the test after it is only
// a test (arith_result_guard_too_late refuses the body without it).
verified int successor_or_zero(int x)
    expects (x < INT_MAX)
    ensures (result >= x || result == 0)
{
    int y = x + 1;
    if (y < x)
        return 0;
    return y;
}

// A postcondition is proven of the result and never supposed to excuse the
// operation computing it (arith_postcondition_not_supposed).
verified int successor_above(int x)
    expects (x < INT_MAX)
    ensures (result > x)
{
    return x + 1;
}

// `(x + 1) - 1 == x` is a modular identity; as a signed claim it needs `x + 1`
// representable, which the precondition gives (arith_modular_identity).
verified int predecessor_of_successor(int x)
    expects (x < INT_MAX)
    ensures (result - 1 == x)
{
    return x + 1;
}

verified int successor_cancels(int x)
    expects (x < INT_MAX)
    ensures (x + 1 - 1 == x)
{
    return x;
}

// `x + 1` in the callee owes its obligation under the callee's precondition,
// and each call site owes that precondition (arith_call_site_precondition).
verified int second_successor(int x)
    expects (x < INT_MAX - 1)
    ensures (result == x + 2)
{
    int y = int_below_max_plus_one(x);
    return int_below_max_plus_one(y);
}

// A division on the right of `||` runs only where the left is false.
verified int ratio_or_zero(int x, int y)
    expects (x != INT_MIN)
    ensures (result >= 0)
{
    if (y == 0 || x / y < 0)
        return 0;
    return x / y;
}

type Small = int where (self >= -1000 && self <= 1000);

verified int tripled(Small x)
    ensures (result == 3 * x)
{
    return 3 * x;
}

// A signed counter bounded by its invariant.
verified int thousands(int n)
    expects (n >= 0 && n <= 2147483)
    ensures (result == n * 1000)
{
    int total = 0;
    for (int i = 0; i < n; ++i)
        invariant (0 <= i && i <= n && total == i * 1000)
    {
        total += 1000;
    }
    return total;
}

// Counting down, and a measure converted to an unsigned type.
verified int countdown(int n)
    expects (n >= 0)
    ensures (result == 0)
{
    int i = n;
    while (i > 0)
        invariant (i >= 0)
        decreases (static_cast<unsigned>(i))
    {
        --i;
    }
    return i;
}

// Compound assignments are the assignments they abbreviate.
verified int compound(int x)
    expects (x >= 0 && x <= 1000)
    ensures (result == (x * 3 - 4) / 2 % 7)
{
    int y = x;
    y *= 3;
    y -= 4;
    y /= 2;
    y %= 7;
    return y;
}

// A callee's postcondition bounds its result, and the caller's own arithmetic
// on it is proven from that.
verified int bounded_half(int x)
    ensures (result >= -1073741824 && result <= 1073741823)
{
    return x / 2;
}

verified int doubled_half(int x)
    ensures (result >= -2147483648 && result <= 2147483646)
{
    return bounded_half(x) * 2;
}

// An operation on a call's result runs after the call returns, so it may use
// what the callee promised, even a promise only a call that never returns can
// keep. `main` never calls this.
verified int stuck(int x)
    ensures (result == 0 && result == 1)
{
    for (;;)
        invariant (x == x)
    {
    }
}

verified int after_the_call(int x)
    ensures (result == result)
{
    return stuck(x) + 1;
}

// A postcondition means what C++ would compute, so its own arithmetic must be
// defined for the result the body returns.
verified int successor_is_larger(int x)
    expects (x < INT_MAX)
    ensures (result + 1 > result)
{
    return x;
}

// SPEC: ARITH-010, BOUNDARYEX-001
// A specification's operation is defined where its route selects it: the else
// arm of `?:` where the condition fails, the right of `||` where the left
// fails, the right of `&&` where the left holds. Here that route never reaches
// the least value, so each postcondition holds at it too.
verified int predecessor_or_zero(int x)
    ensures (x == INT_MIN ? result == 0 : result == x - 1)
{
    if (x == INT_MIN)
        return 0;
    return x - 1;
}

verified int predecessor_unless_least(int x)
    ensures (x == INT_MIN || result == x - 1)
{
    if (x == INT_MIN)
        return 0;
    return x - 1;
}

verified int predecessor_above_least(int x)
    ensures (result == 0 || (x > INT_MIN && result == x - 1))
{
    if (x == INT_MIN)
        return 0;
    return x - 1;
}

// An argument is evaluated, and owes its condition, before the call.
verified int successor_half(int x)
    expects (x < INT_MAX)
    ensures (result >= -1073741824)
{
    return bounded_half(x + 1);
}

// Each specialization is verified with the conversions its own types give it:
// `unsigned char` promotes to `int` and converts back modulo 256, `long long`
// adds at its own width.
template <typename T>
verified T incremented(T x)
    expects (x < 100)
    ensures (result == x + 1)
{
    return x + 1;
}

// SPEC: ARITH-003, ARITH-006
// The same written `a + b` in an unsigned common type wraps and owes nothing;
// at `int` it owes representability (arith_template_signed_instance).
template <typename T>
verified T wrapped_sum(T a, T b)
    ensures (result == a + b)
{
    return a + b;
}

int main() {
    int failures = 0;
    const auto check = [&failures](bool holds) {
        failures += holds ? 0 : 1;
    };
    check(int_below_max_plus_one(41) == 42);
    check(int_above_min_minus_one(-41) == -42);
    check(int64_below_max_plus_one(9000000000LL) == 9000000001LL);
    check(int64_above_min_minus_one(-9000000000LL) == -9000000001LL);
    check(int8_sum_within(100, 27) == 127);
    check(int16_sum_within(-30000, -2768) == -32768);
    check(int8_product(255, 255) == 65025);
    check(int16_product(-32768, -32768) == 1073741824);
    check(int32_scaled(-2147483) == -2147483000);
    check(int64_doubled(4611686018427387903LL) == 9223372036854775806LL);
    check(uint32_wraps(4294967295u) == 0u);
    check(uint64_wraps(6148914691236517206ull) == 2u);
    check(negated(-5) == 5);
    check(negated_unsigned(1u) == 4294967295u);
    check(divided(-7, 2) == -3);
    check(divided_safely(7, -1) == -7);
    check(remainder_safely(-7, 3) == -1);
    check(negative_quotient(-7) == -3);
    check(negative_remainder(-7) == -1);
    check(quotient_by_negative(7) == -3);
    check(remainder_by_negative(7) == 1);
    check(both_negative(-7) == 3);
    check(halved(-9) == -4);
    check(halved_nonpositive(-1) == 0);
    check(remainder_nonpositive(-23) == -3);
    check(bucket(1234567u, 10u) == 7u);
    check(narrowed(-32768) == -32768);
    check(from_unsigned(2147483647u) == 2147483647);
    check(cast_down(-128) == -128);
    check(to_unsigned_char(-1) == 255);
    check(wraps_negative(-1) == 4294967295u);
    check(widened(-1) == -1LL);
    check(never_below_zero(-1) == 1u);
    check(digit_value('7') == 7);
    check(saturating_increment(INT_MAX) == INT_MAX);
    check(toward_zero(INT_MIN) == -2147483647);
    check(away_from_zero_below(-5) == -6);
    check(saturated_successor(INT_MAX) == INT_MAX);
    check(saturated_successor(-1) == 0);
    check(stepped_if_small(INT_MIN) == INT_MIN);
    check(stepped_if_small(-9) == -10);
    check(predecessor_or_zero(INT_MIN) == 0);
    check(predecessor_unless_least(5) == 4);
    check(predecessor_above_least(INT_MIN) == 0);
    check(ratio_or_zero(9, 0) == 0);
    check(ratio_or_zero(9, 2) == 4);
    check(tripled(-1000) == -3000);
    check(thousands(2147483) == 2147483000);
    check(countdown(5) == 0);
    check(compound(10) == 6);
    check(doubled_half(INT_MAX) == 2147483646);
    check(successor_half(9) == 5);
    check(incremented<int>(99) == 100);
    check(incremented<unsigned char>(99) == 100);
    check(incremented<long long>(-5) == -4);
    check(wrapped_sum<unsigned>(4294967295u, 2u) == 1u);
    check(wrapped_sum<unsigned long long>(18446744073709551615ull, 3ull) == 2ull);
    check(unsigned_char_sum(255, 255) == 510);
    check(unsigned_short_doubled(65535, 2) == 131070);
    check(successor_or_zero(41) == 42);
    check(successor_above(-1) == 0);
    check(predecessor_of_successor(-2147483647 - 1) == -2147483647);
    check(successor_cancels(7) == 7);
    check(second_successor(40) == 42);
    return failures;
}
