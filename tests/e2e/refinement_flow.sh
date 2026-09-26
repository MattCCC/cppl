#!/usr/bin/env bash
# The refinement-flow surface: which value crossings this implementation proves,
# and which it refuses (SPEC.md 17, 12.8, 12.9).
#
# Every case here was found by surveying the crossings RFC 0012 enumerates. A
# case that must be refused is refused for a stated reason, so that admitting it
# later is a deliberate change with a visible diff rather than an accident. A
# case that must verify pins reasoning power that exists today.
set -euo pipefail
CPPL="$1"
WORK="$3"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/refinement-flow.XXXXXX")

# A program this implementation must prove.
accept() {
    local name="$1"
    cat > "$run/$name.cpp"
    echo 'int main() { return 0; }' >> "$run/$name.cpp"
    if ! "$CPPL" -std=c++20 "$run/$name.cpp" -o "$run/$name" > "$run/$name.log" 2>&1; then
        echo "a valid refinement flow was refused: $name" >&2
        cat "$run/$name.log" >&2
        exit 1
    fi
}

# A program this implementation must refuse, and the reason it must give.
refuse() {
    local name="$1" pattern="$2"
    cat > "$run/$name.cpp"
    echo 'int main() { return 0; }' >> "$run/$name.cpp"
    if "$CPPL" -std=c++20 "$run/$name.cpp" -o "$run/$name" > "$run/$name.log" 2>&1; then
        echo "an unproven refinement flow was accepted: $name" >&2
        cat "$run/$name.log" >&2
        exit 1
    fi
    test ! -e "$run/$name"
    ! grep -q PROVEN "$run/$name.log"
    if ! grep -Eq "$pattern" "$run/$name.log"; then
        echo "$name was refused for an unexpected reason:" >&2
        cat "$run/$name.log" >&2
        exit 1
    fi
}

# --- Crossings that are proven today -----------------------------------------

# A literal entering a refined local owes and discharges its predicate.
accept refined_local <<'CPP'
type Positive = int where (self > 0);
verified int f() ensures (result > 0) { Positive x = 1; return x; }
CPP

# A verified function may state a refined return.
accept refined_return <<'CPP'
type Positive = int where (self > 0);
verified Positive f() ensures (result > 0) { return 1; }
CPP

# A refinement of a refinement keeps every predicate that applies (SPEC.md 17.5).
accept nested_refinement_composition <<'CPP'
type NonNegative = int where (self >= 0);
type Percentage = NonNegative where (self <= 100);
verified int f(Percentage p) ensures (result >= 0 && result <= 100) { return p; }
CPP

# A refined parameter's predicate is an entry fact; the author does not restate it.
accept refined_parameter_is_an_entry_fact <<'CPP'
type Positive = int where (self > 0);
verified int f(Positive x) ensures (result > 0) { return x; }
CPP

# Path conditions discharge a crossing the branch establishes (SPEC.md 17.3).
accept branch_establishes_membership <<'CPP'
type Percentage = int where (self >= 0 && self <= 100);
verified int f(int x) ensures (result >= 0) {
    if (x >= 0) { if (x <= 100) { Percentage p = x; return p; } }
    return 0;
}
CPP

# A loop may carry a refined local across iterations under its invariant.
accept refined_local_in_loop <<'CPP'
type Small = unsigned where (self < 10u);
verified unsigned f() ensures (result == 9u) {
    Small x = 0u;
    while (x < 9u) invariant (x <= 9u) { ++x; }
    return x;
}
CPP

# '&&' states a proposition in a contract clause.
accept conjunction_in_contract <<'CPP'
verified int f(int x) expects (x >= 0 && x <= 100) ensures (result >= 0) { return x; }
CPP

# A conditional expression is modeled where the path splits on it.
accept conditional_in_return <<'CPP'
verified int f(bool b) ensures (result > 0) { return b ? 1 : 2; }
CPP

# Binding a conditional to a local splits the route, so each arm is proved under
# what its own path supposes rather than as one opaque `select` term.
accept conditional_bound_to_a_local <<'CPP'
verified int f(bool b) ensures (result > 0) { int x = b ? 1 : 2; return x; }
CPP

# A refinement crossing may be discharged arm by arm.
accept refined_crossing_through_a_conditional <<'CPP'
type Positive = int where (self > 0);
verified int f(bool b) ensures (result > 0) { Positive x = b ? 1 : 2; return x; }
CPP

# An arm that is itself a conditional splits again.
accept nested_conditional_arms <<'CPP'
verified int f(bool a, bool b) ensures (result > 0) { int x = a ? (b ? 1 : 2) : 3; return x; }
CPP

# The condition's own facts are available inside the arm it guards.
accept condition_informs_its_arm <<'CPP'
verified int f(int y) ensures (result > 0) { int x = y > 0 ? y : 1; return x; }
CPP

# A conditional's value survives any number of intervening locals: a read
# replays the value its version was given, so the route splits on the condition
# that established it however far back that was.
accept conditional_through_a_local_hop <<'CPP'
verified int f(bool b) ensures (result > 0) { int x = b ? 1 : 2; int y = x; return y; }
CPP
accept conditional_through_several_local_hops <<'CPP'
verified int f(bool b) ensures (result > 0) { int x = b ? 1 : 2; int y = x; int z = y; return z; }
CPP
accept refined_crossing_after_local_hops <<'CPP'
type Positive = int where (self > 0);
verified int f(bool b) ensures (result > 0) { int x = b ? 1 : 2; int y = x; Positive p = y; return p; }
CPP

# A conditional whose arm reads an earlier conditional local. Resolution is
# transitive, so the second binding sees the first conditional and splits on it
# too, rather than one opaque term.
accept chained_conditional_locals <<'CPP'
verified int f(bool a, bool b) ensures (result > 0) { int x = a ? 1 : 2; int y = b ? x : 3; return y; }
CPP
accept three_chained_conditional_locals <<'CPP'
verified int f(bool a, bool b, bool c) ensures (result > 0) {
    int x = a ? 1 : 2; int y = b ? x : 3; int z = c ? y : 4; return z;
}
CPP
accept refined_crossing_through_chained_conditionals <<'CPP'
type Positive = int where (self > 0);
verified int f(bool a, bool b) ensures (result > 0) { int x = a ? 1 : 2; Positive y = b ? x : 3; return y; }
CPP

