// SPEC: CLASS-008, CLASS-015
// What a verified member function cannot name: `this` as a value, a member
// whose type is not modeled, and a function reached through a pointer to
// member. Each is refused where it is written rather than modeled as something
// it is not.
struct Base {
    unsigned value;
};

struct Derived : Base {};

struct Cursor {
    Derived origin;
    unsigned pos;

    unsigned get() const {
        return pos;
    }

    verified unsigned same(const Cursor& other) const
        ensures (result == result)
    {
        if (this == &other) {
            return 1u;
        }
        return 0u;
    }

    // `origin` has a base subobject, so it is storage the receiver does not
    // track, and a read of a member of it is refused rather than guessed.
    verified unsigned origin_value() const
        ensures (result == result)
    {
        return origin.value;
    }

    verified unsigned through() const
        ensures (result == pos)
    {
        return (this->*getter)();
    }

    static constexpr unsigned (Cursor::*getter)() const = &Cursor::get;
};

int main() {
    return 0;
}
