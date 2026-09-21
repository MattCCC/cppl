#!/usr/bin/env bash
# Refinement types that must be refused (SPEC.md 17).
#
# A refinement declaration asserts nothing. Every value that enters the type owes
# a proof of its predicate, and nothing here is accepted on the strength of the
# declaration. The contextual words the syntax uses keep their ordinary C++
# meaning everywhere else.
set -euo pipefail
CPPL="$1"
WORK="$3"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/refinement-negative.XXXXXX")

reject() {
    local name="$1"
    cat > "$run/$name.cpp"
    echo 'int main() { return 0; }' >> "$run/$name.cpp"
    if "$CPPL" -std=c++17 "$run/$name.cpp" -o "$run/$name" > "$run/$name.log" 2>&1; then
        echo "an invalid refinement was accepted: $name" >&2
        cat "$run/$name.log" >&2
        exit 1
    fi
    test ! -e "$run/$name"
    ! grep -q PROVEN "$run/$name.log"
    grep -q error "$run/$name.log"
}

accept() {
    local name="$1"
    cat > "$run/$name.cpp"
    if ! "$CPPL" -std=c++17 "$run/$name.cpp" -o "$run/$name" > "$run/$name.log" 2>&1; then
        echo "valid source was refused: $name" >&2
        cat "$run/$name.log" >&2
        exit 1
    fi
    "$run/$name"
}

# Nothing bounds the value, so its membership is unproven. It is not assumed.
reject unproven_introduction <<'CPP'
type Percentage = int where(self >= 0 && self <= 100);
verified int wrong(int x) ensures(result == x) {
    Percentage p = x;
    return p;
}
CPP

# A value that provably fails the predicate.
reject false_introduction <<'CPP'
type Small = unsigned where(self < 10u);
verified unsigned wrong(unsigned x) ensures(result == 20u) {
    Small s = 20u;
    return s;
}
CPP

# The predicate a refinement inherits is not discarded: 5 is below 10 and not
# below 3.
reject inherited_predicate <<'CPP'
type Small = unsigned where(self < 10u);
type Tiny = Small where(self < 3u);
verified unsigned wrong(unsigned x) ensures(result == 5u) {
    Tiny t = 5u;
    return t;
}
CPP

reject inherited_predicate_through_alias <<'CPP'
type Positive = int where(self > 0);
using Base = Positive;
type Bounded = Base where(self <= 100);
verified int wrong() ensures(result == 0) {
    Bounded value = 0;
    return value;
}
CPP
reject ordinary_alias_cannot_drop_membership <<'CPP'
type Positive = int where(self > 0);
typedef Positive First;
using Second = First;
verified int wrong() ensures(result == 0) {
    Second value = 0;
    return value;
}
CPP
reject indexed_alias_cannot_drop_membership <<'CPP'
type Index(unsigned n) = unsigned where(self < n);
using Four = Index<4>;
verified unsigned wrong() ensures(result == 4u) {
    Four value = 4u;
    return value;
}
CPP
reject same_spelling_does_not_select_another_refinement <<'CPP'
namespace first { type Range = unsigned where(self < 10u); }
namespace second { type Range = unsigned where(self < 3u); }
verified unsigned wrong() ensures(result == 5u) {
    second::Range value = 5u;
    return value;
}
CPP

# A refined result must hold on the path that returns.
reject false_refined_result <<'CPP'
type Small = unsigned where(self < 10u);
verified Small wrong(unsigned x) ensures(result == 20u) {
    return 20u;
}
CPP
reject ordinary_refined_return <<'CPP'
type Positive = int where(self > 0);
Positive manufacture();
CPP
reject ordinary_refined_return_definition <<'CPP'
type Positive = int where(self > 0);
Positive manufacture() { return 0; }
CPP
reject pure_does_not_prove_refined_return <<'CPP'
type Positive = int where(self > 0);
pure Positive manufacture() { return 0; }
CPP
reject ordinary_refined_reference_return <<'CPP'
type Positive = int where(self > 0);
Positive& manufacture();
CPP
reject ordinary_refined_reference_alias_return <<'CPP'
type Positive = int where(self > 0);
using Reference = Positive&;
Reference manufacture();
CPP
reject unverified_refined_storage <<'CPP'
type Positive = int where(self > 0);
int wrong() { Positive value = 0; return value; }
CPP
reject unchecked_refined_member <<'CPP'
type Positive = int where(self > 0);
struct S { Positive value; };
CPP
reject unchecked_refined_array <<'CPP'
type Positive = int where(self > 0);
Positive values[2] = {0, 0};
CPP
reject implicit_refined_postcondition <<'CPP'
type Positive = int where(self > 0);
verified Positive wrong() { return 0; }
CPP
reject implicit_refined_postcondition_on_every_path <<'CPP'
type Positive = int where(self > 0);
verified Positive wrong(bool b) {
    if (b) return 1;
    return 0;
}
CPP

