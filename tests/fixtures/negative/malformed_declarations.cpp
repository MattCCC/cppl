// A specification declaration that does not say what it claims is refused.
//
// A Law states a proposition and a verified function states a runtime contract.
// Confusing the two, or omitting the claim entirely, leaves nothing well-formed
// for the kernel to be given, so each is a syntax error rather than a
// declaration that quietly claims less than it appears to.
#include <iostream>

pure unsigned identity(unsigned x) { return x; }

// A Law concludes with 'proves': it has no runtime result to constrain.
law law_with_ensures(unsigned x)
    ensures (identity(x) == x);

// A Law has no measure; termination belongs to a runtime construct.
law law_with_decreases(unsigned x)
    decreases (x)
    proves (identity(x) == x);

// A Law that states no proposition claims nothing.
law law_without_proposition(unsigned x)
    expects (x > 0u);

// A Law concludes once: two conclusions would be two Laws.
law law_with_two_propositions(unsigned x)
    proves (identity(x) == x)
    proves (identity(x) == identity(x));

// A runtime postcondition is 'ensures': 'proves' states a proposition.
verified unsigned function_with_proves(unsigned x)
    proves (result == x)
{
    return x;
}

// A proof constructs evidence, so it has a body.
proof bodiless_proof(unsigned x)
    proves (law_without_proposition(x));

// A trusted Law is assumed, so it cannot also carry a proof of itself.
trusted law assumed_with_body(unsigned x)
    proves (identity(x) == x)
{
    refl;
}

int main() {
    std::cout << identity(41u) << "\n";
    return 0;
}
