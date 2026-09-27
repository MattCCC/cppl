// SPEC: CONSTRUCT-147
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// pure virtual function, is refused.

struct Shape {
    virtual ~Shape() = default;

    verified virtual unsigned sides() const
        ensures (result >= 0u) = 0;
};

struct Square : Shape {
    unsigned sides() const override {
        return 4u;
    }
};

int main() {
    const Square square;
    return square.sides() == 4u ? 0 : 1;
}
