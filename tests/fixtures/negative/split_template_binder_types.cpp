#include <optional>

// SPEC: CASE-017
// A split's binders are declared once for the split as written, so in a
// template whose specializations bind values of different types, at most one
// specialization can agree with the declared type. The others are refused
// rather than read at a type their case does not bind.
proof same(unsigned x)
    proves (x == x)
{
    refl;
}

template <typename T>
verified unsigned payload(std::optional<T> o)
    ensures (result == 1u)
{
    cases o {
        some(v) => {
            contradiction same(v);
        }

        none => {
        }
    }
    return 1u;
}

int main() {
    return static_cast<int>(payload<unsigned>(3u) + payload<int>(4));
}
