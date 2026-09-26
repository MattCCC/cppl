// SPEC: STDMODEL-012
// A condition reads no storage the statement holding it has not formed: an
// element read directly in an `if` condition is refused, as a dereference
// there is, rather than read at a place no bound was owed for. Accepted twin:
// `skip_digits`, which reads the element into a local first.
#include <cstddef>
#include <span>

verified bool is_digit_at(std::span<const char> in, std::size_t i)
    expects (readable(in) && i < in.size())
    ensures (result == result)
{
    if (in[i] < '0' || in[i] > '9') {
        return false;
    }
    return true;
}

int main() {
    return 0;
}
