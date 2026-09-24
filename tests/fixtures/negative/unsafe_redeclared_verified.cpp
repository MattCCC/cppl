// SPEC: UNSAFE-002
// A function is unsafe wherever any of its declarations says so, however it is
// spelled elsewhere. One declared verified here and unsafe below would have its
// contract proven and its calls cross an unsafe boundary at once.
verified unsigned identity(unsigned x)
    ensures (result == x)
{
    return x;
}

unsafe unsigned identity(unsigned x);

int main() {
    return static_cast<int>(identity(0u));
}
