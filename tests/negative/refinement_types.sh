#!/usr/bin/env bash
# Refinement types that must be refused.
#
# SPEC: REFINE-008, REFINE-010, REFINEOBL-002, REFINEOBL-004, REFINEOBL-005,
# SPEC: REFINEOBL-007
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
type Percentage = int where (self >= 0 && self <= 100);
verified int wrong(int x) ensures (result == x) {
    Percentage p = x;
    return p;
}
CPP

# A value that provably fails the predicate.
reject false_introduction <<'CPP'
type Small = unsigned where (self < 10u);
verified unsigned wrong(unsigned x) ensures (result == 20u) {
    Small s = 20u;
    return s;
}
CPP

# The predicate a refinement inherits is not discarded: 5 is below 10 and not
# below 3.
reject inherited_predicate <<'CPP'
type Small = unsigned where (self < 10u);
type Tiny = Small where (self < 3u);
verified unsigned wrong(unsigned x) ensures (result == 5u) {
    Tiny t = 5u;
    return t;
}
CPP

reject inherited_predicate_through_alias <<'CPP'
type Positive = int where (self > 0);
using Base = Positive;
type Bounded = Base where (self <= 100);
verified int wrong() ensures (result == 0) {
    Bounded value = 0;
    return value;
}
CPP
reject ordinary_alias_cannot_drop_membership <<'CPP'
type Positive = int where (self > 0);
typedef Positive First;
using Second = First;
verified int wrong() ensures (result == 0) {
    Second value = 0;
    return value;
}
CPP
reject indexed_alias_cannot_drop_membership <<'CPP'
type Index(unsigned n) = unsigned where (self < n);
using Four = Index<4>;
verified unsigned wrong() ensures (result == 4u) {
    Four value = 4u;
    return value;
}
CPP
reject same_spelling_does_not_select_another_refinement <<'CPP'
namespace first { type Range = unsigned where (self < 10u); }
namespace second { type Range = unsigned where (self < 3u); }
verified unsigned wrong() ensures (result == 5u) {
    second::Range value = 5u;
    return value;
}
CPP

