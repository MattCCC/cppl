// SPEC: ERASE-017
// A directive inside an expression or a statement C++L states cannot stay where
// it stands: the analysed program copies the expression into generated C++,
// where the directive would stand inside an expression. Each is refused by
// name, at its own line, rather than kept in one program and lost in the other.
#include <cstdio>

law bounded(unsigned x)
    proves (x <
#pragma pack(push, 1)
            100u || x >= 100u);

proof bounded_holds(unsigned x)
    proves (bounded(x))
{
    assume h : x <
#pragma pack(push, 1)
        100u;
    refl;
}

verified unsigned keep(unsigned n)
    ensures (result == n)
{
    ghost unsigned seen =
#pragma pack(push, 1)
        n;
    return n;
}

#pragma pack(pop)
#pragma pack(pop)
#pragma pack(pop)

int main() {
    std::printf("%u\n", keep(3u));
}
