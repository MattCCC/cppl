// SPEC: STDMODEL-012, STDMODEL-016
// False: when every character from `at` on is a digit the loop runs to the
// end and returns `in.size()`, which is not below it. Accepted twin:
// `skip_digits`, which claims `result <= in.size()`.
#include <cstddef>
#include <span>

verified std::size_t skip_digits_past(std::span<const char> in, std::size_t at)
    expects (readable(in) && at <= in.size())
    ensures (result < in.size())
{
    std::size_t i = at;
    while (i < in.size())
        invariant (at <= i && i <= in.size())
        decreases (in.size() - i)
    {
        const char c = in[i];
        if (c < '0' || c > '9') {
            return i;
        }
        ++i;
    }
    return i;
}

int main() {
    return 0;
}
