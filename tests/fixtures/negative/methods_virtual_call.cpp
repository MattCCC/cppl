// SPEC: CLASS-006, COVERAGE-024, CLASS-014
// A virtual call dispatches on the object's dynamic type, so no one body's
// contract describes it: the call is refused rather than verified against the
// function the static type names. Qualifying the name selects one body, but
// the function is still one whose overrides are not checked, so that call is
// refused too.
struct Shape {
    unsigned sides;

    virtual unsigned count() const {
        return sides;
    }

    verified unsigned dispatched() const
        ensures (result == sides)
    {
        return count();
    }

    verified unsigned qualified() const
        ensures (result == sides)
    {
        return Shape::count();
    }

    virtual ~Shape() = default;
};

int main() {
    Shape shape;
    shape.sides = 3u;
    return static_cast<int>(shape.dispatched() + shape.qualified());
}
