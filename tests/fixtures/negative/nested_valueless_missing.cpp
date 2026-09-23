// SPEC: CASE-004
// The refused half of a matched pair whose accepted half is `nested_variants` in
// `fixtures/structural_cases.cpp`. The inner variant has a `valueless` state of
// its own; the outer one's arm does not account for it.
#include <variant>

struct Left {
    int l;
};

struct Right {
    bool r;
};

struct Other {
    unsigned o;
};

proof inner_valueless_missing(std::variant<std::variant<Left, Right>, Other> v)
    proves (Eq<bool>(true, true))
{
    cases v {
        alternative<0>(inner) => {
            cases inner {
                alternative<0>(left) => {
                    refl;
                }

                alternative<1>(right) => {
                    refl;
                }
            }
        }

        alternative<1>(other) => {
            refl;
        }

        valueless => {
            refl;
        }
    }
}

int main() {
    return 0;
}
