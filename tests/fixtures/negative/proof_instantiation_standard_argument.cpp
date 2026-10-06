// SPEC: ERASE-019
// A standard template is the implementation's; what it is instantiated at is
// the program's. The alias names std::optional<Set<0>> without instantiating
// it. Asking the optional whether it holds a value instantiates it, which
// completes Set<0>, before E::A is initialized, and which() would be verified
// to return 1 and run to print 0.
#include "../include/stateful_friend.hpp"

#include <cstdio>
#include <optional>

using Maybe = std::optional<Set<0>>;

law held(const Maybe& o)
    proves (o.has_value() || !o.has_value());

enum class E : unsigned { A = defined<0>() ? 1u : 0u };

verified unsigned which()
    ensures (result == 1u)
{
    return static_cast<unsigned>(E::A);
}

int main()
{
    std::printf("%u\n", which());
    return 0;
}
