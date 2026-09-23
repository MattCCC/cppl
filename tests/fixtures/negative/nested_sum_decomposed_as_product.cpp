// SPEC: CASE-003, CASE-007
// The refused half of a matched pair whose accepted half is `record_of_sums` in
// `fixtures/structural_cases.cpp`. A record's component that is a variant is a
// sum wherever it sits: `decompose` exposes the parts a value always has, and a
// variant has no such parts, only alternatives to choose between.
#include <optional>
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

struct Tagged {
    std::variant<Left, Right> choice;
    std::optional<Other> extra;
};

proof sum_decomposed(Tagged t)
    proves (Eq<bool>(true, true))
{
    decompose t {
        components(choice, extra) => {
            decompose choice {
                components(left, right) => {
                    refl;
                }
            }
        }
    }
}

int main() {
    return 0;
}
