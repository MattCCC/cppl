// SPEC: CONSTRUCT-059
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// virtual call, is refused.

struct Shape {
    virtual unsigned sides() const {
        return 0u;
    }
    virtual ~Shape() = default;
};

verified unsigned probe(const Shape& shape)
    ensures (result == result)
{
    return shape.sides();
}

int main() {
    const Shape shape;
    return probe(shape) == 0u ? 0 : 1;
}
