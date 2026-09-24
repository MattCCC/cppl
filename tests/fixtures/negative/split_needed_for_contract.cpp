// SPEC: CASE-018
// The refused half of a matched pair whose accepted half is `settled` in
// `fixtures/case_split.cpp`. The two differ only in the split: without it, the
// contract is a nonlinear claim over every value the precondition allows, which
// linear arithmetic does not establish. With it, each case's result is a
// constant.
enum class Mode : unsigned { idle = 0u, busy = 1u };

verified unsigned unsplit(Mode m)
    expects (static_cast<unsigned>(m) <= 1u)
    ensures (result * result == result)
{
    return static_cast<unsigned>(m);
}

int main() {
    return 0;
}
