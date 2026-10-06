// Refused twin of fixtures/conditions.cpp (tests/negative/refused_twins.sh): the same
// program, except that countdown's invariant has no case for the entry state.
// SPEC: VERIFIED-021, EXPR-014, EXPR-015, EXPR-016, SPECEXPR-002, STDMODEL-012, LOOP-004
// Conditions in verified bodies, from the shapes realistic programs write: an
// element read in an `if` or loop condition; `&&` and `||` computed as values;
// a returned `?:`, `&&` or `||` whose arms read elements; loop invariants
// stating `&&` and `||`, nested in each other, each operand specified on its
// own; `&&` and `||` as values in a definition and a contract's term; a ghost
// snapshot related to the loop case by case; callers taking apart
// the disjunction inside a callee's postcondition; and a flag that decides a
// branch. e2e/conditions.sh verifies and runs this; negative/conditions.sh
// refuses its false twins.
#include <cstddef>
#include <cstdio>
#include <span>
#include <vector>

// The element the condition reads is formed where the condition reads it, and
// is the one the return reads again: only a digit reaches the subtraction.
verified int digit_at(std::span<const char> in, std::size_t i)
    expects (readable(in) && i < in.size())
    ensures (-1 <= result && result <= 9)
{
    if (in[i] < '0' || in[i] > '9') {
        return -1;
    }
    return in[i] - '0';
}

// The loop condition reads the element only where its first operand holds.
verified std::size_t find(const std::vector<int>& v, int key)
    ensures (result <= v.size())
{
    std::size_t i = 0;
    while (i < v.size() && v[i] != key)
        invariant (i <= v.size())
        decreases (v.size() - i)
    {
        ++i;
    }
    return i;
}

// A returned `&&`, `?:` and `||` read the element on the arm that evaluates it.
verified bool holds_at(const std::vector<int>& v, std::size_t i, int key)
    ensures (result -> i < v.size())
{
    return i < v.size() && v[i] == key;
}

verified int at_or(const std::vector<int>& v, std::size_t i, int fallback)
    ensures (i < v.size() || result == fallback)
{
    return i < v.size() ? v[i] : fallback;
}

verified bool past_or_zero(const std::vector<int>& v, std::size_t i)
    ensures (i >= v.size() -> result)
{
    return i >= v.size() || v[i] == 0;
}

// `&&` and `||` as values are what C++ evaluates: the division happens only
// where the divisor is nonzero.
verified bool divides(unsigned a, unsigned b)
    ensures (result -> b != 0u)
{
    const bool even = b != 0u && a % b == 0u;
    return even;
}

verified bool zero_or_divides(unsigned a, unsigned b)
    ensures (b == 0u -> result)
{
    bool divided = false;
    divided = b == 0u || a % b == 0u;
    return divided;
}

verified bool ordered(int a, int b, int c)
    ensures (result <-> (a <= b && b <= c))
{
    const bool low = a <= b;
    bool both = low && b <= c;
    return both;
}

// In a definition and in a contract's term, `&&` and `||` are values too:
// `a ? b : false` and `a ? true : b`.
pure bool both(bool a, bool b) {
    return a && b;
}

pure bool either_of(bool a, bool b) {
    return a || b;
}

law both_with_false(bool a)
    proves (!both(a, false));

law both_of_true(bool a)
    expects (a)
    proves (both(a, true));

law either_from_right(bool a, bool b)
    expects (b)
    proves (either_of(a, b));

verified bool in_order(int a, int b, int c)
    ensures (result == (a <= b && b <= c))
{
    return a <= b && b <= c;
}

verified unsigned any_positive(unsigned x, unsigned y)
    ensures (result == (x > 0u || y > 0u ? 1u : 0u))
{
    if (x > 0u || y > 0u) {
        return 1u;
    }
    return 0u;
}

// A flag the loop sets once it finds the key: the invariant states which of
// two situations holds.
verified std::size_t index_of(const std::vector<int>& v, int key)
    ensures (result <= v.size())
{
    std::size_t i = 0;
    bool found = false;
    while (!found && i < v.size())
        invariant ((found && i < v.size()) || (!found && i <= v.size()))
        decreases (v.size() - i, found ? 0u : 1u)
    {
        if (v[i] == key) {
            found = true;
        } else {
            ++i;
        }
    }
    return found ? i : v.size();
}

