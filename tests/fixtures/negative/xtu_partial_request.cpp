// SPEC: TUBOUND-004, TUBOUND-007
// `count_down` without `decreases` asks for partial correctness only, and
// ranked.cpp proved a contract that asks for termination: two contracts.
verified unsigned count_down(unsigned n)
    ensures (result == 0u);

verified unsigned counted(unsigned n)
    ensures (result == 0u)
{
    return count_down(n);
}
