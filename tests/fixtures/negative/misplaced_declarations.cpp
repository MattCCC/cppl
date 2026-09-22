// Specification declarations are recognized at namespace scope only.
//
// A Law, proof, refinement type, pure function or verified function is a
// unit-level declaration, so that the trust report can name what it covers. One
// buried in a block would be a claim the report could not account for, so each
// is refused where it was written rather than being recognized and unreported.
#include <iostream>

pure unsigned identity(unsigned x) { return x; }

law outer(unsigned x)
    proves (identity(x) == x);

void block_scope() {
    // A Law inside a function body.
    law inner(unsigned x)
        proves (identity(x) == x);

    // A proof of a Law, likewise.
    proof outer_holds(unsigned x)
        proves (outer(x))
    {
        refl;
    }

    // A refinement type names the values its representation reserves.
    type Small =
        unsigned where (self < 10u);

    // A pure function is a definition the formal core may use.
    pure unsigned twice(unsigned x) { return x + x; }

    // A verified function carries an obligation discharged where it is defined.
    verified unsigned checked(unsigned x)
        ensures (result == x)
    {
        return x;
    }
}

// A trusted Law is an assumption, so the report must be able to name it.
void trusted_in_a_block() {
    trusted law assumed(unsigned x)
        proves (identity(x) == x);
}

// Class scope is not namespace scope either. The virtual case is the one that
// matters most: which body runs is decided by the dynamic type, so a proof of
// the base's body would not cover an override that replaces it. Refusing the
// specifier outright is what keeps a base's contract from becoming evidence
// about a call that dispatches elsewhere.
struct Base {
    verified virtual unsigned get() const
        ensures (result > 0u)
    {
        return 1u;
    }
    virtual ~Base() = default;
};

struct Derived : Base {
    verified unsigned get() const override
        ensures (result > 0u)
    {
        return 2u;
    }
};

int main() {
    std::cout << identity(41u) << "\n";
    return 0;
}
