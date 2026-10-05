// SPEC: CONSTRUCT-150
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// multiple inheritance, is refused.

struct Left {
    unsigned left;
};
struct Right {
    unsigned right;
};
struct Both : Left, Right {
    verified unsigned get_left() const
        ensures (result == result)
    {
        return left;
    }
};

int main() {
    const Both both{{2u}, {3u}};
    return both.get_left() == 2u ? 0 : 1;
}
