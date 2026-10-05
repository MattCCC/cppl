// SPEC: CONSTRUCT-009
// RFC 0022, the V1 verified subset: a verified body may use this construct, `this` expression,
// and it is modeled. Its refused twin is negative/subset/x009_this_expression.cpp.

struct Box {
    unsigned value;

    verified unsigned get() const
        ensures (result == value)
    {
        return this->value;
    }
};

int main() {
    const Box box{2u};
    return box.get() == 2u ? 0 : 1;
}
