// SPEC: CONSTRUCT-077
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// placement construction, is refused.

#include <new>

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    alignas(unsigned) unsigned char storage[sizeof(unsigned)];
    unsigned* cell = new (storage) unsigned(x);
    (void)cell;
    return x;
}

int main() { return probe(2u) == 2u ? 0 : 1; }
