// SPEC: CONSTRUCT-146
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// virtual function, is refused.

struct Shape {
    virtual ~Shape() = default;

    verified virtual unsigned sides() const
        ensures (result == 0u)
    {
        return 0u;
    }
};

int main() {
    const Shape shape;
    return shape.sides() == 0u ? 0 : 1;
}
