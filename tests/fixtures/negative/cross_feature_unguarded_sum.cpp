// SPEC: ARITH-006, DEFINEDBEHAVIOR-001, STDMODEL-012
// `guarded_sum` of fixtures/cross_feature/client.cpp adding every element
// unguarded: two elements of 2^62 overflow `long long`, so the addition owes a
// no-overflow obligation nothing proves.
#include <cstddef>
#include <span>

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
        total = total + value;
        ++i;
    }
    return total;
}
