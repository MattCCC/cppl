// SPEC: ERASE-011, GHOST-001
// Ghost state has no runtime identity. Taking its address would give it runtime
// storage for code that runs to read, and the unit fails closed: no runtime
// program is produced.
verified unsigned counted(unsigned x)
    ensures (result == x)
{
    ghost unsigned seen = x;
    const unsigned* where = &seen;
    return x;
}

int main() {
    return static_cast<int>(counted(0u));
}
