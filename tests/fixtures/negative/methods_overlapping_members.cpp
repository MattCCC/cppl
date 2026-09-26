// SPEC: CLASS-010, CLASS-015
//
// Members whose storage may overlap another place, or change unseen, have no
// place of the receiver: a reference member, which designates storage outside
// the object that may be another member of it; a bit-field, which shares a
// memory location with its neighbours; a member of an anonymous union, which
// shares storage with the union's other members; a member of an anonymous
// struct, which this implementation does not follow; and a volatile member.
// Each is refused where a verified body names it, rather than kept as a place
// of its own whose facts a write elsewhere could take away.
struct Holder {
    unsigned value;
    unsigned& other;

    verified unsigned through_reference()
        expects (value == 1u)
        ensures (result == 1u)
    {
        other = 5u;
        return value;
    }
};

struct Flags {
    unsigned low : 4;
    unsigned high : 4;

    verified unsigned read_low() const
        ensures (result == result)
    {
        return low;
    }
};

struct Word {
    union {
        unsigned whole;
        unsigned alias;
    };

    verified unsigned through_union()
        expects (whole == 1u)
        ensures (result == 1u)
    {
        alias = 5u;
        return whole;
    }
};

struct Split {
    struct {
        unsigned lo;
        unsigned hi;
    };

    verified unsigned through_anonymous_struct()
        ensures (result == result)
    {
        hi = 5u;
        return lo;
    }
};

struct Device {
    volatile unsigned status;

    verified void clear()
        ensures (true)
    {
        status = 0u;
    }
};

int main() {
    return 0;
}
