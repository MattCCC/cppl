// SPEC: GHOST-001
// A ghost initializer never runs, so an effect it asks for would silently not
// happen: here the increment of `x`.
verified unsigned bumps(unsigned x)
    expects (x < 10u)
    ensures (result == x)
{
    unsigned y = x;
    ghost unsigned g = y++;
    return x;
}

int main() {
    return static_cast<int>(bumps(2u));
}