# A branch that does not establish the predicate does not discharge it.
reject wrong_branch_fact <<'CPP'
type NonNegative = int where(self >= 0);
verified int wrong(int x) ensures(result == x) {
    if (x <= 0) {
        NonNegative n = x;
        return n;
    }
    return x;
}
CPP

# An index the value does not satisfy.
reject index_out_of_range <<'CPP'
type Index(unsigned n) = unsigned where(self < n);
verified unsigned wrong(unsigned x) ensures(result == 9u) {
    Index<8> i = 9u;
    return i;
}
CPP

# An assignment into a refined local owes the predicate exactly as the
# declaration did. A write is not a way around the obligation.
reject unproven_assignment <<'CPP'
type NonNegative = int where(self >= 0);
verified int wrong(int x) ensures(result == x) {
    NonNegative n = 0;
    n = x;
    return n;
}
CPP

# An update is the assignment it means, so it owes the predicate too.
reject unproven_update <<'CPP'
type Small = unsigned where(self < 10u);
verified unsigned wrong(unsigned x) ensures(result == 20u) {
    Small s = 0u;
    s += 20u;
    return s;
}
CPP

# The stricter direction of the subset relation needs the implication proven.
# `NonNegative` does not imply `Percentage`.
reject narrowing_without_proof <<'CPP'
type NonNegative = int where(self >= 0);
type Percentage = NonNegative where(self <= 100);
verified int wrong(NonNegative n) ensures(result == n) {
    Percentage p = n;
    return p;
}
CPP

# Two refinements of one base type erase to the same C++ signature, so two
# overloads distinguished only by them are one function. That is reported where
# the author wrote it, not discovered later in the emitted program.
reject erased_overload_collision <<'CPP'
type NonNegative = int where(self >= 0);
type Percentage = NonNegative where(self <= 100);
int f(NonNegative x) { return x; }
int f(Percentage x) { return x; }
CPP

# A refined parameter supposes its own predicate, not a stronger one.
reject stronger_than_the_parameter <<'CPP'
type NonNegative = int where(self >= 0);
verified int wrong(NonNegative n) ensures(result >= 1) {
    return n;
}
CPP

# Loop verification and callers of partial contracts use the same membership
# checks as straight-line paths, including values that are never read.
reject loop_does_not_skip_introduction <<'CPP'
type Small = unsigned where(self < 10u);
verified unsigned wrong(unsigned n) ensures(result == 0u) {
    Small bad = 20u;
    unsigned i = 0u;
    while (i < n) invariant(i <= n) { ++i; }
    return 0u;
}
CPP
reject loop_does_not_skip_update <<'CPP'
type Small = unsigned where(self < 10u);
verified unsigned wrong(unsigned n) ensures(result == 0u) {
    Small bad = 0u;
    unsigned i = 0u;
    while (i < n) invariant(i <= n) {
        bad = 20u;
        ++i;
    }
    return 0u;
}
CPP
reject partial_call_does_not_skip_introduction <<'CPP'
type Small = unsigned where(self < 10u);
verified unsigned count(unsigned n) ensures(result == n) {
    unsigned i = 0u;
    while (i < n) invariant(i <= n) { ++i; }
    return i;
}
verified unsigned wrong(unsigned n) ensures(result == n) {
    Small bad = 20u;
    return count(n);
}
CPP

