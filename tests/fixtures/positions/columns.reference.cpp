// Ordinary C++: `columns.cpp` with its C++L constructs erased by hand, as
// SPEC.md Annex M says they erase, each replaced by as many spaces or lowered to
// its alias padded to the same length, so every line and column stays where it
// was. The preprocessor writes a run of spaces between two tokens as one, which
// is the one difference written out here: `spaced` reads as the program Clang
// compiles has it (COMPATIBILITY.md 13).
//
#include <cstdio>

template <unsigned P, unsigned E>
         unsigned at(unsigned y)
                         
{
    return P;
}

                                      unsigned after_law() { return at<__builtin_COLUMN(), 72u>(0u); }

                                                        unsigned after_proof() { return at<__builtin_COLUMN(), 92u>(0u); }

using Small = unsigned;                   unsigned after_refinement() { return at<__builtin_COLUMN(), 83u>(0u); }

using Big = unsigned;
                   unsigned after_lines() { return at<__builtin_COLUMN(), 55u>(0u); }

using Positive = int; [[maybe_unused]] static inline bool __cppl_v_Positive(int self) { return static_cast<bool>(self > 0); }

         int pick(int raw)
                        
{
    if (__cppl_v_Positive (raw)) { return raw; } return at<__builtin_COLUMN(), 60u>(1u) - 59; } unsigned after_body() { return at<__builtin_COLUMN(), 131u>(0u); }

         unsigned counted(unsigned n)
                     
                           
{
    unsigned i = 0u;
    while (i < n)                    { i = i + 1u; }                       return at<__builtin_COLUMN(), 86u>(0u);
}

         unsigned spaced(unsigned x)
                         
{ return x; } unsigned after_spaced() { return at<__builtin_COLUMN(), 51u>(0u); }

template unsigned at<1u, 1u>(unsigned);
unsigned after_instantiation() { return at<__builtin_LINE(), 46u>(0u); }

int main() {
    std::printf("%u %u %u %u %d %u %u %u %u %u\n", after_law(), after_proof(), after_refinement(), after_lines(),
                pick(3), after_body(), counted(3u), spaced(4u), after_spaced(), after_instantiation());
}