# --- Boolean conditions -------------------------------------------------------
#
# `&&` and `||` state a proposition, and a proposition is not a value. In a
# condition they are elaborated into the routes they select between, which is
# what makes short-circuit evaluation exact: an operand appears only on the
# route where C++ evaluates it.

# The true route of `A && B` establishes both sides, so a two-sided predicate
# is discharged without writing nested ifs.
accept conjunction_as_an_if_condition <<'CPP'
type Percentage = int where (self >= 0 && self <= 100);
verified int f(int x) ensures (result >= 0) {
    if (x >= 0 && x <= 100) { Percentage p = x; return p; }
    return 0;
}
CPP

# The false route of `A || B` establishes both negations.
accept disjunction_false_route_establishes_both <<'CPP'
type Positive = int where (self > 0);
verified int f(int x) ensures (result > 0) {
    if (x > 5 || x <= 0) { return 1; }
    Positive p = x;
    return p;
}
CPP

# The true route of `A || B` is the union of its sides, so it is provable when
# each side alone suffices.
accept disjunction_true_route_is_a_union <<'CPP'
type Positive = int where (self > 0);
verified int f(int x) ensures (result > 0) {
    if (x > 5 || x > 10) { Positive p = x; return p; }
    return 1;
}
CPP

# `!` exchanges the routes its operand selects between.
accept negation_swaps_the_routes <<'CPP'
type Positive = int where (self > 0);
verified int f(int x) ensures (result > 0) {
    if (!(x > 0)) { return 1; }
    Positive p = x;
    return p;
}
CPP

# Elaboration recurses, so the connectives nest.
accept de_morgan_over_a_disjunction <<'CPP'
type Percentage = int where (self >= 0 && self <= 100);
verified int f(int x) ensures (result >= 0) {
    if (!(x < 0 || x > 100)) { Percentage p = x; return p; }
    return 0;
}
CPP
accept conjunction_of_a_disjunction <<'CPP'
type Positive = int where (self > 0);
verified int f(int x, bool b, bool c) ensures (result > 0) {
    if (x > 0 && (b || c)) { Positive p = x; return p; }
    return 1;
}
CPP
accept nested_connectives <<'CPP'
type Positive = int where (self > 0);
verified int f(int x, bool b, bool c, bool d) ensures (result > 0) {
    if (((x > 0 && b) || (x > 5 && c)) && !d) { Positive p = x; return p; }
    return 1;
}
CPP

# --- Crossings that must be refused ------------------------------------------

# An ordinary function's declaration is not proof of its refined return.
refuse unverified_refined_return 'ordinary function.*return cannot establish refinement' <<'CPP'
type Positive = int where (self > 0);
Positive f();
CPP

# A refined member is ordinary refined storage: it owes its predicate where a
# value enters it, not where it is declared (SPEC.md 17.6). Construction inside
# a verified body is checked at the member's own place, so a value that does not
# satisfy the predicate is refused there rather than by refusing the struct.
refuse refined_member_construction 'not shown to satisfy refinement type' <<'CPP'
type Positive = int where (self > 0);
struct S { Positive p; };
verified int f() ensures (result > 0) { S s{0}; return s.p; }
CPP

# The same crossing covers a later write to the member, through the one write
# path every other write uses.
refuse refined_member_write 'not shown to satisfy refinement type' <<'CPP'
type Positive = int where (self > 0);
struct S { Positive p; };
verified int f() ensures (result > 0) { S s{1}; s.p = 0; return s.p; }
CPP

# A record built outside a verified body establishes its members without any
# proof, so naming a refinement there is still refused.
refuse refined_member_unverified_storage 'outside a modeled verified body' <<'CPP'
type Positive = int where (self > 0);
struct S { Positive p; };
S global{0};
CPP

# Construction that satisfies every member predicate is proven, and the member
# reads at the version construction established.
accept refined_member_is_constructed_and_read <<'CPP'
type Positive = int where (self > 0);
struct S { Positive p; };
verified int f() ensures (result > 0) { S s{3}; return s.p; }
CPP

# A write into the member establishes a new version that satisfies the
# predicate, and the read that follows observes that version.
accept refined_member_is_written <<'CPP'
type Positive = int where (self > 0);
struct S { Positive p; };
verified int f() ensures (result > 0) { S s{1}; s.p = 7; return s.p; }
CPP

# Distinct members are distinct places: writing one leaves the other's fact
# standing, and neither borrows the other's predicate.
accept refined_members_are_distinct_places <<'CPP'
type Positive = int where (self > 0);
struct S { Positive a; Positive b; };
verified int f() ensures (result > 0) { S s{1, 2}; s.a = 5; return s.b; }
CPP

# A value that does not satisfy the predicate cannot enter the type.
refuse unproven_crossing 'not shown to satisfy refinement type' <<'CPP'
type Positive = int where (self > 0);
verified int f() ensures (result > 0) { Positive x = 0; return x; }
CPP

# A refinement is not established by naming a wider one.
refuse widening_is_not_proof 'not shown to satisfy refinement type' <<'CPP'
type NonNegative = int where (self >= 0);
type Positive = int where (self > 0);
verified int f(NonNegative x) ensures (result > 0) { Positive y = x; return y; }
CPP

# Splitting a conditional adds proof power, never a fact. Each arm must hold on
# its own path: one failing arm rejects the whole binding.
refuse conditional_true_arm_fails 'does not satisfy its contract' <<'CPP'
verified int f(bool b) ensures (result > 0) { int x = b ? 0 : 2; return x; }
CPP
refuse conditional_false_arm_fails 'does not satisfy its contract' <<'CPP'
verified int f(bool b) ensures (result > 0) { int x = b ? 1 : 0; return x; }
CPP
refuse conditional_arm_fails_refinement 'not shown to satisfy refinement type' <<'CPP'
type Positive = int where (self > 0);
verified int f(bool b) ensures (result > 0) { Positive x = b ? 1 : 0; return x; }
CPP

