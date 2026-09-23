// Ordinary C++: `contextual_words.cpp` with only its C++L constructs erased, as
// SPEC.md Annex M says they erase. Every name spelled like a C++L word is left
// exactly as it was.
#include <cstdio>

int law = 1, proof = 2, proves = 3, pure = 4, verified = 5, ghost = 6, unsafe = 7, trusted = 8;
int type(9), where(10), expects(11), ensures(12), decreases(13), invariant(14), forall(15), exists(16);

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

struct contradiction {
    int reason = 17;
};

unsigned identity(unsigned x) {
    return x;
}

using Small = unsigned;

unsigned named_like_words(unsigned self, unsigned old) {
    unsigned invariant = self;
    unsigned decreases = old;
    return invariant + decreases;
}

unsigned counts_with_words(unsigned n) {
    unsigned result = 0u;
    while (result < n) {
        result = result + 1u;
    }
    return result;
}

Small small(unsigned x) {
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
