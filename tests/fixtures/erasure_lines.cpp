// Erasure keeps every runtime line and column where the author wrote it
// (SPEC.md ERASE-005, ERASEMATRIX-003; ARCHITECTURE.md ARCH-ERASE-003).
//
// Each construct below spans several lines, and each function after one asks
// Clang, while it compiles the erased program, which line it stands on. The
// answer must be its line in this file: a construct that took or added a line
// would move every answer after it. `e2e/erasure_source_mapping.sh` reads the
// expected lines from this file itself.
#include <cstdio>

pure unsigned identity(unsigned x) {
    return x;
}

law identity_holds(unsigned x)
    expects (x < 100u)
    proves (identity(x) == x);

unsigned after_law() {
    return __builtin_LINE();
}

trusted law assumed(unsigned x)
    proves (identity(identity(x)) == identity(x));

unsigned after_trusted_law() {
    return __builtin_LINE();
}

enum class State : int { idle = -1, running = 3 };

proof running_holds(State s)
    proves (Eq<State>(s, s))
{
    cases s {
        State::idle => {
            refl;
        }

        State::running => {
            refl;
        }

        unnamed(value) => {
            refl;
        }
    }
}

unsigned after_proof() {
    return __builtin_LINE();
}

type Small = unsigned
    where (self < 10u && self != 7u);

type Index(unsigned n) = unsigned
    where (self < n);

unsigned after_refinements() {
    return __builtin_LINE();
}

proof nothing()
    proves (identity(0u) == 0u)
{
    refl;
}

verified unsigned clamp(unsigned x)
    expects (x < 1000u)
    ensures (result <= 10u)
{
    unsigned left = x;
    while (left > 10u)
        invariant (left <= x)
        decreases (left)
    {
        left = left - 1u;
    }
    if (left > 10u)
        contradiction nothing;
    return left;
}

unsigned after_contract() {
    return __builtin_LINE();
}

// Erasing a specifier blanks it in place, so the rest of its line keeps its
// columns: a warning about the parameter points where it is written.
verified unsigned first_of(unsigned x, unsigned unused_second)
    ensures (result == x)
{
    return x;
}

int main() {
    std::printf("%u %u %u %u %u %u %u %s\n", after_law(), after_trusted_law(), after_proof(), after_refinements(),
                after_contract(), clamp(12u), first_of(3u, 4u), __builtin_FILE());
}