# A guarded arm supposes only what its condition states, never more.
refuse conditional_does_not_overreach 'does not satisfy its contract' <<'CPP'
verified int f(int y) ensures (result > 5) { int x = y > 0 ? y : 1; return x; }
CPP

# Resolving a conditional through locals adds proof power, never a fact: a
# failing arm still rejects however many hops away it was written.
refuse chained_conditional_arm_fails 'does not satisfy its contract' <<'CPP'
verified int f(bool a, bool b) ensures (result > 0) { int x = a ? 1 : 0; int y = b ? x : 3; return y; }
CPP
refuse chained_conditional_arm_fails_refinement 'not shown to satisfy refinement type' <<'CPP'
type Positive = int where (self > 0);
verified int f(bool a, bool b) ensures (result > 0) { int x = a ? 1 : 0; Positive y = b ? x : 3; return y; }
CPP
refuse refined_crossing_after_hops_fails 'not shown to satisfy refinement type' <<'CPP'
type Positive = int where (self > 0);
verified int f(bool b) ensures (result > 0) { int x = b ? 1 : 0; int y = x; Positive p = y; return p; }
CPP

# Each side of `&&` is established only on the route that evaluates it, so one
# side alone does not discharge a two-sided predicate.
refuse conjunction_needs_both_sides 'not shown to satisfy refinement type' <<'CPP'
type Percentage = int where (self >= 0 && self <= 100);
verified int f(int x) ensures (result >= 0) {
    if (x >= 0) { Percentage p = x; return p; }
    return 0;
}
CPP

# The route where `A && B` fails is the union of `!A` and `A && !B`. It is not
# one route supposing both sides false, so it establishes neither.
refuse conjunction_false_route_supposes_neither_side 'does not satisfy its contract' <<'CPP'
verified int f(int x) ensures (result > 0) {
    if (x >= 0 && x <= 100) { return 1; }
    return x;
}
CPP

# The true route of `A || B` is a union, so neither side holds on all of it.
refuse disjunction_true_route_establishes_neither_side 'not shown to satisfy refinement type' <<'CPP'
type Positive = int where (self > 0);
verified int f(int x) ensures (result > 0) {
    if (x > 0 || x < 10) { Positive p = x; return p; }
    return 1;
}
CPP

# A negated condition states exactly its operand's failure, never more.
refuse negation_does_not_overreach 'not shown to satisfy refinement type' <<'CPP'
type Big = int where (self > 5);
verified int f(int x) ensures (result > 5) {
    if (!(x > 0)) { return 6; }
    Big p = x;
    return p;
}
CPP

# A nested condition is not flattened: changing one side's bound must change
# what the route establishes.
refuse nested_connectives_need_the_stated_bound 'not shown to satisfy refinement type' <<'CPP'
type Positive = int where (self > 0);
verified int f(int x, bool b, bool c, bool d) ensures (result > 0) {
    if (((x > 0 && b) || (x >= 0 && c)) && !d) { Positive p = x; return p; }
    return 1;
}
CPP

# --- Member places -----------------------------------------------------------
#
# An aggregate local is tracked as one place per data member (SPEC.md 12.10),
# so a member is storage with its own version rather than a projection out of
# one value of the whole object. These pin that a write reaches exactly the
# member written, and no other.

accept aggregate_member_read <<'CPP'
struct S { int x; int y; };
verified int f() ensures (result == 3) { S s{3, 7}; return s.x; }
CPP

accept member_write_is_seen <<'CPP'
struct S { int x; };
verified int f() ensures (result == 4) { S s{3}; s.x = 4; return s.x; }
CPP

# A write to one member must leave its siblings exactly as they were.
accept sibling_member_untouched <<'CPP'
struct S { int x; int y; };
verified int f() ensures (result == 3) { S s{3, 7}; s.y = 9; return s.x; }
CPP

refuse sibling_member_is_not_the_written_one 'does not satisfy its contract' <<'CPP'
struct S { int x; int y; };
verified int f() ensures (result == 9) { S s{3, 7}; s.y = 9; return s.x; }
CPP

# A member's old version must not survive a write to it.
refuse written_member_keeps_no_old_value 'does not satisfy its contract' <<'CPP'
struct S { int x; int y; };
verified int f() ensures (result == 7) { S s{3, 7}; s.y = 9; return s.y; }
CPP

# Two objects of one type are different storage.
accept distinct_objects_are_distinct_places <<'CPP'
struct S { int x; };
verified int f() ensures (result == 1) { S a{1}; S b{2}; b.x = 5; return a.x; }
CPP

refuse distinct_objects_do_not_share_a_write 'does not satisfy its contract' <<'CPP'
struct S { int x; };
verified int f() ensures (result == 5) { S a{1}; S b{2}; b.x = 5; return a.x; }
CPP

# Members take part in ordinary branch and update reasoning.
accept member_through_a_branch <<'CPP'
struct S { int x; };
verified int f(bool b) ensures (result > 0) { S s{1}; if (b) { s.x = 2; } return s.x; }
CPP

refuse member_through_a_branch_needs_every_route 'does not satisfy its contract' <<'CPP'
struct S { int x; };
verified int f(bool b) ensures (result > 1) { S s{1}; if (b) { s.x = 2; } return s.x; }
CPP

accept member_compound_update <<'CPP'
struct S { unsigned x; };
verified unsigned f() ensures (result == 5u) { S s{3u}; s.x += 2u; return s.x; }
CPP

# Construction this body cannot see the effect of on every member is refused,
# rather than leaving a member tracked at an unconstrained value.
refuse partial_aggregate_initialization 'partial aggregate initialization is not modeled' <<'CPP'
struct S { int x; int y; };
verified int f() ensures (result == 0) { S s{1}; return s.y; }
CPP

refuse aggregate_without_an_initializer 'cannot state what each member holds' <<'CPP'
struct S { int x; };
verified int f() ensures (result == 0) { S s; return s.x; }
CPP

