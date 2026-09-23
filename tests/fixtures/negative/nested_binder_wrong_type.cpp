// SPEC: CASE-006
// The refused half of a matched pair whose accepted half is `variant_of_tuple` in
// `fixtures/structural_cases.cpp`. The only difference is that the tuple's second
// component, a `Right`, is handed to the lemma for `Left`. A binder that denoted
// the first component instead would be accepted here.
#include <tuple>
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

proof is_left(Left x)
    proves (Eq<bool>(true, true))
{
    refl;
}

proof is_other(Other x)
    proves (Eq<bool>(true, true))
{
    refl;
}

proof second_as_first(std::variant<std::tuple<Left, Right>, Other> v)
    proves (Eq<bool>(true, true))
{
    cases v {
        alternative<0>(pair) => {
            decompose pair {
                components(first, second) => {
                    exact is_left(second);
                }
            }
        }

        alternative<1>(single) => {
            exact is_other(single);
        }

        valueless => {
            refl;
        }
    }
}

int main() {
    return 0;
}
