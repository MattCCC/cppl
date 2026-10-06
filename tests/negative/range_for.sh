#!/usr/bin/env bash
# Range-based for loops that must be refused.
#
# SPEC: LOOP-001, LOOP-004, STMT-005, STDMODEL-015, STDMODEL-016, STDMODEL-019, STDMODEL-020
#
# A range-based for is verified as C++ iterates it, so a claim it does not
# establish is refused, and so is every way of using one this implementation
# does not model: a range that is not a name, a range of a type it does not
# model, an initialization statement it cannot see, an invariant that names
# the loop variable it holds before, and an iteration that goes on after the
# range's storage may have been replaced, which C++ leaves undefined.
#
# Each refused program has an accepted twin here that differs from it in one
# thing, so the refusal is shown to come from that thing; the programs it
# mirrors are verified in fixtures/equivalence/range_for.cpp.
set -euo pipefail
CPPL="$1"
WORK="$3"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/range-for-negative.XXXXXX")

fail() {
    echo "$1" >&2
    exit 1
}

# accept <name>: the program on stdin verifies, and runs.
accept() {
    local name="$1"
    cat > "$run/$name.cpp"
    if ! "$CPPL" -std=c++20 "$run/$name.cpp" -o "$run/$name" > "$run/$name.log" 2>&1; then
        cat "$run/$name.log" >&2
        fail "valid source was refused: $name"
    fi
    "$run/$name"
}

# refuse <name> <diagnostic>: the program on stdin is refused with the
# diagnostic, writes no program and reports nothing proven.
refuse() {
    local name="$1" expected="$2"
    cat > "$run/$name.cpp"
    if "$CPPL" -std=c++20 "$run/$name.cpp" -o "$run/$name" > "$run/$name.log" 2>&1; then
        cat "$run/$name.log" >&2
        fail "an invalid range-based for was accepted: $name"
    fi
    [ ! -e "$run/$name" ] || fail "$name wrote a program"
    if grep -q PROVEN "$run/$name.log"; then
        fail "$name reported something proven"
    fi
    if ! grep -qF -- "$expected" "$run/$name.log"; then
        cat "$run/$name.log" >&2
        fail "$name was refused, but not because: $expected"
    fi
}

# A bound one below what the elements keep.
accept a_bound_the_elements_keep <<'CPP'
#include <vector>
type Digit = unsigned where (self < 10u);
verified unsigned largest() ensures (result < 10u) {
    std::vector<Digit> digits{3u, 9u};
    unsigned best = 0u;
    for (Digit d : digits) invariant (best < 10u) { if (d > best) { best = d; } }
    return best;
}
int main() { return largest() == 9u ? 0 : 1; }
CPP
refuse an_off_by_one_bound "return path 'largest path 1' does not satisfy its contract" <<'CPP'
#include <vector>
type Digit = unsigned where (self < 10u);
verified unsigned largest() ensures (result < 9u) {
    std::vector<Digit> digits{3u, 9u};
    unsigned best = 0u;
    for (Digit d : digits) invariant (best < 10u) { if (d > best) { best = d; } }
    return best;
}
int main() { return 0; }
CPP

# Every element is read: skipping too few leaves an element the claim ignores.
accept every_element_kept_apart <<'CPP'
#include <vector>
verified unsigned first_over(const std::vector<unsigned>& v, unsigned limit) ensures (result == 0u || result > limit) {
    for (unsigned x : v) { if (x <= limit) { continue; } return x; }
    return 0u;
}
int main() { return first_over(std::vector<unsigned>{1u, 7u}, 5u) == 7u ? 0 : 1; }
CPP
refuse an_element_the_claim_ignores "does not satisfy its contract" <<'CPP'
#include <vector>
verified unsigned first_over(const std::vector<unsigned>& v, unsigned limit) ensures (result == 0u || result > limit) {
    for (unsigned x : v) { if (x < limit) { continue; } return x; }
    return 0u;
}
int main() { return 0; }
CPP

