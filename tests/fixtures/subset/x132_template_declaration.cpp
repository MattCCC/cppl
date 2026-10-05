// SPEC: CONSTRUCT-132
// RFC 0022, the V1 verified subset: a verified body may use this construct, template declaration,
// and it is modeled. Its refused twin is negative/subset/x132_template_declaration.cpp.

template <typename T>
verified T probe(T x)
    ensures (result == x)
{
    return x;
}

int main() {
    return probe(2u) == 2u ? 0 : 1;
}
