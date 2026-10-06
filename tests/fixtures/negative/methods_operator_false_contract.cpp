// SPEC: CLASS-008, CONTRACT-009
// An operator function is verified over its own parameters, so a contract
// false of them is refused as any member function's is. The accepted twin is
// `methods_operator_functions` in negative/verified_methods.sh.
struct Adder {
    unsigned base;
    verified unsigned operator()(unsigned k) const
        expects (base < 100u && k < 100u)
        ensures (result == base + k + 1u)
    {
        return base + k;
    }
};

int main() {
    Adder add{1u};
    return add(3u) == 4u ? 0 : 1;
}