# An invariant every iteration keeps, and one an iteration breaks.
accept an_invariant_kept <<'CPP'
#include <vector>
verified unsigned capped(const std::vector<unsigned>& v) ensures (result <= 100u) {
    unsigned best = 0u;
    for (unsigned x : v) invariant (best <= 100u) { if (x <= 100u && x > best) { best = x; } }
    return best;
}
int main() { return capped(std::vector<unsigned>{5u, 500u}) == 5u ? 0 : 1; }
CPP
refuse an_invariant_broken "is not preserved by an iteration" <<'CPP'
#include <vector>
verified unsigned capped(const std::vector<unsigned>& v) ensures (result <= 100u) {
    unsigned best = 0u;
    for (unsigned x : v) invariant (best <= 100u) { if (x <= 101u && x > best) { best = x; } }
    return best;
}
int main() { return 0; }
CPP

# An invariant holds at the head, before the loop variable is initialized.
refuse an_invariant_naming_the_loop_variable \
    "names its loop variable 'x', which it holds before: the invariant holds at each iteration's head, before the loop variable is initialized" <<'CPP'
#include <vector>
verified unsigned total(const std::vector<unsigned>& v) ensures (true) {
    unsigned sum = 0u;
    for (unsigned x : v) invariant (x <= 4294967295u) { sum += x; }
    return sum;
}
int main() { return 0; }
CPP

# Replacing the range's storage and going on iterating is undefined; leaving
# the loop right after is not.
accept a_range_grown_and_left <<'CPP'
#include <vector>
verified unsigned grow_once(std::vector<unsigned>& v) ensures (true) {
    for (unsigned x : v) { if (x == 0u) { v.push_back(1u); break; } }
    return 0u;
}
int main() { std::vector<unsigned> v{0u}; return grow_once(v) == 0u && v.size() == 2u ? 0 : 1; }
CPP
refuse a_range_grown_while_iterated \
    "goes on iterating 'v' after 'std::vector::push_back'" <<'CPP'
#include <vector>
verified unsigned grow(std::vector<unsigned>& v) ensures (true) {
    for (unsigned x : v) { if (x == 0u) { v.push_back(1u); } }
    return 0u;
}
int main() { return 0; }
CPP
# A measure that descends does not make it defined: C++ took the end before the
# first iteration, so the loop goes on past the last element left.
refuse a_range_shrunk_while_iterated_under_a_measure \
    "goes on iterating 'v' after 'std::vector::pop_back'" <<'CPP'
#include <vector>
verified unsigned shrink(std::vector<unsigned>& v) ensures (true) {
    unsigned total = 0u;
    for (unsigned x : v) decreases (v.size()) { total += x; v.pop_back(); }
    return total;
}
int main() { return 0; }
CPP
refuse a_range_handed_to_a_writing_call "goes on iterating 'v' after passing it by mutable reference to 'refill'" <<'CPP'
#include <vector>
void refill(std::vector<unsigned>& v) { v.push_back(1u); }
verified unsigned grow(std::vector<unsigned>& v) ensures (true) {
    for (unsigned x : v) { refill(v); }
    return 0u;
}
int main() { return 0; }
CPP
refuse a_range_reached_by_unsafe_code "goes on iterating 'v' after the unsafe block at" <<'CPP'
#include <vector>
verified unsigned grow(std::vector<unsigned>& v) ensures (true) {
    for (unsigned x : v) { unsafe { v.clear(); } }
    return 0u;
}
int main() { return 0; }
CPP
refuse a_loop_reference_after_the_range_grew "'x' refers to an element of 'v', which may have been reallocated or ended by 'std::vector::push_back'" <<'CPP'
#include <vector>
verified unsigned grow(std::vector<unsigned>& v) ensures (true) {
    for (unsigned& x : v) { v.push_back(1u); x = 2u; break; }
    return 0u;
}
int main() { return 0; }
CPP

# A write through the loop variable owes the element type's refinement.
accept a_refined_element_written_within <<'CPP'
#include <vector>
type Digit = unsigned where (self < 10u);
verified unsigned reset() ensures (true) {
    std::vector<Digit> digits{3u, 9u};
    for (Digit& d : digits) { d = 9u; }
    return 0u;
}
int main() { return reset(); }
CPP
refuse a_refined_element_written_outside "this value is not shown to satisfy refinement type 'Digit'" <<'CPP'
#include <vector>
type Digit = unsigned where (self < 10u);
verified unsigned reset() ensures (true) {
    std::vector<Digit> digits{3u, 9u};
    for (Digit& d : digits) { d = 10u; }
    return 0u;
}
int main() { return 0; }
CPP