refuse aggregate_from_a_constructor 'cannot state what each member holds' <<'CPP'
struct S { int x; S(int v) : x(v) {} };
verified int f() ensures (result == 1) { S s(1); return s.x; }
CPP

# --- Element places ----------------------------------------------------------
#
# An array is a record whose members are its elements, so a constant index names
# a place exactly as a field name does. A variable index names no single place
# and is refused, because deciding which element it selects needs the extent
# obligations the capability model supplies, not a guess.

accept array_constant_index <<'CPP'
verified int f() ensures (result == 2) { int a[3]{1, 2, 3}; return a[1]; }
CPP

accept element_write_is_seen <<'CPP'
verified int f() ensures (result == 9) { int a[3]{1, 2, 3}; a[1] = 9; return a[1]; }
CPP

accept other_elements_untouched <<'CPP'
verified int f() ensures (result == 1) { int a[3]{1, 2, 3}; a[1] = 9; return a[0]; }
CPP

refuse element_write_does_not_reach_a_sibling 'does not satisfy its contract' <<'CPP'
verified int f() ensures (result == 9) { int a[3]{1, 2, 3}; a[1] = 9; return a[0]; }
CPP

refuse written_element_keeps_no_old_value 'does not satisfy its contract' <<'CPP'
verified int f() ensures (result == 2) { int a[3]{1, 2, 3}; a[1] = 9; return a[1]; }
CPP

refuse subscript_outside_the_extent 'cannot state as a value' <<'CPP'
verified int f() ensures (result == 0) { int a[3]{1, 2, 3}; return a[7]; }
CPP

# A variable index names a symbolic element place, which is not resolved to any
# particular element: the value read is not any one initializer, so a contract
# claiming one is unproven (RFC 0014 §17 step 7).
refuse variable_index_read 'does not satisfy its contract' <<'CPP'
verified int f(unsigned i) expects (i < 3u) ensures (result > 0) { int a[3]{1, 2, 3}; return a[i]; }
CPP

# A write at a symbolic index may hit any element, so a fact about a sibling
# does not survive it: `i != 0` is not proved (RFC 0014 §4).
refuse variable_index_write 'does not satisfy its contract' <<'CPP'
verified int f(unsigned i) expects (i < 3u) ensures (result == 1) { int a[3]{1, 2, 3}; a[i] = 5; return a[0]; }
CPP

# A member that is itself an aggregate is the places its own members are, not
# one value: `o.i.v` is a place reached by a longer path, exactly as `o.a` is.
accept nested_aggregate_member <<'CPP'
struct Inner { int v; };
struct Outer { Inner i; };
verified int f() ensures (result == 5) { Outer o{{5}}; return o.i.v; }
CPP

# The same for a specialization, decomposed from its resolved type.
accept nested_aggregate_member_of_a_specialization <<'CPP'
struct Inner { int v; };
template <typename T> struct Outer { Inner i; };
verified int f() ensures (result == 5) { Outer<int> o{{5}}; return o.i.v; }
CPP

# An array member is the same case: its elements are places of the object, at
# element steps rather than field steps.
accept array_member_of_an_aggregate <<'CPP'
struct Holder { int items[3]; };
verified int f() ensures (result == 1) { Holder h{{1, 2, 3}}; return h.items[0]; }
CPP

# Each leaf is its own place, so a write reaches exactly the member written and
# leaves a sibling at depth alone (SPEC.md 12.10).
accept nested_write_leaves_a_sibling_alone <<'CPP'
struct Inner { int v; };
struct Outer { Inner a; Inner b; };
verified int f() ensures (result == 2) { Outer o{{1}, {2}}; o.a.v = 7; return o.b.v; }
CPP

# A nested leaf carries its own declared refinement, so the write into it owes
# that predicate where the value enters (SPEC.md 17.6).
accept nested_leaf_owes_its_refinement <<'CPP'
type Positive = int where (self > 0);
struct Inner { Positive v; };
struct Outer { Inner i; };
verified int f(int x) expects (x > 0) ensures (result > 0) { Outer o{{1}}; o.i.v = x; return o.i.v; }
CPP

refuse nested_leaf_refinement_unproven 'does not satisfy its contract' <<'CPP'
type Positive = int where (self > 0);
struct Inner { Positive v; };
struct Outer { Inner i; };
verified int f(int x) ensures (result > 0) { Outer o{{1}}; o.i.v = x; return o.i.v; }
CPP

# Construction must still be fully visible at every level: a nested member with
# fewer values than members is partial initialization, not a tracked object.
refuse nested_partial_initialization 'partial aggregate initialization' <<'CPP'
struct Inner { int v; int w; };
struct Outer { Inner i; };
verified int f() ensures (result == 1) { Outer o{{1}}; return o.i.v; }
CPP

# --- Gaps: refused today, and the reason must stay visible -------------------
#
# These are reasoning or modeling gaps, not soundness boundaries. Each is
# refused, which is the fail-closed direction. If one begins to verify, that is
# a deliberate improvement and this suite must be updated to `accept`.

# A cast to a refinement's own base type is the value it casts (SPEC.md
# ARITH-008), so what is known of that value holds of the result. It creates
# no refinement: the result is a plain `int`, and a cast into a refined type
# owes the predicate where the value enters it, like any other crossing.
accept a_cast_to_the_base_type_keeps_the_value <<'CPP'
type Positive = int where (self > 0);
verified int f(Positive x) ensures (result > 0) { return static_cast<int>(x); }
CPP

# SPEC: REFINEOBL-002, ARITH-008
refuse a_cast_does_not_establish_a_refinement 'does not satisfy|not shown to satisfy' <<'CPP'
type Positive = int where (self > 0);
verified int f(int x) ensures (result > 0) { Positive p = static_cast<Positive>(x); return p; }
CPP

# An indexed refinement's application is not resolved by the bridge yet.
refuse indexed_refinement 'requires template arguments|unresolved' <<'CPP'
type Index(unsigned n) = unsigned where (self < n);
verified unsigned f(Index(10) i) ensures (result < 10u) { return i; }
CPP

