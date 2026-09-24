// SPEC: GHOST-001, REFINE-008
// A ghost of a refinement type owes its predicate where it is declared, like
// any other value entering the type.
type Small = unsigned where (self < 10u);

verified unsigned narrows(unsigned x)
    ensures (result == x)
{
    ghost Small s = x;
    return x;
}

int main() {
    return static_cast<int>(narrows(20u));
}
