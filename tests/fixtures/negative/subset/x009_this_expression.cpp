// SPEC: CONSTRUCT-009
// RFC 0022: the refused twin of subset/x009_this_expression.cpp (`this` expression), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

struct Box {
    unsigned value;

    verified unsigned get() const
        ensures (result == value)
    {
        return this->value + 1u;
    }
};

int main() {
    const Box box{2u};
    return box.get() == 2u ? 0 : 1;
}
