// The words of `contradiction` and of a case omission stay ordinary names
// inside C++L too, wherever the proof grammar does not put them (SPEC.md
// WORD-002, WORD-010).
//
// An enumeration is named `omit`, so a case label begins with the word itself:
// `omit::running => { ... }` is an arm, not an omission, because no label
// followed by `by` comes after `omit` there. The subject is a parameter named
// `by`, and the premise that discharges each omission is named
// `contradiction`, so `omit omit::idle by contradiction contradiction;` uses
// every word both as a keyword and as a name in one statement. `main` then uses
// all three as ordinary C++: a local hiding the enumeration, a declaration
// spelled like the `contradiction` statement, and a call.
#include <cstdio>

enum class omit : int { idle = -1, running = 3 };

struct contradiction {
    int by = 2;
};

int by(int value) {
    return value + 1;
}

law named_like_the_words(omit by)
    expects (by == omit::running)
    proves (Eq<bool>(by == omit::running, true));

proof named_like_the_words_holds(omit by)
    proves (named_like_the_words(by))
{
    assume contradiction : by == omit::running;
    cases by {
        omit omit::idle by contradiction contradiction;

        omit::running => {
            rewrite contradiction;
            refl;
        }

        omit unnamed by contradiction contradiction;
    }
}

int main() {
    int omit = 1;
    contradiction verdict;
    omit = by(omit) + verdict.by;
    std::printf("%d\n", omit);
}
