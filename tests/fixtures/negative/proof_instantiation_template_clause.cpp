// SPEC: ERASE-019, TEMPLATE-001
// A verified template's clause is instantiated with each of its
// specializations. Here it reads the argument it is given, which completes
// Set<0>, while the body and the program run, holding it by reference, never
// do. The clause stands only in the program verified, so it is refused.
#include "../include/stateful_friend.hpp"

template <class T>
verified unsigned zero(const T& s)
    ensures (s.unused == s.unused && result == 0u)
{
    return 0u;
}

unsigned through(const Set<0>& s)
{
    return zero(s);
}

int main()
{
    return 0;
}