# A refined result must hold on the path that returns.
reject false_refined_result <<'CPP'
type Small = unsigned where (self < 10u);
verified Small wrong(unsigned x) ensures (result == 20u) {
    return 20u;
}
CPP
reject ordinary_refined_return <<'CPP'
type Positive = int where (self > 0);
Positive manufacture();
CPP
reject ordinary_refined_return_definition <<'CPP'
type Positive = int where (self > 0);
Positive manufacture() { return 0; }
CPP
reject pure_does_not_prove_refined_return <<'CPP'
type Positive = int where (self > 0);
pure Positive manufacture() { return 0; }
CPP
reject ordinary_refined_reference_return <<'CPP'
type Positive = int where (self > 0);
Positive& manufacture();
CPP
reject ordinary_refined_reference_alias_return <<'CPP'
type Positive = int where (self > 0);
using Reference = Positive&;
Reference manufacture();
CPP
reject unverified_refined_storage <<'CPP'
type Positive = int where (self > 0);
int wrong() { Positive value = 0; return value; }
CPP
# A record built outside a verified body establishes its members without any
# obligation, exactly as a refined variable would, so naming the refinement at
# that boundary is refused (SPEC.md 17.6).
reject unchecked_refined_member_storage <<'CPP'
type Positive = int where (self > 0);
struct S { Positive value; };
S global{0};
CPP
# Construction inside a verified body owes the member's predicate at the
# member's own place. The declaration is legal; this value is not.
reject refined_member_construction_is_checked <<'CPP'
type Positive = int where (self > 0);
struct S { Positive p; };
verified int f() ensures (result > 0) { S s{0}; return s.p; }
CPP
# A write to a member crosses into the member's declared type through the same
# write path as every other write.
reject refined_member_write_is_checked <<'CPP'
type Positive = int where (self > 0);
struct S { Positive p; };
verified int f() ensures (result > 0) { S s{1}; s.p = 0; return s.p; }
CPP
# A member does not borrow a sibling's predicate: proving `a` says nothing
# about `b`.
reject a_member_does_not_borrow_a_sibling_predicate <<'CPP'
type Positive = int where (self > 0);
struct S { Positive a; int b; };
verified int f() ensures (result > 0) { S s{1, 0}; return s.b; }
CPP
# A reference denotes the member's storage, so writing through it crosses into
# the member's declared type. The obligation is owed at the write, not deferred
# to the read (SPEC.md 17.6, Annex I REFINEOBL-007).
reject a_reference_write_cannot_bypass_a_member_predicate <<'CPP'
type Positive = int where (self > 0);
struct S { Positive x; };
verified int f() ensures (result > 0) {
    S s{3};
    int& r = s.x;
    r = 0;
    return s.x;
}
CPP
reject unchecked_refined_array <<'CPP'
type Positive = int where (self > 0);
Positive values[2] = {0, 0};
CPP
reject mutable_reference_cannot_bypass_membership <<'CPP'
type Positive = int where (self > 0);
verified int wrong() ensures (result == 0) {
    Positive x = 1;
    int& r = x;
    r = 0;
    return x;
}
CPP
reject refined_reference_does_not_keep_a_stale_fact <<'CPP'
type Positive = int where (self > 0);
verified int wrong() ensures (result > 0) {
    int x = 1;
    const Positive& r = x;
    x = 0;
    return r;
}
CPP
reject refined_reference_binding_requires_membership <<'CPP'
type Positive = int where (self > 0);
verified int wrong() ensures (result == 0) {
    int x = 0;
    const Positive& r = x;
    return x;
}
CPP
reject refined_reference_write_requires_membership <<'CPP'
type Positive = int where (self > 0);
verified int wrong() ensures (result == 0) {
    int x = 1;
    Positive& r = x;
    r = 0;
    return x;
}
CPP
reject reference_update_observes_current_version <<'CPP'
verified unsigned wrong() ensures (result == 2u) {
    unsigned x = 1u;
    unsigned& r = x;
    x = 8u;
    ++r;
    return x;
}
CPP
reject reference_to_a_temporary_is_refused <<'CPP'
type Positive = int where (self > 0);
verified int wrong() ensures (result > 0) {
    const Positive& r = 1 + 1;
    return r;
}
CPP
reject reference_to_parameter_tracks_mutation <<'CPP'
verified int wrong(int x) ensures (result == x) {
    int& r = x;
    r = 0;
    return r;
}
CPP
reject implicit_refined_postcondition <<'CPP'
type Positive = int where (self > 0);
verified Positive wrong() { return 0; }
CPP
reject implicit_refined_postcondition_on_every_path <<'CPP'
type Positive = int where (self > 0);
verified Positive wrong(bool b) {
    if (b) return 1;
    return 0;
}
CPP

# A branch that does not establish the predicate does not discharge it.
reject wrong_branch_fact <<'CPP'
type NonNegative = int where (self >= 0);
verified int wrong(int x) ensures (result == x) {
    if (x <= 0) {
        NonNegative n = x;
        return n;
    }
    return x;
}
CPP

# An index the value does not satisfy.
reject index_out_of_range <<'CPP'
type Index(unsigned n) = unsigned where (self < n);
verified unsigned wrong(unsigned x) ensures (result == 9u) {
    Index<8> i = 9u;
    return i;
}
CPP

# An assignment into a refined local owes the predicate exactly as the
# declaration did. A write is not a way around the obligation.
reject unproven_assignment <<'CPP'
type NonNegative = int where (self >= 0);
verified int wrong(int x) ensures (result == x) {
    NonNegative n = 0;
    n = x;
    return n;
}
CPP

# An update is the assignment it means, so it owes the predicate too.
reject unproven_update <<'CPP'
type Small = unsigned where (self < 10u);
verified unsigned wrong(unsigned x) ensures (result == 20u) {
    Small s = 0u;
    s += 20u;
    return s;
}
CPP

