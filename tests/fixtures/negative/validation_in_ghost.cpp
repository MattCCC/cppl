// SPEC: RUNTIMECHECK-019, GHOST-001
// A validation expression is runtime code, and a ghost declaration never runs:
// the validation is refused by name, once, without the compiler's own
// scaffolding showing through and without an erasure the checker disowns.
#include <cstdio>
#include <cstdlib>

type Positive = int where (self > 0);

verified int f(int raw)
    ensures (result == 1)
{
    ghost bool ok = validate<Positive>(raw);
    return 1;
}

int main(int argc, char** argv) {
    std::printf("%d\n", f(argc > 1 ? std::atoi(argv[1]) : 0));
}
