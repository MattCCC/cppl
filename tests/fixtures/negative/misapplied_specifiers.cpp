// A specifier applies to a function declaration, and a verified function states
// its return type.
//
// The contract's 'result' is a value of the declared return type, so a function
// whose result type is deduced or unwritten leaves 'result' without one. Rather
// than infer a type for the thing a postcondition constrains, this
// implementation requires it to be written.
#include <iostream>

pure unsigned identity(unsigned x) { return x; }

// A specifier with no function declarator after it.
verified unsigned verified_without_declarator = 0u;

pure unsigned pure_without_declarator = 0u;

// The result type is deduced, so 'result' has no declared type.
verified auto deduced_result(unsigned x)
    ensures (result == x)
{
    return x;
}

int main() {
    std::cout << identity(41u) << "\n";
    return 0;
}
