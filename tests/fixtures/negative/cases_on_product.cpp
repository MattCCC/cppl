// SPEC: CASE-003
// The refused half of a matched pair whose accepted half is `class_fields` in
// `fixtures/structural_cases.cpp`. A product has one state, so it has no cases to
// split on; the same arm under `decompose` is accepted.
struct Left {
    int l;
};

struct Right {
    bool r;
};

class Open {
  public:
    Left first;
    Right second;
};

proof product_split_as_sum(Open o)
    proves (Eq<bool>(true, true))
{
    cases o {
        components(first, second) => {
            refl;
        }
    }
}

int main() {
    return 0;
}
