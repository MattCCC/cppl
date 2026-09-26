// A unit that verifies on its own: it defines clamp4 and proves a different
// contract for it than library.cpp does. Its interface and library.cpp's both
// record the function, with different statements, so tests/negative/cross_tu.sh
// shows that imported together, neither is used (SPEC.md TUBOUND-009).
verified unsigned clamp4(unsigned x)
    expects (x < 100u)
    ensures (result < 5u)
{
    if (x < 5u) {
        return x;
    }
    return 4u;
}