# A compound update is the assignment it abbreviates, so a refined local owes
# its predicate at the updated value. The existing negative pins a constant that
# plainly leaves the type; this pins the symbolic case, where the predicate
# survives only because the kernel relates the update to the entry fact.
# SPEC: REFINEOBL-002
accept a_compound_update_preserving_its_refinement <<'CPP'
type Big = unsigned where (self > 10u);
verified unsigned f(unsigned x) expects (x > 10u && x < 100u) ensures (result > 10u) {
    Big b = x;
    b += 5u;
    return b;
}
CPP

# An indexed refinement is applied per argument, and a wider bound follows from
# a narrower one only because the kernel proves `x < 4` implies `x < 8`. The
# index is a value the predicate mentions, never a name matched against another.
# SPEC: REFINEOBL-004
accept an_indexed_refinement_widens_when_the_kernel_proves_it <<'CPP'
type Index(unsigned n) = unsigned where (self < n);
verified unsigned takes8(Index<8u> x) ensures (result < 8u) { return x; }
verified unsigned f(Index<4u> x) ensures (result < 8u) { return takes8(x); }
CPP

# Passing an argument into a refined parameter is a crossing at the call, owed
# by the caller from what it knows. The existing cases cross a value already
# carrying the refinement; this crosses a plain `int` that only a precondition
# relates to the predicate, so the call stands on the kernel's reasoning.
# SPEC: REFINEOBL-004
accept a_plain_argument_crosses_into_a_refined_parameter <<'CPP'
type Positive = int where (self > 0);
verified int takes(Positive p) ensures (result > 0) { return p; }
verified int f(int x) expects (x > 0) ensures (result > 0) { return takes(x); }
CPP

# A refinement over a refinement carries both predicates, so entering the inner
# one owes the outer one as well.
# SPEC: REFINEOBL-002
accept a_nested_refinement_owes_both_predicates <<'CPP'
type Positive = int where (self > 0);
type Small = Positive where (self < 10);
verified int f(int x) expects (x > 0 && x < 10) ensures (result > 0) {
    Small s = x;
    return s;
}
CPP

# A lambda is not a modeled body.
refuse lambda_is_not_modeled 'cannot state as a value' <<'CPP'
verified int f(int x) ensures (result == x) { auto g = [](int v) { return v; }; return g(x); }
CPP

# A compound update that leaves the refinement is refused at the update, not at
# the return: the local's own place owes the predicate whenever it is written.
# `x > 10` permits `x == 11`, so `b -= 10u` can reach 1.
# SPEC: REFINEOBL-002
refuse a_compound_update_leaving_its_refinement 'not shown to satisfy refinement type' <<'CPP'
type Big = unsigned where (self > 10u);
verified unsigned f(unsigned x) expects (x > 10u && x < 100u) ensures (result > 10u) {
    Big b = x;
    b -= 10u;
    return b;
}
CPP

# Evidence for one index argument is not evidence for another. The narrowing
# direction is unsound and stays refused however the two are spelled.
# SPEC: REFINEOBL-004, TEMPLATE-003
refuse an_indexed_refinement_does_not_narrow 'not shown to satisfy refinement type' <<'CPP'
type Index(unsigned n) = unsigned where (self < n);
verified unsigned f(Index<8u> x) ensures (result < 4u) {
    Index<4u> y = x;
    return y;
}
CPP

# The same crossing without the precondition that justifies it. Nothing relates
# `x` to the predicate, so the call is refused where the argument crosses rather
# than where the result is returned.
# SPEC: REFINEOBL-004
refuse a_plain_argument_without_its_fact_does_not_cross 'call-site precondition' <<'CPP'
type Positive = int where (self > 0);
verified int takes(Positive p) ensures (result > 0) { return p; }
verified int f(int x) ensures (result > 0) { return takes(x); }
CPP

# The inner predicate of a nested refinement is not implied by the outer one.
# `Positive` alone does not establish `Small`.
# SPEC: REFINEOBL-002
refuse a_nested_refinement_is_not_entered_by_its_base 'not shown to satisfy refinement type' <<'CPP'
type Positive = int where (self > 0);
type Small = Positive where (self < 10);
verified int f(int x) expects (x > 0) ensures (result > 0) {
    Small s = x;
    return s;
}
CPP

# A dereference resolves to a place and reads through the one read path
# (SPEC.md 12.10 VERIFIED-038, RFC 0014 §17 step 6).
accept a_pointee_reads_through_the_one_read_path <<'CPP'
verified int f(int* p) expects (readable(p)) ensures (result == result) { return *p; }
CPP

# A capability permits the access; it does not assert what the storage holds. A
# pointee this body never wrote has an opaque value, so its declared refinement
# is not available as a fact: `readable` is not a claim about the value
# (SPEC.md 12.10). Fail-closed is the correct outcome here, not a limitation to
# paper over -- the caller's entry validity for pointees is not yet modeled.
refuse a_capability_does_not_assert_the_pointee_value 'does not satisfy its contract' <<'CPP'
type Positive = int where (self > 0);
verified int f(Positive* p) expects (readable(p)) ensures (result > 0) { return *p; }
CPP

# A write through a pointer owes the pointee's predicate at the pointee's own
# place, established before the version the write binds.
accept a_write_through_a_pointer_may_establish_a_refinement <<'CPP'
type Positive = int where (self > 0);
verified void f(Positive* p) expects (writable(p)) { *p = 3; }
CPP

refuse a_write_through_a_pointer_owes_the_predicate 'not shown to satisfy refinement type' <<'CPP'
type Positive = int where (self > 0);
verified void f(Positive* p) expects (writable(p)) { *p = 0; }
CPP

# A symbolic subscript is a place like any other: the index owes its bound, and
# once bounded the element reads and writes through the one read/write path
# (RFC 0014 §17 step 7).
accept a_bounded_symbolic_subscript_reads <<'CPP'
verified unsigned f(unsigned i) expects (i < 4u) ensures (result == result) {
    unsigned a[4] = {0u, 1u, 2u, 3u};
    return a[i];
}
CPP

