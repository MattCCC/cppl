// A pointer is an abstract value to the formal core, with no machine equality
// or order it states. Comparing two of them in a Law is refused by name; it
// once aborted the compiler, reading the pointer's type as an integer's.
law same_pointer(int* p)
    proves (p == p);

int main() {
    return 0;
}
