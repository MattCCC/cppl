// An omission is a claim that the case cannot occur, not that its goal can be
// proved (SPEC.md CASE-004, CASE-005).
//
// The goal here holds everywhere, so any case of `pointer` could close it with
// `refl`. The premise `pointer == nullptr` agrees with `null`, so `null` is
// perfectly possible, and omitting it claims something false. An
// implementation that asks whether the omitted case's GOAL follows, rather
// than whether the case itself is contradictory, accepts this: the goal
// follows from anything. This is the shape that separates the two.
law omits_a_possible_case_with_an_easy_goal(int* pointer)
    expects (pointer == nullptr)
    proves (Eq<bool>(true, true))
{
    assume is_null : pointer == nullptr;

    cases pointer {
        non_null => {
            refl;
        }

        omit null by contradiction is_null;
    }
}

int main() {
    return 0;
}