# The stricter direction of the subset relation needs the implication proven.
# `NonNegative` does not imply `Percentage`.
reject narrowing_without_proof <<'CPP'
type NonNegative = int where (self >= 0);
type Percentage = NonNegative where (self <= 100);
verified int wrong(NonNegative n) ensures (result == n) {
    Percentage p = n;
    return p;
}
CPP

# Two refinements of one base type erase to the same C++ signature, so two
# overloads distinguished only by them are one function. That is reported where
# the author wrote it, not discovered later in the emitted program.
reject erased_overload_collision <<'CPP'
type NonNegative = int where (self >= 0);
type Percentage = NonNegative where (self <= 100);
int f(NonNegative x) { return x; }
int f(Percentage x) { return x; }
CPP

# A refined parameter supposes its own predicate, not a stronger one.
reject stronger_than_the_parameter <<'CPP'
type NonNegative = int where (self >= 0);
verified int wrong(NonNegative n) ensures (result >= 1) {
    return n;
}
CPP

# Loop verification and callers of partial contracts use the same membership
# checks as straight-line paths, including values that are never read.
reject loop_does_not_skip_introduction <<'CPP'
type Small = unsigned where (self < 10u);
verified unsigned wrong(unsigned n) ensures (result == 0u) {
    Small bad = 20u;
    unsigned i = 0u;
    while (i < n) invariant (i <= n) { ++i; }
    return 0u;
}
CPP
reject loop_does_not_skip_update <<'CPP'
type Small = unsigned where (self < 10u);
verified unsigned wrong(unsigned n) ensures (result == 0u) {
    Small bad = 0u;
    unsigned i = 0u;
    while (i < n) invariant (i <= n) {
        bad = 20u;
        ++i;
    }
    return 0u;
}
CPP
reject partial_call_does_not_skip_introduction <<'CPP'
type Small = unsigned where (self < 10u);
verified unsigned count(unsigned n) ensures (result == n) {
    unsigned i = 0u;
    while (i < n) invariant (i <= n) { ++i; }
    return i;
}
verified unsigned wrong(unsigned n) ensures (result == n) {
    Small bad = 20u;
    return count(n);
}
CPP

