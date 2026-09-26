// SPEC: TUBOUND-006
// Claims proven through contracts of other units, alike but for what those
// contracts' proofs rest on. Only the ones through `plain`, directly and through
// the middle unit, are free of trusted assumptions; each of the others rests on
// the trusted law, the model or the unsafe block the other unit's proof used,
// carried by the records however many units away. Every one lists the records
// it rests on, with the interfaces they came from.
#include "closure.hpp"
#include "closure_middle.hpp"

#include <cstdio>

verified unsigned through_plain(unsigned x)
    expects (x < 50u)
    ensures (result == x + 1u)
{
    return plain(x);
}

verified unsigned through_trusting(unsigned x)
    expects (x < 10u)
    ensures (result != 7u)
{
    return trusting(x);
}

verified unsigned through_modeled()
    ensures (result == 2u)
{
    return modeled();
}

verified unsigned through_unsafe()
    ensures (result <= 100u)
{
    return unsafe_read();
}

verified unsigned through_relayed_plain(unsigned x)
    expects (x < 50u)
    ensures (result == x + 1u)
{
    return relayed_plain(x);
}

verified unsigned through_relayed_trusting(unsigned x)
    expects (x < 10u)
    ensures (result != 7u)
{
    return relayed_trusting(x);
}

verified unsigned through_relayed_unsafe()
    ensures (result <= 100u)
{
    return relayed_unsafe();
}

int main() {
    std::printf("%u %u %u %u %u %u %u\n", through_plain(1u), through_trusting(2u), through_modeled(), through_unsafe(),
                through_relayed_plain(3u), through_relayed_trusting(4u), through_relayed_unsafe());
    return 0;
}
