// SPEC: CASE-004
// The refused half of a matched pair whose accepted half is `variant_of_pointer`
// in `fixtures/structural_cases.cpp`. The pointer inside the alternative has two
// states however it was reached, and the inner `cases` names only one.
#include <variant>

struct Left {
    int l;
};

struct Right {
    bool r;
};

proof inner_non_null_missing(std::variant<const Left*, Right> v)
    proves (Eq<bool>(true, true))
{
    cases v {
        alternative<0>(pointer) => {
            cases pointer {
                null => {
                    refl;
                }
            }
        }

        alternative<1>(flag) => {
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