# Malformed declarations, each reported as what it is.
reject no_base_type <<'CPP'
type Positive = where (self > 0);
CPP
reject no_predicate <<'CPP'
type Positive = int where ();
CPP
reject empty_index_list <<'CPP'
type Positive() = int where (self > 0);
CPP
reject unterminated_predicate <<'CPP'
type Positive = int where (self > 0;
CPP
reject missing_semicolon <<'CPP'
type Positive = int where (self > 0)
CPP

# A predicate outside the modeled fragment is refused, not approximated.
reject unmodeled_base <<'CPP'
type Odd = double where (self > 0.0);
CPP
reject effectful_predicate <<'CPP'
int counter = 0;
int bump() { return ++counter; }
type Odd = int where (self > bump());
CPP

# A refinement declared where this implementation does not recognize one.
reject inside_a_function <<'CPP'
int f() {
    type Positive = int where (self > 0);
    return 0;
}
CPP

grep -q 'not shown to satisfy refinement type' "$run/unproven_introduction.log"
grep -q 'ordinary function.*return cannot establish refinement' "$run/ordinary_refined_return.log"
grep -q 'ordinary function.*return cannot establish refinement' "$run/ordinary_refined_return_definition.log"
grep -q 'ordinary function.*return cannot establish refinement' "$run/pure_does_not_prove_refined_return.log"
grep -q 'ordinary function.*return cannot establish refinement' "$run/ordinary_refined_reference_return.log"
grep -q 'must bind a tracked local object' "$run/reference_to_a_temporary_is_refused.log"
grep -q 'does not satisfy its contract' "$run/reference_to_parameter_tracks_mutation.log"
# A write through an alias is a write to the storage: the refinement is owed
# there, and no fact about an earlier version survives it.
grep -q 'not shown to satisfy refinement type' "$run/mutable_reference_cannot_bypass_membership.log"
grep -q 'not shown to satisfy refinement type' "$run/refined_reference_binding_requires_membership.log"
grep -q 'not shown to satisfy refinement type' "$run/refined_reference_write_requires_membership.log"
grep -q 'does not satisfy its contract' "$run/refined_reference_does_not_keep_a_stale_fact.log"
grep -q 'does not satisfy its contract' "$run/reference_update_observes_current_version.log"
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
int where (int x) {
    return x;
}
int main() {
    return where (0);
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
namespace first { type Positive = int where (self > 0); }
namespace second { using Positive = int; }
verified int zero() ensures (result == 0) {
    second::Positive value = 0;
    return value;
}
int main() { return zero(); }
CPP
accept indexed_alias_preserves_arguments <<'CPP'
type Index(unsigned n) = unsigned where (self < n);
using Four = Index<4>;
using Again = Four;
verified unsigned three() ensures (result == 3u) {
    Again value = 3u;
    return value;
}
int main() { return three() == 3u ? 0 : 1; }
CPP
accept nested_alias_preserves_all_predicates <<'CPP'
type Positive = int where (self > 0);
using Base = Positive;
type Bounded = Base where (self <= 100);
verified int identity(Bounded value) ensures (result > 0 && result <= 100) {
    return value;
}
int main() { return identity(1) == 1 ? 0 : 1; }
CPP
accept refined_return_is_a_postcondition <<'CPP'
type Positive = int where (self > 0);
verified Positive one() { return 1; }
verified Positive choose(bool b) {
    if (b) return 1;
    return 2;
}
verified Positive from_path(int x) expects (x > 0) { return x; }
verified int caller() ensures (result > 0) { return one(); }
int main() { return caller() == 1 && choose(false) == 2 && from_path(3) == 3 ? 0 : 1; }
CPP
accept local_reference_tracks_storage <<'CPP'
type Small = unsigned where (self < 10u);
using SmallReference = const Small&;
verified unsigned forwarding(unsigned x) ensures (result == x) {
    unsigned y = x;
    auto&& r = y;
    return r;
}
verified unsigned write_alias() ensures (result == 3u) {
    Small x = 1u;
    unsigned& r = x;
    unsigned& s = r;
    r = 2u;
    ++s;
    return x;
}
verified unsigned read_alias() ensures (result == 3u) {
    unsigned x = 1u;
    SmallReference r = x;
    x = 3u;
    return r;
}
verified unsigned invalidate_view() ensures (result == 20u) {
    unsigned x = 1u;
    const Small& r = x;
    x = 20u;
    return r;
}
verified unsigned loop_alias() ensures (result == 9u) {
    Small x = 0u;
    unsigned& r = x;
    while (r < 9u) invariant (x <= 9u) { ++r; }
    return x;
}
int main() {
    return forwarding(4u) == 4u && write_alias() == 3u && read_alias() == 3u && invalidate_view() == 20u && loop_alias() == 9u ? 0 : 1;
}
CPP

# Refined members are ordinary refined storage (SPEC.md 17.6). Construction and
# every later write cross into the member's declared type through the same
# machinery a refined local uses; a verified parameter supplies the validity of
# its refined subobjects as an entry premise, exactly as a refined parameter
# does (SPEC.md 17.2).
accept refined_members_construct_mutate_and_enter <<'CPP'
type Positive = int where (self > 0);
type Percentage = int where (self >= 0 && self <= 100);
struct S { Positive a; Percentage b; };

verified int constructed() ensures (result > 0) {
    S s{3, 50};
    return s.a;
}
verified int written() ensures (result > 0) {
    S s{1, 0};
    s.a = 9;
    return s.a;
}
// Writing one member leaves its sibling's version, and its fact, standing.
verified int sibling_survives_a_write() ensures (result > 0) {
    S s{1, 7};
    s.a = 4;
    return s.b;
}
// A refined member of a parameter is valid on entry, so the body may rely on
// it without reproving it here (SPEC.md 17.2).
verified int from_a_parameter(S s) ensures (result > 0) {
    return s.a;
}
// A reference denotes the member's own storage, so a write through it is a
// write to that place and proves the member's predicate there.
verified int written_through_a_reference() ensures (result > 0) {
    S s{1, 0};
    int& r = s.a;
    r = 8;
    return s.a;
}
// Writing a sibling through a reference reaches only that sibling.
verified int a_sibling_reference_leaves_the_member_alone() ensures (result > 0) {
    S s{4, 0};
    int& r = s.b;
    r = 11;
    return s.a;
}
int main() {
    return constructed() == 3 && written() == 9 && sibling_survives_a_write() == 7 &&
                   written_through_a_reference() == 8 && a_sibling_reference_leaves_the_member_alone() == 4
               ? 0
               : 1;
}
CPP

# A value crossing from one refinement into another owes the target's predicate
# like any other crossing. The implication is proved from the predicates, never
# assumed from the names, so a true one is admitted and a false one is refused.
# SPEC: REFINEOBL-002, REFINEOBL-004

# `self > 0` implies `self >= 0`, so this crossing is discharged.
accept a_stronger_refinement_enters_a_weaker_one <<'CPP'
type Positive = int where (self > 0);
type NonNegative = int where (self >= 0);
verified NonNegative widen(Positive p) ensures (result >= 0) {
    return p;
}
int main() { return widen(3) >= 0 ? 0 : 1; }
CPP

# The converse does not hold: zero satisfies `self >= 0` and not `self > 0`.
# Nothing about the declaration order or the names makes this admissible.
reject a_weaker_refinement_does_not_enter_a_stronger_one <<'CPP'
type Positive = int where (self > 0);
type NonNegative = int where (self >= 0);
verified Positive narrow(NonNegative n) ensures (result > 0) {
    return n;
}
CPP

# The relation is arithmetic, not lexical: nothing declares Tiny and Small to be
# related, and the kernel relates `self < 5` to `self < 10` on its own.
accept an_unrelated_pair_crosses_when_arithmetic_proves_it <<'CPP'
type Small = unsigned where (self < 10u);
type Tiny = unsigned where (self < 5u);
verified Small from_tiny(Tiny t) ensures (result < 10u) {
    return t;
}
int main() { return from_tiny(3u) < 10u ? 0 : 1; }
CPP

# Two names for one predicate. The crossing holds because the predicate is
# proved, not because the two declarations were identified with each other:
# verification identity is what a refinement means, not how it is spelled.
accept distinct_names_with_one_predicate_cross <<'CPP'
type Positive = int where (self > 0);
type Count = int where (self > 0);
verified Count relabel(Positive p) ensures (result > 0) {
    return p;
}
int main() { return relabel(2) > 0 ? 0 : 1; }
CPP

# A refinement crosses a translation unit the same way it crosses anything else:
# the predicate is proved where the value enters, never trusted because a
# declaration elsewhere named the type. A header is the realistic way an
# unverified definition in another unit would reach a caller, so the boundary is
# checked through one rather than only within a single file.
# SPEC: REFINEOBL-004
cat > "$run/supplied.hpp" <<'HPP'
#pragma once
type Positive = int where (self > 0);
Positive supplied(int x);
HPP
reject a_header_declaration_does_not_carry_evidence <<'CPP'
#include "supplied.hpp"
verified int use(int x) expects (x > 0) ensures (result > 0) {
    Positive p = supplied(x);
    return p;
}
CPP
grep -q 'ordinary function.*return cannot establish refinement' \
    "$run/a_header_declaration_does_not_carry_evidence.log"

echo "refinement membership is proven or refused; contextual words keep their C++ meaning"
