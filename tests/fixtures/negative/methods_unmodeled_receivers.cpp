// SPEC: CLASS-015
// A member function is verified with its implicit object only where that
// object's storage is modeled: a union's active member, and a base subobject's
// storage and dispatch, are not. Each is refused by name.
union Word {
    unsigned whole;
    unsigned short half;

    verified unsigned get() const
        ensures (result == whole)
    {
        return whole;
    }
};

struct Base {
    unsigned value;
};

struct Derived : Base {
    unsigned extra;

    verified unsigned get() const
        ensures (result == extra)
    {
        return extra;
    }
};

int main() {
    return 0;
}
