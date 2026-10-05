// SPEC: CONSTRUCT-063
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// conversion function call, is refused.

struct Wrapped {
    unsigned value;

    operator unsigned() const {
        return value;
    }
};

verified unsigned probe(Wrapped wrapped)
    ensures (result == result)
{
    return wrapped;
}

int main() { return probe(Wrapped{2u}) == 2u ? 0 : 1; }