accept a_bounded_symbolic_subscript_writes <<'CPP'
type Small = unsigned where (self < 10u);
verified unsigned f(unsigned i) expects (i < 4u) ensures (result == result) {
    Small a[4] = {0u, 1u, 2u, 3u};
    a[i] = 5u;
    return a[0];
}
CPP

# A write into a refined element owes the element's predicate, at the element's
# own place, whether the index is decided or not.
refuse a_symbolic_element_write_owes_the_predicate 'not shown to satisfy refinement type' <<'CPP'
type Small = unsigned where (self < 10u);
verified unsigned f(unsigned i) expects (i < 4u) ensures (result == result) {
    Small a[4] = {0u, 1u, 2u, 3u};
    a[i] = 50u;
    return a[0];
}
CPP

# A subscript through a sized capability owes `index < n` against the extent the
# contract stated. The capability and the bound stay separate: `readable(a, n)`
# permits reaching the region, and the refinement on the index is what proves
# the element selected lies inside it (RFC 0014 §10, §17 step 7, SPEC.md
# VERIFIED-038).
accept a_capability_extent_bounds_a_subscript <<'CPP'
type Below4 = unsigned where (self < 4u);
verified unsigned f(const unsigned* a, Below4 i)
    expects (readable(a, 4u))
    ensures (result == result)
{
    return a[i];
}
CPP

# The extent is a term, not a count, so a region whose size is a runtime value
# bounds its elements exactly as a constant one does. Nothing here is a literal:
# `n` is a parameter, and the index is proved below it.
accept a_symbolic_capability_extent_bounds_a_subscript <<'CPP'
verified unsigned f(const unsigned* a, unsigned n, unsigned i)
    expects (readable(a, n))
    ensures (result == result)
{
    if (i < n) {
        return a[i];
    }
    return 0u;
}
CPP

# An array reference carries its extent in its own type, so a symbolic index
# into one is bounded without any capability and without any element of it
# having been observed first. The extent comes from the resolved type, never
# from which elements an earlier access happened to form (SPEC.md STORAGE-005,
# ARCHITECTURE.md ARCH-ELEM-004).
accept an_array_reference_extent_bounds_a_symbolic_subscript <<'CPP'
type Below4 = unsigned where (self < 4u);
verified unsigned f(const unsigned (&a)[4], Below4 i)
    ensures (result == result)
{
    return a[i];
}
CPP

# The same access, with nothing else in the body: no `a[0]` precedes it, so the
# extent cannot have come from a tracked element entry.
accept an_array_extent_needs_no_prior_element_observation <<'CPP'
verified unsigned f(const unsigned (&a)[4], unsigned i)
    expects (i < 4u)
    ensures (result == result)
{
    return a[i];
}
CPP

# An index bounded above the array's own extent is not bounded by it.
refuse an_index_wider_than_the_array_extent "element index' is not proven" <<'CPP'
verified unsigned f(const unsigned (&a)[4], unsigned i)
    expects (i < 8u)
    ensures (result == result)
{
    return a[i];
}
CPP

# An unbounded index into an array reference is refused, exactly as one into a
# capability region is: observing an element establishes no bound
# (SPEC.md STORAGE-005).
refuse an_unbounded_index_into_an_array_reference "element index' is not proven" <<'CPP'
verified unsigned f(const unsigned (&a)[4], unsigned i)
    ensures (result == result)
{
    return a[i];
}
CPP

# A write owes the same bound as a read, and `writable` is what permits it.
accept a_capability_extent_bounds_an_element_write <<'CPP'
type Below4 = unsigned where (self < 4u);
verified void f(unsigned* a, Below4 i)
    expects (writable(a, 4u))
    ensures (true)
{
    a[i] = 7u;
}
CPP

# An index bounded by a wider extent than the capability states is not bounded
# by the capability: `i < 8` does not give `i < 4`, and the difference is
# exactly the memory the extent exists to keep the access out of.
refuse an_index_wider_than_the_stated_extent "element index' is not proven" <<'CPP'
type Below8 = unsigned where (self < 8u);
verified unsigned f(const unsigned* a, Below8 i)
    expects (readable(a, 4u))
    ensures (result == result)
{
    return a[i];
}
CPP

# The capability is what permits the dereference; nothing about the pointer's
# value supplies it, non-nullness least of all (SPEC.md VERIFIED-037).
refuse a_dereference_without_a_capability "requires 'readable" <<'CPP'
type Positive = int where (self > 0);
verified int f(Positive* p) expects (p != nullptr) ensures (result > 0) { return *p; }
CPP

# Invalidating a pointee across a call is a rule about what the callee could
# have written, not a blanket refusal of pointer arguments. What the call
# establishes is still readable after it, and a pointee reached through a
# pointer to const survives, since writing through one is not something the
# callee may do (SPEC.md 12.10 VERIFIED-040).
accept a_pointee_read_after_a_call_and_a_const_pointee_across_one <<'CPP'
verified void touch(int* q) expects (writable(q)) ensures (true) { *q = 0; }
verified int reads_after(int* p) expects (readable(p) && writable(p)) ensures (result == result) {
    touch(p);
    return *p;
}
verified int const_pointee_survives(const int* p, int* q)
    expects (readable(p) && writable(q))
    ensures (result == result)
{
    int seen = *p;
    touch(q);
    return seen;
}
CPP

# One index term names one place, so a write at it is read back at it. This is
# the completeness side of keeping `a[i]` and `a[j]` apart: telling places apart
# by their index term must still recognize the same term as the same place, or
# an element could never be read back after being written (RFC 0014 §4).
accept one_index_term_names_one_place <<'CPP'
verified int f(unsigned i) expects (i < 3u) ensures (result == 7) {
    int a[3] = {1, 1, 1};
    a[i] = 7;
    return a[i];
}
CPP

# Invalidating what a write may overlap must not cost a place the fact it just
# wrote: the second write at one term is a fact about that element afterwards.
accept a_second_write_at_one_index_term_is_read_back <<'CPP'
verified int f(unsigned i) expects (i < 3u) ensures (result == 7) {
    int a[3] = {1, 1, 1};
    a[i] = 5;
    a[i] = 7;
    return a[i];
}
CPP

