// SPEC: ERASE-011, WORD-008
// Ghost state is specified (SPEC.md 36.4) and not implemented. `ghost` is then
// an ordinary name, this declaration keeps its C++ reading, and Clang refuses
// it. The unit fails closed: no runtime program is produced, so no ghost value
// is ever given runtime storage.
verified unsigned counted(unsigned x)
    ensures (result == x)
{
    ghost unsigned seen = x;
    return x;
}

int main() {
    return static_cast<int>(counted(0u));
}
