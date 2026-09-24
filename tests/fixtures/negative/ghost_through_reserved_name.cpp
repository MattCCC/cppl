// SPEC: GHOST-002
// A declaration named with the prefix C++L gives what it generates would be
// read as generated, and its initializer as a specification expression, so a
// ghost could reach code that runs through it. Such a name is refused.
verified unsigned sneaks(unsigned x)
    ensures (result == x)
{
    ghost unsigned g = x;
    unsigned __cppl_copy = g;
    return __cppl_copy;
}

int main() {
    return static_cast<int>(sneaks(2u));
}
