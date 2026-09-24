// SPEC: TERMINATION-006
// A measure on a function template would have to be checked at every
// specialization, which this implementation does not force yet, so it is
// refused rather than dropped.
template <unsigned N>
verified unsigned bounded(unsigned n)
    expects (n < N)
    ensures (result == 0u)
    decreases (n)
{
    if (n == 0u) {
        return 0u;
    }
    return bounded<N>(n - 1u);
}

int main() {
    return static_cast<int>(bounded<8u>(3u));
}
