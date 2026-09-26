// SPEC: TUBOUND-004, TEMPLATE-001
// A verified function template declared here and defined nowhere this unit
// can see. Its specialization clamp_to<4u> is only declared here, so its
// contract is never instantiated at 4u, and there is no statement to compare
// with any interface: it is refused rather than guessed.
template <unsigned N>
verified unsigned clamp_to(unsigned x)
    ensures (result < N);

verified unsigned use(unsigned y)
    ensures (result < 4u)
{
    return clamp_to<4u>(y);
}
