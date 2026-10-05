// Refused twin of `provenance_matrix.cpp`: `trusted_source` leaves out the
// contradiction through `counter_is_one`, the step that makes its path with
// `x == 7u` impossible, so its contract holds only with the trusted law and is
// refused without it (`negative/refused_twins.sh`).
#include <cstdio>
#include <string>
#include <vector>

type Positive = int where (self > 0);

pure unsigned zero() {
    return 0u;
}

trusted law broken_counter()
    proves (zero() == 1u);

trusted law broken_again()
    proves (zero() == 2u);

// Reached by nothing, so it is unused and no claim names it.
trusted law never_reached(unsigned x)
    proves (x * 1u == x);

proof counter_is_one()
    proves (zero() == 1u)
{
    exact broken_counter;
}

proof counter_is_two()
    proves (zero() == 2u)
{
    exact broken_again;
}

unsigned pokes = 0u;

void poke() {
    ++pokes;
}

// --- Each kind of dependency at its source ----------------------------------

verified unsigned trusted_source(unsigned x)
    expects (x < 10u)
    ensures (result != 7u)
{
    return x;
}

verified unsigned trusted_source_again(unsigned x)
    expects (x < 10u)
    ensures (result != 8u)
{
    if (x == 8u) {
        contradiction counter_is_two;
    }
    return x;
}

verified unsigned unsafe_source(unsigned x)
    ensures (result == x)
{
    unsafe {
        poke();
    }
    return x;
}

verified unsigned vector_source()
    ensures (result == 2u)
{
    std::vector<unsigned> v{1u, 2u};
    return static_cast<unsigned>(v.size());
}

verified unsigned string_source()
    ensures (result == 3u)
{
    std::string s = "abc";
    return static_cast<unsigned>(s.size());
}

verified int validation_source(int raw)
    ensures (result > 0)
{
    if (validate<Positive>(raw)) {
        Positive p = raw;
        return p;
    }
    return 1;
}

// --- Through one call, a chain of three, and both sides of a diamond --------

verified unsigned trusted_hop_1(unsigned x)
    expects (x < 10u)
    ensures (result != 7u)
{
    return trusted_source(x);
}

verified unsigned trusted_hop_2(unsigned x)
    expects (x < 10u)
    ensures (result != 7u)
{
    return trusted_hop_1(x);
}

verified unsigned trusted_hop_3(unsigned x)
    expects (x < 10u)
    ensures (result != 7u)
{
    return trusted_hop_2(x);
}

verified unsigned trusted_right(unsigned x)
    expects (x < 10u)
    ensures (result != 7u)
{
    return trusted_source(x);
}

verified unsigned trusted_diamond(unsigned x)
    expects (x < 10u)
    ensures (result == result)
{
    const unsigned left = trusted_hop_1(x);
    const unsigned right = trusted_right(x);
    return left + right;
}

verified unsigned unsafe_hop_1(unsigned x)
    ensures (result == x)
{
    return unsafe_source(x);
}

verified unsigned unsafe_hop_2(unsigned x)
    ensures (result == x)
{
    return unsafe_hop_1(x);
}

verified unsigned unsafe_hop_3(unsigned x)
    ensures (result == x)
{
    return unsafe_hop_2(x);
}

verified unsigned unsafe_right(unsigned x)
    ensures (result == x)
{
    return unsafe_source(x);
}

verified unsigned unsafe_diamond(unsigned x)
    ensures (result == result)
{
    const unsigned left = unsafe_hop_1(x);
    const unsigned right = unsafe_right(x);
    return left + right;
}

verified unsigned vector_hop_1()
    ensures (result == 2u)
{
    return vector_source();
}

verified unsigned vector_hop_2()
    ensures (result == 2u)
{
    return vector_hop_1();
}

verified unsigned vector_hop_3()
    ensures (result == 2u)
{
    return vector_hop_2();
}

verified unsigned vector_right()
    ensures (result == 2u)
{
    return vector_source();
}

verified unsigned vector_diamond()
    ensures (result == 4u)
{
    const unsigned left = vector_hop_1();
    const unsigned right = vector_right();
    return left + right;
}

verified int validation_hop_1(int raw)
    ensures (result > 0)
{
    return validation_source(raw);
}

verified int validation_hop_2(int raw)
    ensures (result > 0)
{
    return validation_hop_1(raw);
}

verified int validation_hop_3(int raw)
    ensures (result > 0)
{
    return validation_hop_2(raw);
}

verified int validation_right(int raw)
    ensures (result > 0)
{
    return validation_source(raw);
}

verified int validation_diamond(int raw)
    ensures (result > 0)
{
    const int left = validation_hop_1(raw);
    const int right = validation_right(raw);
    return left < right ? left : right;
}

// --- Around a recursion group: the dependency of one member is the other's.
// An unsafe block cannot be reached this way: nothing establishes that it
// terminates, so a recursion group through one is refused for its measure.

unsigned trusted_odd(unsigned n);

verified unsigned trusted_even(unsigned n)
    expects (n < 10u)
    ensures (result != 7u)
    decreases (n)
{
    if (n == 0u) {
        return trusted_source(0u);
    }
    return trusted_odd(n - 1u);
}

verified unsigned trusted_odd(unsigned n)
    expects (n < 10u)
    ensures (result != 7u)
    decreases (n)
{
    if (n == 0u) {
        return 0u;
    }
    return trusted_even(n - 1u);
}

unsigned vector_odd(unsigned n);

verified unsigned vector_even(unsigned n)
    ensures (result == 2u)
    decreases (n)
{
    if (n == 0u) {
        return vector_source();
    }
    return vector_odd(n - 1u);
}

verified unsigned vector_odd(unsigned n)
    ensures (result == 2u)
    decreases (n)
{
    if (n == 0u) {
        return 2u;
    }
    return vector_even(n - 1u);
}

// --- Several kinds at once, and nothing at all -------------------------------

verified unsigned two_laws(unsigned x)
    expects (x < 10u)
    ensures (result == result)
{
    return trusted_source(x) + trusted_source_again(x);
}

verified unsigned two_models()
    ensures (result == 5u)
{
    return vector_source() + string_source();
}

verified unsigned everything(unsigned x, int raw)
    expects (x < 10u)
    ensures (result == result)
{
    const unsigned counted = trusted_source(x) + unsafe_source(x) + vector_source() + string_source();
    const int checked = validation_source(raw);
    return checked > 0 ? counted + 1u : counted;
}

verified unsigned plain(unsigned x)
    ensures (result == x)
{
    return x;
}

verified unsigned beside(unsigned x)
    ensures (result == x)
{
    return plain(x);
}

int main() {
    std::printf("%u %u %u %u %u %d %u %u %u %u %u %u %u %u %d\n", trusted_hop_3(3u), trusted_diamond(3u),
                unsafe_hop_3(4u), unsafe_diamond(4u), vector_diamond(), validation_diamond(-5), trusted_odd(5u),
                vector_odd(3u), two_laws(2u), two_models(), everything(1u, 6), plain(9u), beside(9u), pokes,
                validation_hop_3(12));
    return 0;
}
