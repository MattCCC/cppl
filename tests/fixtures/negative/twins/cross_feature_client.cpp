// Refused twin of fixtures/cross_feature/client.cpp (tests/negative/refused_twins.sh): the same
// program, except that its contract on 20ll + delta + delta is off by one.
// Member functions of another unit over spans, vectors and strings; a view
// that outlives a member call; signed arithmetic over container values. This
// unit sees `buffers.hpp` and the interface `buffers.cpp` writes. Each refused
// twin in `negative/cross_feature_*.cpp` differs from a function here in one
// thing; `tests/negative/cross_feature.sh` drives them.
#include "buffers.hpp"

#include <cstddef>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

// SPEC: CLASS-011, STDMODEL-016, TUBOUND-003
// Member functions of another unit reading a span under its capability and
// appending to a vector the caller holds: the span stays readable, since a
// capability never designates a container the function holds by reference.
// No disjointness of an object and a reference argument is assumed, so after
// the call that may write through `out` only `emit`'s `ensures` is known of
// `cursor`, and the second read goes through a cursor made after it.
verified unsigned first_copied(std::span<const unsigned> in, std::vector<unsigned>& out)
    expects (readable(in) && 1ul <= in.size())
    ensures (true)
{
    const Cursor cursor{0ul};
    const unsigned seen = cursor.peek(in);
    const std::size_t written = cursor.emit(out, seen);
    const Cursor again{0ul};
    return seen + again.peek(in);
}

// SPEC: STDMODEL-015, CLASS-011
// A span over a local vector survives a member call that takes the vector by
// `const&`, which cannot reallocate it. Twin of
// `negative/cross_feature_span_after_mutating_call.cpp`.
verified unsigned view_after_const_call()
    ensures (true)
{
    std::vector<unsigned> v{5u, 6u};
    const std::span<const unsigned> view(v);
    const Cursor cursor{0ul};
    const std::size_t length = cursor.length_of(v);
    if (length == 2ul) {
        return view[1];
    }
    return 0u;
}

// SPEC: STDMODEL-013, CLASS-011
// A member function of another unit reading a string.
verified char second_character(const std::string& text)
    expects (2ul <= text.size())
    ensures (true)
{
    const Cursor cursor{1ul};
    return cursor.character(text);
}

// SPEC: ARITH-006, ARITH-008, CLASS-011
// Signed arithmetic in another unit's member function, and on its result here.
verified long long moved_twice(long long delta)
    expects (delta >= -1000ll && delta <= 1000ll)
    ensures (result == 21ll + delta + delta)
{
    const Cursor cursor{10ul};
    return cursor.moved(delta) + cursor.moved(delta);
}

// SPEC: TUBOUND-006, TCB-REPORT-005
// Through a member function of another unit whose proof rests on an unsafe
// block: this claim rests on it too.
verified std::size_t configured()
    ensures (result <= 4096ul)
{
    const Cursor cursor{0ul};
    return cursor.setting();
}

// SPEC: STDMODEL-013, STDMODEL-018, TUBOUND-006
// A string built here and passed by value to another unit's function: the
// literal's length and the imported contract give the result.
verified std::size_t suffixed_pair()
    ensures (result == 3ul)
{
    const std::string pair = "ab";
    return suffixed_length(pair);
}

// SPEC: ARITH-006, STDMODEL-012, STDMODEL-016
// A signed sum over a span's elements, each added only once it is bounded, so
// the running total stays within the bound the invariant states. Twin of
// `negative/cross_feature_unguarded_sum.cpp`.
verified long long guarded_sum(std::span<const long long> in)
    expects (readable(in) && in.size() <= 4096ul)
    ensures (-4096000000ll <= result && result <= 4096000000ll)
{
    long long total = 0ll;
    std::size_t i = 0ul;
    while (i < in.size())
        invariant (i <= in.size() && in.size() <= 4096ul && total >= -1000000ll * static_cast<long long>(i) &&
                  total <= 1000000ll * static_cast<long long>(i))
        decreases (in.size() - i)
    {
        const long long value = in[i];
        if (value >= -1000000ll && value <= 1000000ll) {
            total = total + value;
        }
        ++i;
    }
    return total;
}

int main() {
    const std::vector<unsigned> input{4u, 9u};
    std::vector<unsigned> output;
    const std::vector<long long> values{-7ll, 2000000ll, 12ll};
    std::printf("%u %zu %u %c %lld %zu %zu %lld\n", first_copied(input, output), output.size(), view_after_const_call(),
                second_character("xyz"), moved_twice(-3ll), configured(), suffixed_pair(), guarded_sum(values));
    return 0;
}
