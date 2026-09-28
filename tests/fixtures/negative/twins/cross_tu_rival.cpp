// Refused twin of fixtures/cross_tu/rival.cpp (tests/negative/refused_twins.sh): the same
// program, except that clamp4 claims a result below 1u.
// A unit that verifies on its own: it defines clamp4 and proves a different
// contract for it than library.cpp does. Its interface and library.cpp's both
// record the function, with different statements, so tests/negative/cross_tu.sh
// shows that imported together, neither is used (SPEC.md TUBOUND-009).
verified unsigned clamp4(unsigned x)
    expects (x < 100u)
    ensures (result < 1u)
{
    if (x < 5u) {
        return x;
    }
    return 4u;
}
