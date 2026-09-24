// SPEC: TERMINATION-005, LOOP-006
// A lexicographic measure falls when its first component falls, or when the
// first stays and the rest fall. Here the first stays and the second grows.
verified unsigned grows(unsigned rows, unsigned cols)
    expects (cols < 10u)
    ensures (result == 0u)
{
    unsigned r = rows;
    unsigned c = cols;
    while (r > 0u)
        invariant (c <= 10u)
        decreases (r, c)
    {
        if (c < 10u) {
            c = c + 1u;
        } else {
            r = r - 1u;
            c = 0u;
        }
    }
    return r;
}

int main() {
    return static_cast<int>(grows(2u, 3u));
}
