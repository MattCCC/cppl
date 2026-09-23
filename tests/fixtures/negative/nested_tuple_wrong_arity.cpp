// SPEC: CASE-004
// The refused half of a matched pair whose accepted half is `variant_of_tuple` in
// `fixtures/structural_cases.cpp`. The payload is a two-component tuple, so an
// arm naming three components is refused however deep the tuple sits.
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

proof three_of_two(std::variant<std::tuple<Left, Right>, Other> v)
    proves (Eq<bool>(true, true))
{
    cases v {
        alternative<0>(pair) => {
            decompose pair {
                components(first, second, third) => {
                    refl;
                }
            }
        }

        alternative<1>(single) => {
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
