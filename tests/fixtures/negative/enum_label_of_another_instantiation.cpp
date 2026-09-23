// SPEC: CASE-002
// The refused half of a matched pair whose accepted half is
// `member_enum_of_template` in `fixtures/enum_cases.cpp`. `Machine<long>::Mode`
// is a different enumeration from `Machine<int>::Mode`, even though its
// enumerators are spelled and valued the same, so its label names no case of
// this subject.
template <typename T> struct Machine {
    enum class Mode : unsigned { off = 0u, on = 1u };
};

proof label_of_another_instantiation(Machine<int>::Mode m)
    proves (Eq<bool>(true, true))
{
    cases m {
        Machine<long>::Mode::off => {
            refl;
        }

        Machine<int>::Mode::on => {
            refl;
        }

        unnamed(value) => {
            refl;
        }
    }
}

int main() {
    return 0;
}
