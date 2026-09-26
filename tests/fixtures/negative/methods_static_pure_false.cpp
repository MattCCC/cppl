// SPEC: CLASS-012
//
// A static member function marked pure is a definition a contract may use, and
// what it defines is what its body computes: `twice(x) + 1u` is not `twice(x)`.
// The accepted twin states the value: `scaled` in
// `fixtures/verified_methods.cpp`.
struct Scale {
    static pure unsigned twice(unsigned x) {
        return x + x;
    }
};

verified unsigned wrong(unsigned x)
    ensures (result == Scale::twice(x) + 1u)
{
    return Scale::twice(x);
}

int main() {
    return 0;
}
