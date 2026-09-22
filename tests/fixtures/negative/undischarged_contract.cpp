// A contract is discharged from the body, so a verified function is verified
// where it is defined.
//
// A declaration alone states an obligation with nothing to discharge it. Taking
// the contract on the strength of the declaration would make it an assumption
// wearing the syntax of a proof; 'trusted law' is how an assumption is written,
// and it is reported as one.
#include <iostream>

pure unsigned identity(unsigned x) { return x; }

verified unsigned declared_only(unsigned x)
    ensures (result == x);

int main() {
    std::cout << identity(41u) << "\n";
    return 0;
}
