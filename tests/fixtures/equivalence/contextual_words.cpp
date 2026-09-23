// Every C++L contextual word used as an ordinary C++ name, in a unit that also
// uses the same words as C++L, so erasure runs over both (SPEC.md WORD-001,
// WORD-002, WORD-005, WORD-008, WORD-010, WORD-011, 3.1).
//
// Erasure may remove only what the C++L grammar claimed. Erased, this unit must
// compile to exactly the code `contextual_words.reference.cpp` compiles to,
// where only the C++L constructs were removed and every name below was left as
// it is. A name mistaken for C++L would be blanked out of the program, and the
// two would differ.
#include <cstdio>

// The core words as variables. The parenthesized initializers are spelled like
// clauses on a declarator, `ensures(12)`, and are declarators all the same.
int law = 1, proof = 2, proves = 3, pure = 4, verified = 5, ghost = 6, unsafe = 7, trusted = 8;
int type(9), where(10), expects(11), ensures(12), decreases(13), invariant(14), forall(15), exists(16);

// Words with meaning only inside a specification, as members.
struct Words {
    int result;
    int old;
    int self;
    int readable;
    int writable;
    int size;
    int at;
    int contains;
};

// Proof-statement words, case-omission words and arm labels, as functions.
int refl(int x) {
    return x + 1;
}

int exact(int x) {
    return x + 2;
}

int apply(int x) {
    return x + 3;
}

int assume(int x) {
    return x + 4;
}

int rewrite(int x) {
    return x + 5;
}

int cases(int x) {
    return x + 6;
}

int decompose(int x) {
    return x + 7;
}

int induction(int x) {
    return x + 8;
}

int omit(int x) {
    return x + 9;
}

int by(int x) {
    return x + 10;
}

struct Labels {
    int unnamed;
    int alternative;
    int valueless;
    int some;
    int none;
    int value;
    int error;
    int null;
    int non_null;
    int components;
    int zero;
    int successor;
};

// `contradiction` names a type, so the unit claims no impossible path and the
// statement in `main` declares a local (WORD-011).
struct contradiction {
    int reason = 17;
};

// The same words in their C++L meaning, beside the names above.
pure unsigned identity(unsigned x) {
    return x;
}

law identity_holds(unsigned x)
    proves (identity(x) == x);

proof identity_holds_proof(unsigned x)
    proves (identity_holds(x))
{
    refl;
}

type Small = unsigned where (self < 10u);

// Parameters and locals of a verified function named like contract words: in a
// contract `self` and `old` are ordinary parameters, and `result` the result.
verified unsigned named_like_words(unsigned self, unsigned old)
    expects ((self < 100u) && (old < 100u))
    ensures (result == self + old)
{
    unsigned invariant = self;
    unsigned decreases = old;
    return invariant + decreases;
}

verified unsigned counts_with_words(unsigned n)
    ensures (result == n)
{
    unsigned result = 0u;
    while (result < n)
        invariant (result <= n)
    {
        result = result + 1u;
    }
    return result;
}

verified Small small(unsigned x)
    expects (x < 10u)
{
    return x;
}

int main() {
    const Words words{1, 2, 3, 4, 5, 6, 7, 8};
    const Labels labels{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    contradiction verdict;
    const int core = law + proof + proves + pure + verified + ghost + unsafe + trusted + type + where + expects +
                     ensures + decreases + invariant + forall + exists;
    const int specification = words.result + words.old + words.self + words.readable + words.writable + words.size +
                              words.at + words.contains;
    const int statements = refl(0) + exact(0) + apply(0) + assume(0) + rewrite(0) + cases(0) + decompose(0) +
                           induction(0) + omit(0) + by(0);
    const int arms = labels.unnamed + labels.alternative + labels.valueless + labels.some + labels.none + labels.value +
                     labels.error + labels.null + labels.non_null + labels.components + labels.zero + labels.successor;
    std::printf("%d %d %d %d %d %u %u %u %u\n", core, specification, statements, arms, verdict.reason, identity(5u),
                named_like_words(3u, 4u), counts_with_words(6u), small(7u));
}
