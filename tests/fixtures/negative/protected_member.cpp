// SPEC: CASE-007
// The refused half of a matched pair whose accepted half is `class_fields` in
// `fixtures/structural_cases.cpp`. The two classes differ only in the access of
// their second member. A protected member is as inaccessible to a proof outside
// the class as a private one, so it is not a component to expose.
struct Left {
    int l;
};

struct Right {
    bool r;
};

class Guarded {
  public:
    Left first;

  protected:
    Right second;
};

proof protected_component(Guarded g)
    proves (Eq<bool>(true, true))
{
    decompose g {
        components(first, second) => {
            refl;
        }
    }
}

int main() {
    return 0;
}
