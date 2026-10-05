// SPEC: CONSTRUCT-136
// RFC 0022, the V1 verified subset: a verified body may use this construct, deduction guide,
// and it is modeled. Its refused twin is negative/subset/x136_deduction_guide.cpp.

template <typename T> struct Box {
    T value;
};

template <typename T> Box(T) -> Box<T>;

verified unsigned probe(Box<unsigned> box)
    ensures (result == box.value)
{
    return box.value;
}

int main() {
    return probe(Box{2u}) == 2u ? 0 : 1;
}
