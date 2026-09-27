// SPEC: CONSTRUCT-136
// RFC 0022: the refused twin of subset/x136_deduction_guide.cpp (deduction guide), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

template <typename T>
struct Box {
    T value;
};

template <typename T>
Box(T) -> Box<T>;

verified unsigned probe(Box<unsigned> box)
    ensures (result == box.value)
{
    return box.value + 1u;
}

int main() { return probe(Box{2u}) == 2u ? 0 : 1; }
