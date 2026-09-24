// SPEC: UNSAFE-002
// `unsafe` does not waive what `verified` asks for, so the two do not combine.
unsafe verified unsigned identity(unsigned x)
    ensures (result == x)
{
    return x;
}

int main() {
    return static_cast<int>(identity(0u));
}
