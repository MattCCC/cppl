// SPEC: GHOST-001, UNSAFE-003
// An unsafe block's statements are not a path the verifier walks, so ghost
// state declared there would never be checked.
verified unsigned inside(unsigned x)
    ensures (result == x)
{
    unsafe {
        ghost unsigned g = x;
    }
    return x;
}

int main() {
    return static_cast<int>(inside(2u));
}
