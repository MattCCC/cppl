// SPEC: CLASS-015
// A destructor the class provides runs where an object's lifetime ends: at the
// end of a block, of a loop body or of the function, where no statement names
// it. What it does is not modeled, so a type with one is not modeled in a
// verified body. Each function below would otherwise be PROVEN and false when
// called as `set(g)` or `kept(g)`: the destructor writes the storage the
// reference parameter designates. Accepted twin: a defaulted destructor, as in
// `refinements.cpp` and `structural_cases.cpp`, declares nothing that runs.
int g = 0;

struct Guard {
    int unused;
    ~Guard() { g = -7; }
};

template <class T>
struct Holder {
    T unused;
    ~Holder() { g = -7; }
};

verified void set(int& out)
    ensures (out == 5)
{
    Guard guard{0};
    out = 5;
}

verified int kept(int& v)
    expects (v > 0)
    ensures (result > 0)
{
    {
        Guard guard{0};
    }
    return v;
}

verified void set_templated(int& out)
    ensures (out == 5)
{
    Holder<int> holder{0};
    out = 5;
}

int main() {
    return 0;
}
