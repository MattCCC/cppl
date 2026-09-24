// SPEC: UNSAFE-003
// A verified body passes through an unsafe block and goes on after it. A return
// from inside would end the path with a value nothing checked.
verified unsigned returns_inside(unsigned a)
    ensures (result == a)
{
    unsafe {
        return a;
    }
}

int main() {
    return static_cast<int>(returns_inside(1u));
}
