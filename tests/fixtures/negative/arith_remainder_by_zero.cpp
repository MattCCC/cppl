// SPEC: ARITH-007, DEFINEDBEHAVIOR-002
// An unsigned remainder owes a nonzero divisor too, and an empty table has
// none. The twin with `size > 0u` is bucket.
verified unsigned bucket_of_any_size(unsigned hash, unsigned size)
    ensures (result <= hash)
{
    return hash % size;
}

int main() {
    return static_cast<int>(bucket_of_any_size(7u, 4u)) - 3;
}
