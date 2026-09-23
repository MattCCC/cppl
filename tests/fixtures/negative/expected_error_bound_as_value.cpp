// SPEC: CASE-006
// The refused half of a matched pair whose accepted half is `tuple_of_expected`
// in `fixtures/expected_cases.cpp`. The only difference is that the error
// payload, a `Fault`, is handed to the lemma for the value payload's `Left`. An
// `error` arm that bound the value instead would be accepted here.
#include <expected>
#include <tuple>

struct Left {
    int l;
};

struct Right {
    bool r;
};

struct Fault {
    unsigned code;
};

proof is_left(Left x)
    proves (Eq<bool>(true, true))
{
    refl;
}

proof error_as_value(std::tuple<std::expected<Left, Fault>, Right> t)
    proves (Eq<bool>(true, true))
{
    decompose t {
        components(outcome, flag) => {
            cases outcome {
                value(good) => {
                    exact is_left(good);
                }

                error(bad) => {
                    exact is_left(bad);
                }
            }
        }
    }
}

int main() {
    return 0;
}