# Identity is of the term, not of the spelling's occurrence: a compound index
# written twice is one term, and a local index untouched between the two
# accesses reads at one version.
accept a_compound_index_term_names_one_place <<'CPP'
verified unsigned f(unsigned i) expects (i < 2u) ensures (result == 7u) {
    unsigned a[3] = {1u, 1u, 1u};
    a[i + 1u] = 7u;
    return a[i + 1u];
}
CPP

accept an_untouched_local_index_names_one_place <<'CPP'
verified int f(unsigned i) expects (i < 3u) ensures (result == 7) {
    int a[3] = {1, 1, 1};
    unsigned k = i;
    a[k] = 7;
    return a[k];
}
CPP

# A symbolic element read supplies the element type's predicate. Every value
# that reached an element of a local array was written here and owed that
# predicate where it was written, so the element holds a value of its type even
# though which element is undecided. This supposes a fact and charges no new
# obligation: the crossing was already paid for at the write (SPEC.md 17.2).
# SPEC: REFINE-060, REFINE-062
accept a_symbolic_element_supplies_the_element_predicate <<'CPP'
type Positive = int where (self > 0);
verified int f(unsigned i) expects (i < 3u) ensures (result > 0) {
    Positive a[3] = {1, 2, 3};
    return a[i];
}
CPP

# The same after a write, which owed the predicate itself.
accept a_symbolic_element_supplies_it_after_a_write <<'CPP'
type Positive = int where (self > 0);
verified int f(unsigned i, unsigned j) expects (i < 3u && j < 3u) ensures (result > 0) {
    Positive a[3] = {1, 2, 3};
    a[i] = 5;
    return a[j];
}
CPP

# Every predicate the type states is supplied, and a refinement of a refinement
# states both (SPEC.md 17.5).
accept a_symbolic_element_supplies_every_stated_predicate <<'CPP'
type Positive = int where (self > 0);
type Small = Positive where (self < 10);
verified int f(unsigned i) expects (i < 3u) ensures (result > 0 && result < 10) {
    Small a[3] = {1, 2, 3};
    return a[i];
}
CPP

# ...and no more than it states.
refuse a_symbolic_element_supplies_no_more 'does not satisfy its contract' <<'CPP'
type Positive = int where (self > 0);
type Small = Positive where (self < 10);
verified int f(unsigned i) expects (i < 3u) ensures (result < 5) {
    Small a[3] = {1, 2, 3};
    return a[i];
}
CPP

# Each array supplies its own predicate and not its neighbour's. Reading the
# other one of the pair must flip the verdict, which a predicate supposed of the
# wrong value would not do.
accept each_array_supplies_its_own_predicate <<'CPP'
type Small = int where (self < 10);
type Big = int where (self > 100);
verified int f(unsigned i, unsigned j) expects (i < 3u && j < 3u) ensures (result == 1) {
    Small a[3] = {1, 2, 3};
    Big b[3] = {101, 102, 103};
    return b[j] > a[i] ? 1 : 0;
}
CPP

refuse the_other_ordering_is_not_supplied 'does not satisfy its contract' <<'CPP'
type Small = int where (self < 10);
type Big = int where (self > 100);
verified int f(unsigned i, unsigned j) expects (i < 3u && j < 3u) ensures (result == 1) {
    Small a[3] = {1, 2, 3};
    Big b[3] = {101, 102, 103};
    return a[i] > b[j] ? 1 : 0;
}
CPP

# An unrefined array beside a refined one is supplied nothing.
# SPEC: REFINE-019
refuse a_plain_array_supplies_no_predicate 'does not satisfy its contract' <<'CPP'
type Small = int where (self < 10);
verified int f(unsigned i, unsigned j) expects (i < 3u && j < 3u) ensures (result == 1) {
    Small a[3] = {1, 2, 3};
    int c[3] = {1, 2, 3};
    return a[i] < 10 ? (c[j] < 10 ? 1 : 0) : 0;
}
CPP

# A pointee is supplied nothing: a pointer to a refined type erases to a pointer
# to its representation, so the declared type says nothing about what is there,
# and a caller may write the region through another pointer.
# SPEC: REFINE-061
refuse a_pointee_element_supplies_no_predicate 'does not satisfy its contract' <<'CPP'
type Positive = int where (self > 0);
verified int f(Positive* p, unsigned n, unsigned i) expects (readable(p, n)) ensures (result > 0) {
    if (i < n) { return p[i]; }
    return 1;
}
CPP

# An array a reference parameter designates is caller storage, which another
# reference may designate too.
refuse a_reference_parameter_array_supplies_no_predicate 'does not satisfy its contract' <<'CPP'
type Positive = int where (self > 0);
verified int f(Positive (&a)[3], unsigned i) expects (i < 3u) ensures (result > 0) {
    return a[i];
}
CPP

# A parameter passed by value is the callee's own copy, so its members are
# places of this body and writing one is an ordinary write (SPEC.md E.1).
# SPEC: STORAGE-011
accept a_by_value_parameter_member_is_written_and_read <<'CPP'
struct S { int x; int y; };
verified int f(S s) ensures (result == 5) {
    s.x = 5;
    return s.x;
}
CPP

# The sibling keeps the value it arrived with rather than becoming unknown.
accept a_by_value_parameter_sibling_survives_the_write <<'CPP'
struct S { int x; int y; };
verified int f(S s) ensures (result == result) {
    s.x = 5;
    return s.y;
}
CPP

# ...and is not invented: nothing says what the caller passed.
refuse a_by_value_parameter_sibling_is_not_invented 'does not satisfy its contract' <<'CPP'
struct S { int x; int y; };
verified int f(S s) ensures (result == 5) {
    s.x = 5;
    return s.y;
}
CPP

# A member nested one level deeper is reached by a longer path.
accept a_nested_parameter_member_is_written_and_read <<'CPP'
struct Inner { int v; };
struct Outer { Inner i; int w; };
verified int f(Outer o) ensures (result == 5) {
    o.i.v = 5;
    return o.i.v;
}
CPP

