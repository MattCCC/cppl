// SPEC: CASE-002
// The refused half of a matched pair whose accepted half is `top_of_unsigned` in
// `fixtures/enum_cases.cpp`. `Top::top` is 4294967295 in C++, so claiming it
// differs from that literal is false. It must be refused because it is false,
// not because the enumerator reached the kernel as a value its type cannot hold.
enum class Top : unsigned { top = 0xFFFFFFFFu, low = 0u };

proof top_differs_from_its_value()
    proves (Eq<bool>(static_cast<unsigned>(Top::top) == 4294967295u, false))
{
    refl;
}

int main() {
    return 0;
}
