// SPEC: RUNTIMECHECK-020
// Validating a value against an indexed refinement is not supported.
type Index(unsigned n) = unsigned where (self < n);

verified unsigned index_or_zero(unsigned raw)
    ensures (result < 4u)
{
    if (validate<Index>(raw)) {
        return 0u;
    }
    return 0u;
}

int main() {
    return static_cast<int>(index_or_zero(3u));
}