# A refined loop variable owes its predicate at every initialization.
accept a_refined_loop_variable_from_refined_elements <<'CPP'
#include <vector>
type Digit = unsigned where (self < 10u);
verified unsigned count() ensures (true) {
    std::vector<Digit> digits{3u, 9u};
    unsigned n = 0u;
    for (Digit d : digits) { n += d; }
    return n;
}
int main() { return count() == 12u ? 0 : 1; }
CPP
refuse a_refined_loop_variable_from_any_element "this value is not shown to satisfy refinement type 'Digit'" <<'CPP'
#include <vector>
type Digit = unsigned where (self < 10u);
verified unsigned count(const std::vector<unsigned>& v) ensures (true) {
    unsigned n = 0u;
    for (Digit d : v) { n += d; }
    return n;
}
int main() { return 0; }
CPP

# A span parameter's elements are read under its capability, and no reference
# is bound to one.
refuse a_span_read_without_its_capability "reading an element of 's' requires 'readable(s)'" <<'CPP'
#include <span>
verified unsigned total(std::span<const unsigned> s) ensures (true) {
    unsigned sum = 0u;
    for (unsigned x : s) { sum += x; }
    return sum;
}
int main() { return 0; }
CPP
refuse a_reference_to_a_span_parameter_element "binds an element of span parameter 's'" <<'CPP'
#include <span>
verified unsigned clear(std::span<unsigned> s) expects (writable(s)) ensures (true) {
    for (unsigned& x : s) { x = 0u; }
    return 0u;
}
int main() { return 0; }
CPP

# A written measure is checked, not trusted.
refuse a_written_measure_that_does_not_descend "is not shown to decrease on every iteration" <<'CPP'
#include <vector>
verified unsigned total(const std::vector<unsigned>& v) ensures (true) {
    unsigned sum = 0u;
    for (unsigned x : v) decreases (5u) { sum += x; }
    return sum;
}
int main() { return 0; }
CPP

# What is not modeled is refused by name.
refuse a_range_of_a_type_not_modeled "which is not a vector, a string, a span or an array this implementation models" <<'CPP'
struct Bag {
    unsigned items[2];
    unsigned* begin() { return items; }
    unsigned* end() { return items + 2; }
};
verified unsigned total() ensures (true) {
    Bag bag{{1u, 2u}};
    unsigned sum = 0u;
    for (unsigned x : bag) { sum += x; }
    return sum;
}
int main() { return 0; }
CPP
refuse a_range_that_is_not_a_name "is not a name: a range-based for is modeled over a vector, a string or a span this body names, or an array local" <<'CPP'
#include <vector>
verified unsigned total() ensures (true) {
    unsigned sum = 0u;
    for (unsigned x : std::vector<unsigned>{1u, 2u}) { sum += x; }
    return sum;
}
int main() { return 0; }
CPP
refuse a_range_with_an_initialization_statement "a range-based for with an initialization statement before its loop variable is not modeled" <<'CPP'
#include <vector>
verified unsigned total(const std::vector<unsigned>& v) ensures (true) {
    unsigned sum = 0u;
    for (unsigned k = 1u; unsigned x : v) { sum += x + k; }
    return sum;
}
int main() { return 0; }
CPP
# An initialization statement libclang does not expose would be a write the
# model never saw: `total` is 5 here, never 0.
refuse a_range_initializing_a_local "a range-based for with an initialization statement before its loop variable is not modeled" <<'CPP'
#include <vector>
verified unsigned total(const std::vector<unsigned>& v) ensures (result == 0u) {
    unsigned sum = 0u;
    for (sum = 5u; unsigned x : v) { }
    return sum;
}
int main() { return 0; }
CPP
refuse a_range_of_aggregates "which a range-based for does not bind" <<'CPP'
struct Point {
    unsigned x;
    unsigned y;
};
verified unsigned total() ensures (true) {
    Point points[2] = {{1u, 2u}, {3u, 4u}};
    unsigned sum = 0u;
    for (const Point& p : points) { sum += p.x; }
    return sum;
}
int main() { return 0; }
CPP

echo "a range-based for owes what C++ iteration does, and what it does not model is refused by name"