# Malformed declarations, each reported as what it is.
reject no_base_type <<'CPP'
type Positive = where(self > 0);
CPP
reject no_predicate <<'CPP'
type Positive = int where();
CPP
reject empty_index_list <<'CPP'
type Positive() = int where(self > 0);
CPP
reject unterminated_predicate <<'CPP'
type Positive = int where(self > 0;
CPP
reject missing_semicolon <<'CPP'
type Positive = int where(self > 0)
CPP

# A predicate outside the modeled fragment is refused, not approximated.
reject unmodeled_base <<'CPP'
type Odd = double where(self > 0.0);
CPP
reject effectful_predicate <<'CPP'
int counter = 0;
int bump() { return ++counter; }
type Odd = int where(self > bump());
CPP

# A refinement declared where this implementation does not recognize one.
reject inside_a_function <<'CPP'
int f() {
    type Positive = int where(self > 0);
    return 0;
}
CPP

grep -q 'not shown to satisfy refinement type' "$run/unproven_introduction.log"
grep -q 'ordinary function.*return cannot establish refinement' "$run/ordinary_refined_return.log"
grep -q 'ordinary function.*return cannot establish refinement' "$run/ordinary_refined_return_definition.log"
grep -q 'ordinary function.*return cannot establish refinement' "$run/pure_does_not_prove_refined_return.log"
grep -q 'ordinary function.*return cannot establish refinement' "$run/ordinary_refined_reference_return.log"
grep -q 'not shown to satisfy refinement type' "$run/index_out_of_range.log"
grep -q 'not shown to satisfy refinement type' "$run/unproven_assignment.log"
grep -q 'not shown to satisfy refinement type' "$run/unproven_update.log"
grep -q 'not shown to satisfy refinement type' "$run/narrowing_without_proof.log"
grep -q 'not shown to satisfy refinement type' "$run/loop_does_not_skip_introduction.log"
grep -q 'not shown to satisfy refinement type' "$run/loop_does_not_skip_update.log"
grep -q 'not shown to satisfy refinement type' "$run/partial_call_does_not_skip_introduction.log"
grep -q "redefinition of 'f'" "$run/erased_overload_collision.log"
grep -q 'declares no base type' "$run/no_base_type.log"
grep -q 'states no predicate' "$run/no_predicate.log"
grep -q 'empty index list' "$run/empty_index_list.log"
grep -q "ends with ';'" "$run/missing_semicolon.log"
grep -q 'outside namespace scope' "$run/inside_a_function.log"

# `type` and `where` are contextual (SPEC.md 3): anything that is not the complete
# declaration form is ordinary C++ and keeps its own meaning.
accept type_is_a_member <<'CPP'
struct S {
    int type;
};
int main() {
    S s{0};
    return s.type;
}
CPP
accept type_is_an_alias <<'CPP'
using type = int;
int main() {
    type value = 0;
    return value;
}
CPP
accept where_is_a_function <<'CPP'
int where(int x) {
    return x;
}
int main() {
    return where(0);
}
CPP
accept type_returned_by_a_function <<'CPP'
using type = int;
type f(int x) {
    return x;
}
int main() {
    return f(0);
}
CPP
accept where_inside_a_base_type <<'CPP'
template <class T>
struct where {
    T value;
};
int main() {
    where<int> w{0};
    return w.value;
}
CPP

accept ordinary_alias_same_spelling_is_not_refined <<'CPP'
namespace first { type Positive = int where(self > 0); }
namespace second { using Positive = int; }
verified int zero() ensures(result == 0) {
    second::Positive value = 0;
    return value;
}
int main() { return zero(); }
CPP
accept indexed_alias_preserves_arguments <<'CPP'
type Index(unsigned n) = unsigned where(self < n);
using Four = Index<4>;
using Again = Four;
verified unsigned three() ensures(result == 3u) {
    Again value = 3u;
    return value;
}
int main() { return three() == 3u ? 0 : 1; }
CPP
accept nested_alias_preserves_all_predicates <<'CPP'
type Positive = int where(self > 0);
using Base = Positive;
type Bounded = Base where(self <= 100);
verified int identity(Bounded value) ensures(result > 0 && result <= 100) {
    return value;
}
int main() { return identity(1) == 1 ? 0 : 1; }
CPP
accept refined_return_is_a_postcondition <<'CPP'
type Positive = int where(self > 0);
verified Positive one() { return 1; }
verified Positive choose(bool b) {
    if (b) return 1;
    return 2;
}
verified Positive from_path(int x) expects(x > 0) { return x; }
verified int caller() ensures(result > 0) { return one(); }
int main() { return caller() == 1 && choose(false) == 2 && from_path(3) == 3 ? 0 : 1; }
CPP

echo "refinement membership is proven or refused; contextual words keep their C++ meaning"
