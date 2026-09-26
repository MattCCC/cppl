// SPEC: STDMODEL-014
// A span is a value: taken by reference, the callee could point it at other
// storage than the one its generation is followed for.
#include <span>

verified void retarget(std::span<const unsigned>& s)
    ensures (true)
{
}

int main() {
    return 0;
}
