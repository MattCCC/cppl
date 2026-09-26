// SPEC: CONTRACT-010, CLASS-009, EDGECASE-058
// `const` does not make a member function write nothing: this one writes a
// `mutable` member, so the call may write its object and what the caller knew
// of `value` does not survive it. The accepted half calls a const member
// function of a class with no mutable member: `const_call_keeps` in
// `fixtures/verified_methods.cpp`.
struct Cache {
    unsigned value;
    mutable unsigned hits;

    verified unsigned get() const
        ensures (result == value)
    {
        hits = hits + 1u;
        return value;
    }
};

verified unsigned cached(unsigned x)
    ensures (result == x)
{
    Cache cache{x, 0u};
    const unsigned seen = cache.get();
    return cache.value;
}

int main() {
    return static_cast<int>(cached(5u));
}
