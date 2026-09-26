// SPEC: CLASS-009, CLASS-011
//
// `std::move(t)` names `t` itself, so the `&&` member function is called on
// `t`'s own places and its effect is `t`'s: after it `t.value` is 0, not the
// value it was made with. The accepted twin claims 0: `moved_take` in
// `fixtures/verified_methods.cpp`.
#include <utility>

struct Token {
    unsigned value;

    verified unsigned take() &&
        ensures (value == 0u)
    {
        const unsigned taken = value;
        value = 0u;
        return taken;
    }
};

verified unsigned stale(unsigned x)
    expects (x != 0u)
    ensures (result == x)
{
    Token t{x};
    const unsigned taken = std::move(t).take();
    return t.value;
}

// `static_cast<Token>(t)` is a copy of `t`, a temporary: the call is made on
// the copy, which is not storage the caller holds, and `t` is left as it was.
// Treating the copy as `t` would prove this claim, false at run time.
verified unsigned copied(unsigned x)
    expects (x != 0u)
    ensures (result == 0u)
{
    Token t{x};
    const unsigned taken = static_cast<Token>(t).take();
    return t.value;
}

int main() {
    return static_cast<int>(stale(3u) + copied(3u));
}
