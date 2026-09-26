// SPEC: TUBOUND-004
// A third overload of step, declared here with a contract no unit proved. Its
// contract states what the `unsigned` overload's does, but it is another
// function, and no interface records it.
#include "library.hpp"

verified int step(int x)
    ensures (result == x);

verified int stepped(int narrow)
    ensures (result == narrow)
{
    return step(narrow);
}
