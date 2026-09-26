// SPEC: CONTRACT-014, CLASS-006, CLASS-014
// A call through a base interface runs whichever override the dynamic type
// selects, so a contract proved from one body says nothing about the call.
// Override substitutability is not checked by this implementation, so a
// verified virtual function is refused where it is declared, however the
// declaration says it is virtual.
struct Base {
    unsigned value;

    verified virtual unsigned get() const
        ensures (result == value)
    {
        return value;
    }

    virtual ~Base() = default;
};

struct Derived : Base {
    verified unsigned get() const override
        ensures (result == 2u)
    {
        return 2u;
    }
};

struct Final : Base {
    verified unsigned get() const final
        ensures (result == 3u)
    {
        return 3u;
    }
};

int main() {
    return 0;
}
