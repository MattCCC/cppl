// SPEC: TUBOUND-004, TUBOUND-007
// library.cpp proved `count_up` partial; this declaration asks that it
// terminate, which no record states.
verified unsigned count_up(unsigned n)
    ensures (result == n)
    decreases (n);

verified unsigned counted(unsigned n)
    ensures (result == n)
{
    return count_up(n);
}
