// A specification that would never become an obligation is refused, not
// ignored.
//
// The danger these guard against is a contract that reads as though it were
// checked while nothing ever checks it. Silently ignoring one would leave a
// program annotated with claims the trust report never covers, which is worse
// than having written no contract at all: the reader believes a proof exists.
#include <iostream>

pure unsigned identity(unsigned x) { return x; }

// A contract only becomes an obligation on a 'verified' function. 'pure' makes
// a function usable in specifications; it does not discharge a contract, so a
// clause written here would never be checked.
pure unsigned unchecked_contract(unsigned x)
    ensures (result == x)
{
    return x;
}

// A loop invariant is checked because its enclosing function is verified. In an
// ordinary function nothing would establish it.
unsigned unchecked_invariant(unsigned n) {
    unsigned i = 0u;
    while (i < n)
        invariant (i <= n)
    {
        i = i + 1u;
    }
    return i;
}

int main() {
    std::cout << identity(41u) << "\n";
    return 0;
}
