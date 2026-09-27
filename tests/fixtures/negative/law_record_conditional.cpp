// A conditional choosing between two records has no integer value, and the
// core's selection is an integer primitive. It is refused by name; it once
// aborted the compiler, reading the record's type as an integer's.
struct Pair {
    int first;
    int second;
};

law picked(Pair p, Pair q, unsigned c)
    proves (Eq<Pair>(c < 1u ? p : q, c < 1u ? p : q));

int main() {
    return 0;
}
