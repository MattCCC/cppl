// SPEC: CONSTRUCT-121
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// union declaration, is refused.

union Word {
    unsigned whole;
    unsigned short halves[2];

    verified unsigned get() const
        ensures (result == result)
    {
        return whole;
    }
};

int main() {
    Word word{};
    word.whole = 2u;
    return word.get() == 2u ? 0 : 1;
}
