// SPEC: WORD-001, WORD-007
// A function named `old` stays ordinary C++ wherever no postcondition reads the
// entry-value form (SPEC.md 11.4, GRAMMAR.md 13): in code that runs, in a
// verified body, in a precondition, in a Law, and as a member. Inside a
// postcondition `old(...)` is formal, and refused while entry values are not
// supported (negative/old_shadowed_by_function.cpp); a postcondition calls such
// a function by a qualified name, which is not the form.

#include <cstdio>

pure unsigned old(unsigned v) {
    return v + 1u;
}

struct Archive {
    unsigned stored;
    unsigned old() const {
        return stored;
    }
};

// In a Law the name is the function.
law old_increments(unsigned x)
    proves (old(x) == x + 1u);

// In a precondition and in a verified body likewise, and in a postcondition
// through its qualified name.
verified unsigned bumped(unsigned x)
    expects (old(x) < 10u)
    ensures (result == ::old(x))
{
    return old(x);
}

int main() {
    unsigned old_value = old(1u);
    Archive archive{old_value};
    std::printf("%u %u\n", bumped(3u), archive.old());
}
