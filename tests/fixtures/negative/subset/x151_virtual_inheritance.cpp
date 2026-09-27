// SPEC: CONSTRUCT-151
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// virtual inheritance, is refused.

struct Root {
    unsigned root;
};
struct Middle : virtual Root {
    verified unsigned get_root() const
        ensures (result == result)
    {
        return root;
    }
};

int main() {
    Middle middle;
    middle.root = 2u;
    return middle.get_root() == 2u ? 0 : 1;
}
