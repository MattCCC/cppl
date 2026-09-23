// Ordinary C++ that happens to use C++L contextual words as identifiers.
// None of this may become C++L syntax (SPEC.md 3.1, GRAMMAR.md 50).
//
// C++L adds no reserved words: every word below is contextual, so outside a
// grammatical C++L construct the ordinary C++ reading wins (SPEC.md 3.1).
// Each word is therefore exercised in the three shapes that could plausibly
// be mistaken for C++L: as a type name, as a variable, and as a callable.
//
// `ghost value;` is the sharpest of these. Its C++ reading (a variable of
// type `ghost`) and a hypothetical C++L ghost-local declaration are spelled
// identically, so it pins the precedence rule rather than any single
// construct. Resolving it needs the frontend's syntactic and semantic C++
// context, not a lexer -- which is why editors/shared/cppl.tmLanguage.json
// deliberately leaves it to the C++ reading.

int law = 1;

void proof() {}

struct ghost {};

int verified = 0;

int trusted = 0;

// Declaration modifiers used as ordinary declarators.
int pure = 0;

int type = 0;

// A contextual word naming a type, then shadowed by an ordinary identifier
// of the same spelling. Both readings must stay valid C++.
struct law_tag {};

int shadowing() {
    int ghost = 1;
    return ghost;
}

// Specification-clause words as ordinary identifiers.
int expects = 0;

int ensures = 0;

int invariant = 0;

int decreases = 0;

int where = 0;

// Quantifier words.
int forall = 0;

int exists = 0;

// Contract-scoped words are ordinary names outside their scopes
// (GRAMMAR.md 12, 13 and 15).
int result = 0;

int old = 0;

int self = 0;

// Proof-statement words.
int refl = 0;

int exact = 0;

int apply = 0;

int assume = 0;

int rewrite = 0;

int cases = 0;

int decompose = 0;

int induction = 0;

// The contradiction statement and the words of a case omission (GRAMMAR.md
// 5.6, 5.7). `contradiction verdict;` in `main` is spelled exactly like the
// proof statement `contradiction evidence;`, so like `ghost value;` it pins the
// precedence rule: outside a proof body it declares a variable of type
// `contradiction`.
struct contradiction {};

int omit = 0;

int by(int value) {
    return value;
}

int sum_of_contextual_names() {
    return pure + type + expects + ensures + invariant + decreases + where + forall + exists + result + old + self +
           refl + exact + apply + assume + rewrite + cases + decompose + induction + omit + by(0);
}

int main() {
    proof();
    ghost value;
    law_tag tag;
    contradiction verdict;
    (void)value;
    (void)tag;
    (void)verdict;
    (void)verified;
    (void)trusted;
    return law - 1 + sum_of_contextual_names() + shadowing() - 1;
}
