// Refinement types: a verification-level type over an ordinary C++ base type
// (SPEC.md 17, 18; GRAMMAR.md 14, 16).
//
// Every declaration below lowers to the alias it means, so the program keeps the
// base type and nothing else. What the refinement adds is a predicate that must
// be proven wherever a value enters the type, and that is known wherever a value
// already has it.

#include <cstdio>

type NonNegative = int where(self >= 0);
type Percentage = NonNegative where(self <= 100);
type Small = unsigned where(self < 10u);
type Index(unsigned n) = unsigned where(self < n);
type Short(n) = unsigned where(self < n);

// A literal that satisfies the predicate enters the type.
verified int fifty(int x) ensures(result == 50) {
    Percentage p = 50;
    return p;
}

// A branch fact discharges the obligation: the value is known to satisfy the
// predicate where it enters the type, not where the type was declared.
verified int from_a_branch(int x) ensures(result >= 0) {
    if (x >= 0) {
        NonNegative n = x;
        return n;
    }
    return 0;
}

// A refined parameter is known to satisfy its predicate inside the body, so the
// postcondition follows without restating the predicate as an `expects` clause.
verified int keeps(NonNegative n) ensures(result >= 0) {
    return n;
}

// A refined result is proven on the path that returns it.
verified NonNegative zero(int x) ensures(result == 0) {
    return 0;
}

verified Percentage implicit_percentage() {
    return 50;
}

// A refinement of a refinement states both predicates: the value must satisfy
// the one written here and the one it inherits.
verified int composed(int x) ensures(result == 2) {
    Percentage p = 2;
    return p;
}

// An indexed refinement is applied at a value, and its predicate is stated at
// that value.
verified unsigned indexed(unsigned x) ensures(result == 3u) {
    Index<8> i = 3u;
    return i;
}

// An index written without a type takes the type being refined.
verified unsigned short_indexed(unsigned x) ensures(result == 1u) {
    Short<4> s = 1u;
    return s;
}

// An assignment is a value entering the local's declared type just as its
// declaration was, so the write owes the predicate and a branch fact discharges it.
verified int assigned(int x) ensures(result >= 0) {
    NonNegative n = 0;
    if (x >= 0) {
        n = x;
    }
    return n;
}

// An update is the assignment it means, so it owes the predicate as well.
verified unsigned updated(unsigned x) ensures(result == 1u) {
    Small s = 0u;
    s += 1u;
    return s;
}

// The looser direction of the subset relation: every `Percentage` is a
// `NonNegative`, because the predicate it carries implies that one's (SPEC.md
// 17.3.2). Nothing is checked at run time to cross it.
verified int widened(Percentage p) ensures(result >= 0) {
    NonNegative n = p;
    return n;
}

// A loop's current version is constrained by its invariant. Every update still
// owes membership, even when the local is not used in the postcondition.
verified unsigned refined_loop() ensures(result == 9u) {
    Small i = 0u;
    while (i < 9u) invariant(i <= 9u) {
        ++i;
    }
    return i;
}

// A refined value used as its base value needs no further proof (SPEC.md 17.3).
pure int identity(int x) {
    return x;
}
law refined_is_its_base(NonNegative n) ensures(identity(n) == n);

// `type` and `where` are contextual: a program that spells its own keeps it.
struct Holder {
    int type;
};
pure int where(int x) {
    return x;
}
using type = int;

int main() {
    if (implicit_percentage() != 50) return 1;
    if (refined_loop() != 9u) return 1;
    Holder holder{7};
    type ordinary = holder.type;
    std::printf("%d %d %d %u %u %d %u %d\n", fifty(0), from_a_branch(1), keeps(2), indexed(0u),
                where(ordinary) - 7u, assigned(4), updated(0u), widened(9));
}