// A flag the loop may set: the invariant states which of two situations
// holds, each a conjunction.
verified unsigned countdown(unsigned n, unsigned mark)
    ensures (result <= 1u)
{
    unsigned i = n;
    unsigned seen = 0u;
    while (i > 0u)
        invariant ((seen == 1u && i < n) || (seen == 0u && i < n))
        decreases (i)
    {
        if (i == mark) {
            seen = 1u;
        }
        i = i - 1u;
    }
    return seen;
}

// What the loop remembers is either nothing yet or a position below the limit.
verified unsigned last_below(unsigned n, unsigned limit)
    ensures (result == n || result < limit)
{
    unsigned last = n;
    for (unsigned i = 0u; i < n; ++i)
        invariant (i <= n && (last == n || last < limit))
        decreases (n - i)
    {
        if (i < limit) {
            last = i;
        }
    }
    return last;
}

// The snapshot relates the loop to its entry case by case: `b0 > 0u` holds or
// fails, and where it fails the loop never ran.
verified unsigned gcd(unsigned a, unsigned b)
    ensures (b == 0u -> result == a)
{
    ghost unsigned a0 = a;
    ghost unsigned b0 = b;
    while (b != 0u)
        invariant (b0 > 0u || (a == a0 && b == 0u))
        decreases (b)
    {
        const unsigned t = a % b;
        a = b;
        b = t;
    }
    return a;
}

// Each callee states which argument it returns as a disjunction inside a
// conjunction; a caller takes it apart.
template <typename T>
verified T min_of(T a, T b)
    ensures (result <= a && result <= b && (result == a || result == b))
{
    return b < a ? b : a;
}

template <typename T>
verified T max_of(T a, T b)
    ensures (result >= a && result >= b && (result == a || result == b))
{
    return a < b ? b : a;
}

verified unsigned clamp(unsigned v, unsigned lo, unsigned hi)
    expects (lo <= hi)
    ensures (lo <= result && result <= hi && (result == v || result == lo || result == hi))
{
    return min_of(max_of(v, lo), hi);
}

verified int median(int a, int b, int c)
    ensures (result == a || result == b || result == c)
{
    return max_of(min_of(a, b), min_of(max_of(a, b), c));
}

// What a flag computed with `||` says is known case by case where it decides
// a branch, and a disjunction no side of which holds alone holds by one in
// each case.
verified unsigned either(unsigned x, unsigned y)
    ensures (result <= 1u && (result == 0u || x > 0u || y > 0u) && (result == 1u || (x == 0u && y == 0u)))
{
    const bool any = x > 0u || y > 0u;
    if (any) {
        return 1u;
    }
    return 0u;
}

verified unsigned bump(unsigned count, unsigned x)
    expects (count < 100u)
    ensures (result == count || (x > 0u && result == count + 1u))
{
    return count + (x > 0u ? 1u : 0u);
}

int main() {
    const char text[] = {'4', 'x'};
    const std::vector<int> v{3, 0, 7};
    std::printf("%d %d %zu %zu\n", digit_at(text, 0), digit_at(text, 1), find(v, 7), find(v, 9));
    std::printf("%d %d %d %d %d %d\n", holds_at(v, 1, 0) ? 1 : 0, holds_at(v, 5, 0) ? 1 : 0, at_or(v, 2, -1),
                at_or(v, 3, -1), past_or_zero(v, 1) ? 1 : 0, past_or_zero(v, 0) ? 1 : 0);
    std::printf("%d %d %d %d %d %d\n", divides(9u, 3u) ? 1 : 0, divides(9u, 0u) ? 1 : 0,
                zero_or_divides(9u, 0u) ? 1 : 0, zero_or_divides(9u, 2u) ? 1 : 0, ordered(1, 2, 3) ? 1 : 0,
                ordered(2, 1, 3) ? 1 : 0);
    std::printf("%zu %zu %d %d %u %u\n", index_of(v, 0), index_of(v, 5), in_order(1, 2, 3) ? 1 : 0,
                in_order(1, 3, 2) ? 1 : 0, any_positive(0u, 3u), any_positive(0u, 0u));
    std::printf("%u %u %u %u\n", countdown(5u, 2u), countdown(5u, 9u), last_below(6u, 3u), last_below(4u, 0u));
    std::printf("%u %u %u %u %d\n", gcd(12u, 18u), gcd(5u, 0u), clamp(9u, 2u, 4u), clamp(3u, 2u, 4u), median(3, 1, 2));
    std::printf("%u %u %u %u\n", either(0u, 4u), either(0u, 0u), bump(7u, 2u), bump(7u, 0u));
    return 0;
}