# A member of a by-value parameter owes its refinement on the way in, exactly as
# a local's member does.
refuse a_refined_parameter_member_write_owes_its_predicate 'not shown to satisfy refinement type' <<'CPP'
type Positive = int where (self > 0);
struct S { Positive p; };
verified int f(S s) ensures (result > 0) {
    s.p = 0;
    return s.p;
}
CPP

accept a_refined_parameter_member_write_satisfying_it <<'CPP'
type Positive = int where (self > 0);
struct S { Positive p; };
verified int f(S s) ensures (result > 0) {
    s.p = 7;
    return s.p;
}
CPP

# A parameter that may designate caller storage gets none of this: a write
# through it reaches storage the callee does not own.
# SPEC: STORAGE-011
refuse a_reference_parameter_member_is_not_callee_storage 'not tracked storage|cannot state as a value' <<'CPP'
struct S { int x; int y; };
verified int f(S& s) ensures (result == 5) {
    s.x = 5;
    return s.x;
}
CPP

# Ownership of a by-value parameter stops at indirection. The storage an
# alias-bearing member designates is not the parameter object's, so a fact about
# it must not survive a write through a pointer that may designate it too. An
# implementation that treated `*s.p` as callee-owned would keep the stale fact
# and prove this false program.
#
# The four cases below are fail-closed guards rather than alias decisions: today
# a type with a pointer or reference member is not a modeled value type, so such
# a parameter is never tracked and these forms are refused before the ownership
# question arises. The patterns stay broad on purpose -- what each case pins is
# that the program is refused at all, whatever the reason, so making such
# parameters trackable without handling the pointee turns one of these red.
# The alias decision itself is pinned where it is modelable, by
# `negative/verified_storage.sh` `a_pointer_write_invalidates_another_pointee`.
# SPEC: STORAGE-011
refuse a_pointee_of_a_member_is_not_callee_owned 'cannot state as a value|does not satisfy its contract' <<'CPP'
struct S { int* p; };
verified int f(S s, int* q)
    expects (writable(s.p, 1u) && readable(s.p, 1u) && writable(q, 1u))
    ensures (result == 5)
{
    *s.p = 5;
    *q = 0;
    return *s.p;
}
CPP

# A call that may write through the member pointer invalidates the pointee for
# the same reason: the callee's effects reach storage the parameter never owned.
refuse a_call_through_a_member_pointer_invalidates_the_pointee 'cannot state as a value|does not satisfy its contract' <<'CPP'
struct S { int* p; };
void sink(int* q);
verified int f(S s) expects (writable(s.p, 1u) && readable(s.p, 1u)) ensures (result == 5) {
    *s.p = 5;
    sink(s.p);
    return *s.p;
}
CPP

# A pointer to a refined type erases to a pointer to its representation, so the
# member's declared type is no evidence about what the pointee holds.
# SPEC: REFINE-061
refuse a_refined_pointee_of_a_member_supplies_nothing 'cannot state as a value|does not satisfy its contract' <<'CPP'
type Positive = int where (self > 0);
struct S { Positive* p; };
verified int f(S s) expects (readable(s.p, 1u)) ensures (result > 0) {
    return *s.p;
}
CPP

# A reference member designates the referred storage rather than storage the
# parameter object owns.
refuse a_reference_member_is_not_callee_owned 'cannot state as a value|does not satisfy its contract' <<'CPP'
struct S { int& r; };
verified int f(S s) ensures (result == 5) {
    s.r = 5;
    return s.r;
}
CPP

# A write inside the parameter object reaches no storage outside it, so an
# unrelated pointer cannot disturb a contained member: the parameter object's
# address never escaped this body.
# SPEC: STORAGE-011
accept a_contained_member_survives_a_foreign_write <<'CPP'
struct S { int x; int y; };
verified int f(S s, int* q) expects (writable(q, 1u)) ensures (result == 5) {
    s.x = 5;
    *q = 0;
    return s.x;
}
CPP

# Validity is established once where the version is written and is not charged
# again where that same version is read: the write below owes the predicate, and
# the two reads of it owe nothing further.
# SPEC: REFINE-062
accept a_written_version_is_read_without_a_second_obligation <<'CPP'
type Positive = int where (self > 0);
verified int f(unsigned i) expects (i < 3u) ensures (result > 0) {
    Positive a[3] = {1, 2, 3};
    a[i] = 7;
    return a[i] > 0 ? a[i] : 1;
}
CPP

# A loop that writes elements and a symbolic read that supplies the element
# predicate were built independently, and their intersection is where a stale
# predicate would hide: the loop establishes new versions of every element, so
# the predicate must come from the loop's own write obligation and not from the
# initializer that preceded it.
# SPEC: REFINE-060
accept a_loop_write_in_the_refinement_still_supplies_it <<'CPP'
type Positive = int where (self > 0);
verified int f(unsigned i) expects (i < 3u) ensures (result > 0) {
    Positive a[3] = {1, 2, 3};
    for (unsigned k = 0u; k < 3u; ++k) { a[k] = 1; }
    return a[i];
}
CPP

# The same loop writing out of the refinement owes the predicate and fails,
# rather than leaning on the elements the initializer established.
refuse a_loop_write_out_of_the_refinement_is_refused 'not shown to satisfy refinement type' <<'CPP'
type Positive = int where (self > 0);
verified int f(unsigned i) expects (i < 3u) ensures (result > 0) {
    Positive a[3] = {1, 2, 3};
    for (unsigned k = 0u; k < 3u; ++k) { a[k] = 0; }
    return a[i];
}
CPP

# A loop over a plain array supplies nothing, and an exact value does not
# survive the versions the loop establishes.
refuse a_loop_over_a_plain_array_supplies_nothing 'does not satisfy its contract' <<'CPP'
verified int f(unsigned i) expects (i < 3u) ensures (result > 0) {
    int a[3] = {1, 2, 3};
    for (unsigned k = 0u; k < 3u; ++k) { a[k] = 1; }
    return a[i];
}
CPP

echo 'refinement flow: proven crossings and refused crossings both hold'
