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
type Positive = int where(self > 0);
verified int f() ensures(result > 0) { Positive x = 1; return x; }
CPP

# A verified function may state a refined return.
accept refined_return <<'CPP'
type Positive = int where(self > 0);
verified Positive f() ensures(result > 0) { return 1; }
CPP

# A refinement of a refinement keeps every predicate that applies (SPEC.md 17.5).
accept nested_refinement_composition <<'CPP'
type NonNegative = int where(self >= 0);
type Percentage = NonNegative where(self <= 100);
verified int f(Percentage p) ensures(result >= 0 && result <= 100) { return p; }
CPP

# A refined parameter's predicate is an entry fact; the author does not restate it.
accept refined_parameter_is_an_entry_fact <<'CPP'
type Positive = int where(self > 0);
verified int f(Positive x) ensures(result > 0) { return x; }
CPP

# Path conditions discharge a crossing the branch establishes (SPEC.md 17.3).
accept branch_establishes_membership <<'CPP'
type Percentage = int where(self >= 0 && self <= 100);
verified int f(int x) ensures(result >= 0) {
    if (x >= 0) { if (x <= 100) { Percentage p = x; return p; } }
    return 0;
}
CPP

# A loop may carry a refined local across iterations under its invariant.
accept refined_local_in_loop <<'CPP'
type Small = unsigned where(self < 10u);
verified unsigned f() ensures(result == 9u) {
    Small x = 0u;
    while (x < 9u) invariant(x <= 9u) { ++x; }
    return x;
}
CPP

# '&&' states a proposition in a contract clause.
accept conjunction_in_contract <<'CPP'
verified int f(int x) expects(x >= 0 && x <= 100) ensures(result >= 0) { return x; }
CPP

# A conditional expression is modeled where the path splits on it.
accept conditional_in_return <<'CPP'
verified int f(bool b) ensures(result > 0) { return b ? 1 : 2; }
CPP

# Binding a conditional to a local splits the route, so each arm is proved under
# what its own path supposes rather than as one opaque `select` term.
accept conditional_bound_to_a_local <<'CPP'
verified int f(bool b) ensures(result > 0) { int x = b ? 1 : 2; return x; }
CPP

# A refinement crossing may be discharged arm by arm.
accept refined_crossing_through_a_conditional <<'CPP'
type Positive = int where(self > 0);
verified int f(bool b) ensures(result > 0) { Positive x = b ? 1 : 2; return x; }
CPP

# An arm that is itself a conditional splits again.
accept nested_conditional_arms <<'CPP'
verified int f(bool a, bool b) ensures(result > 0) { int x = a ? (b ? 1 : 2) : 3; return x; }
CPP

# The condition's own facts are available inside the arm it guards.
accept condition_informs_its_arm <<'CPP'
verified int f(int y) ensures(result > 0) { int x = y > 0 ? y : 1; return x; }
CPP

# --- Crossings that must be refused ------------------------------------------

# An ordinary function's declaration is not proof of its refined return.
refuse unverified_refined_return 'ordinary function.*return cannot establish refinement' <<'CPP'
type Positive = int where(self > 0);
Positive f();
CPP

# A refined member's construction has no obligation, so the declaration is
# refused: a record is built by unverified code before it enters a verified body.
refuse refined_member 'outside a modeled verified body' <<'CPP'
type Positive = int where(self > 0);
struct S { Positive p; };
CPP

# A value that does not satisfy the predicate cannot enter the type.
refuse unproven_crossing 'not shown to satisfy refinement type' <<'CPP'
type Positive = int where(self > 0);
verified int f() ensures(result > 0) { Positive x = 0; return x; }
CPP

# A refinement is not established by naming a wider one.
refuse widening_is_not_proof 'not shown to satisfy refinement type' <<'CPP'
type NonNegative = int where(self >= 0);
type Positive = int where(self > 0);
verified int f(NonNegative x) ensures(result > 0) { Positive y = x; return y; }
CPP

# Splitting a conditional adds proof power, never a fact. Each arm must hold on
# its own path: one failing arm rejects the whole binding.
refuse conditional_true_arm_fails 'does not satisfy its contract' <<'CPP'
verified int f(bool b) ensures(result > 0) { int x = b ? 0 : 2; return x; }
CPP
refuse conditional_false_arm_fails 'does not satisfy its contract' <<'CPP'
verified int f(bool b) ensures(result > 0) { int x = b ? 1 : 0; return x; }
CPP
refuse conditional_arm_fails_refinement 'not shown to satisfy refinement type' <<'CPP'
type Positive = int where(self > 0);
verified int f(bool b) ensures(result > 0) { Positive x = b ? 1 : 0; return x; }
CPP

# A guarded arm supposes only what its condition states, never more.
refuse conditional_does_not_overreach 'does not satisfy its contract' <<'CPP'
verified int f(int y) ensures(result > 5) { int x = y > 0 ? y : 1; return x; }
CPP

# --- Gaps: refused today, and the reason must stay visible -------------------
#
# These are reasoning or modeling gaps, not soundness boundaries. Each is
# refused, which is the fail-closed direction. If one begins to verify, that is
# a deliberate improvement and this suite must be updated to `accept`.

# A conditional whose arm reads an earlier conditional local is not resolved
# transitively, so the second binding still sees an opaque term. Direct and
# nested conditional bindings do split and do verify (see above).
refuse chained_conditional_locals 'does not satisfy its contract' <<'CPP'
verified int f(bool a, bool b) ensures(result > 0) { int x = a ? 1 : 2; int y = b ? x : 3; return y; }
CPP

# '&&' is modeled in a contract clause but not as an if-condition, so a branch
# that would establish a two-sided predicate must be written as nested ifs.
refuse conjunction_as_an_if_condition "'&&' states a proposition" <<'CPP'
verified int f(int x) ensures(result >= 0) {
    if (x >= 0 && x <= 100) { return 1; }
    return 0;
}
CPP

# Casts are refused rather than silently preserving or dropping a refinement.
refuse cast_is_not_modeled 'only a scoped enum cast' <<'CPP'
type Positive = int where(self > 0);
verified int f(Positive x) ensures(result > 0) { return static_cast<int>(x); }
CPP

# An indexed refinement's application is not resolved by the bridge yet.
refuse indexed_refinement 'requires template arguments|unresolved' <<'CPP'
type Index(n) = unsigned where(self < n);
verified unsigned f(Index(10) i) ensures(result < 10u) { return i; }
CPP

# A lambda is not a modeled body.
refuse lambda_is_not_modeled 'cannot state as a value' <<'CPP'
verified int f(int x) ensures(result == x) { auto g = [](int v) { return v; }; return g(x); }
CPP

echo 'refinement flow: proven crossings and refused crossings both hold'
