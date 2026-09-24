// SPEC: GHOST-002
// Ghost state does not make the program compute anything: a postcondition that
// holds of the ghost and not of what runs is not proven.
verified unsigned claims(unsigned x)
    ensures (result == 7u)
{
    ghost unsigned g = 7u;
    return x;
}

int main() {
    return static_cast<int>(claims(2u));
}
