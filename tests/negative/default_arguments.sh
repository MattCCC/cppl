#!/usr/bin/env bash
# Default arguments of verified functions that must be refused (SPEC.md R.16).
#
# SPEC: CONTRACTCOMP-002, EDGECASE-038, TUBOUND-003
# TRUST.md TCB-CALL-003, TCB-CALL-005
#
# A call relying on a default argument evaluates it where the call stands, so
# whatever the default owes is owed there, exactly as for the value written
# out: the callee's precondition, its refined parameter, the precondition of a
# call the default makes, the descent of a recursive call, and every rule of
# the body the call stands in. A default the bridge cannot evaluate at the
# call is refused naming the parameter and the function whose default it is.
#
# Each refused program has an accepted twin here that differs from it in one
# thing, so the refusal is shown to come from that thing. The refined
# parameter's twins are in tests/negative/refinement_types.sh, and the
# accepted programs these mirror in fixtures/equivalence/default_arguments.cpp.
set -euo pipefail
CPPL="$1"
WORK="$3"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/default-arguments-negative.XXXXXX")

fail() {
    echo "$1" >&2
    exit 1
}

# accept <name> [flags...]: the program on stdin verifies, and runs.
accept() {
    local name="$1"
    shift
    cat > "$run/$name.cpp"
    if ! "$CPPL" -std=c++17 "$@" "$run/$name.cpp" -o "$run/$name" > "$run/$name.log" 2>&1; then
        cat "$run/$name.log" >&2
        fail "valid source was refused: $name"
    fi
    "$run/$name"
}

# refuse <name> <diagnostic> [flags...]: the program on stdin is refused with
# the diagnostic, writes no program and reports nothing proven.
refuse() {
    local name="$1" expected="$2"
    shift 2
    cat > "$run/$name.cpp"
    if "$CPPL" -std=c++17 "$@" "$run/$name.cpp" -o "$run/$name" > "$run/$name.log" 2>&1; then
        cat "$run/$name.log" >&2
        fail "an invalid default argument was accepted: $name"
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

# A precondition the default does not satisfy is refused at the call relying
# on it.
accept a_default_inside_the_precondition <<'CPP'
verified int need(int p = 70) expects (p > 60) ensures (result == p) { return p; }
verified int relies() ensures (result == 70) { return need(); }
int main() { return relies() == 70 ? 0 : 1; }
CPP
refuse a_default_outside_the_precondition "call-site precondition for 'relies -> need' is not proven" <<'CPP'
verified int need(int p = 50) expects (p > 60) ensures (result == p) { return p; }
verified int relies() ensures (result == 50) { return need(); }
int main() { return 0; }
CPP

# The contract is instantiated at the default's value: a postcondition that
# only another default would establish is not proven.
accept a_postcondition_of_the_default <<'CPP'
verified int take(int p = 50) ensures (result == p) { return p; }
verified int relies() ensures (result == 50) { return take(); }
int main() { return relies() == 50 ? 0 : 1; }
CPP
refuse a_postcondition_of_another_default "verified function 'relies' does not satisfy its contract" <<'CPP'
verified int take(int p = 50) ensures (result == p) { return p; }
verified int relies() ensures (result == 51) { return take(); }
int main() { return 0; }
CPP

# A call the default makes owes its own precondition at the call relying on it.
accept a_default_calling_within_a_precondition <<'CPP'
verified pure int twice(int x) expects (x >= 0 && x < 1000) ensures (result == x + x) { return x + x; }
verified int doubled(int p = twice(4)) ensures (result == p) { return p; }
verified int relies() ensures (result == 8) { return doubled(); }
int main() { return relies() == 8 ? 0 : 1; }
CPP
refuse a_default_calling_outside_a_precondition "call-site precondition for 'relies -> twice' is not proven" <<'CPP'
verified pure int twice(int x) expects (x >= 0 && x < 1000) ensures (result == x + x) { return x + x; }
verified int doubled(int p = twice(4000)) ensures (result == p) { return p; }
verified int relies() ensures (result == 8000) { return doubled(); }
int main() { return 0; }
CPP

# A default calling a function that is not pure is refused, naming the
# function and the default it stands in.
accept a_default_calling_a_pure_function <<'CPP'
pure int seed() { return 5; }
verified int take(int p = seed()) ensures (result == p) { return p; }
verified int relies() ensures (result == 5) { return take(); }
int main() { return relies() == 5 ? 0 : 1; }
CPP
refuse a_default_calling_a_function_that_is_not_pure \
    "it calls a function that is not declared pure, so its value is not a mathematical function of its arguments: 'seed', called by the default argument of parameter 'p' of 'take'" <<'CPP'
int seed() { return 5; }
verified int take(int p = seed()) ensures (result == p) { return p; }
verified int relies() ensures (result == 5) { return take(); }
int main() { return 0; }
CPP

# A default the bridge cannot evaluate at the call is refused naming the
# parameter and the function whose default it is.
refuse a_default_reading_a_global \
    "the default argument of parameter 'p' of 'take', on which this call relies, is not modeled: 'level' is not a parameter or local" <<'CPP'
int level = 50;
verified int take(int p = level) ensures (result == p) { return p; }
verified int relies() ensures (result == 50) { return take(); }
int main() { return 0; }
CPP

# A reference parameter's default binds storage the call does not name. The
# same callee is verified at a call handing it a local.
accept a_reference_argument_written_out <<'CPP'
int shared = 5;
verified void bump(int& r = shared) expects (r >= 0 && r < 100) ensures (r < 101) { r = r + 1; }
verified int relies() ensures (result < 101) {
    int local = 6;
    bump(local);
    return local;
}
int main() { return relies() == 7 ? 0 : 1; }
CPP
refuse a_reference_argument_left_to_its_default \
    "the default argument of reference parameter 'r' of 'bump' binds storage this call does not name, which is not modeled" <<'CPP'
int shared = 5;
verified void bump(int& r = shared) expects (r >= 0 && r < 100) ensures (r < 101) { r = r + 1; }
verified int relies() ensures (result < 101) {
    bump();
    return 7;
}
int main() { return 0; }
CPP

# A recursive call relying on a default descends by the default's value.
accept a_recursive_default_that_descends <<'CPP'
verified unsigned walk(unsigned k = 0u) ensures (result == 0u) decreases (k) {
    if (k == 0u) {
        return 0u;
    }
    return walk();
}
int main() { return static_cast<int>(walk(4u)); }
CPP
refuse a_recursive_default_that_does_not_descend \
    "recursive call 'walk -> walk' is not shown to be made at a smaller measure than its caller was entered with" <<'CPP'
verified unsigned walk(unsigned k = 5u) ensures (result == 0u) decreases (k) {
    if (k == 0u) {
        return 0u;
    }
    return walk();
}
int main() { return 0; }
CPP

# A clause relying on a default states the clause at the default's value.
accept a_clause_relying_on_a_default <<'CPP'
pure unsigned add(unsigned x, unsigned k = 2u) { return x + k; }
verified unsigned bumped(unsigned x) ensures (result == add(x)) { return x + 2u; }
int main() { return bumped(3u) == 5u ? 0 : 1; }
CPP
refuse a_clause_relying_on_another_default "does not satisfy its contract" <<'CPP'
pure unsigned add(unsigned x, unsigned k = 3u) { return x + k; }
verified unsigned bumped(unsigned x) ensures (result == add(x)) { return x + 2u; }
int main() { return 0; }
CPP

# A ghost initializer calls what the defaults it relies on call: here a
# function that is verified and not pure, whose call would have to run.
accept a_ghost_relying_on_a_pure_default <<'CPP'
verified pure unsigned seven() ensures (result == 7u) { return 7u; }
pure unsigned keep(unsigned x, unsigned k = seven()) { return x + k; }
verified unsigned relies(unsigned x) ensures (result == x) {
    ghost unsigned g = keep(x);
    return x;
}
int main() { return relies(3u) == 3u ? 0 : 1; }
CPP
# A ghost initializer never runs, so the effects of a default it relies on would
# silently not happen.
refuse a_ghost_relying_on_a_default_with_an_effect \
    "the initializer of ghost 'g' has an increment or decrement in the default argument of parameter 'k' of 'keep'" <<'CPP'
unsigned counter = 0u;
pure unsigned keep(unsigned x, unsigned k = counter++) { return x + k; }
verified unsigned relies(unsigned x) ensures (result == x) {
    ghost unsigned g = keep(x);
    return x;
}
int main() { return 0; }
CPP
refuse a_ghost_relying_on_a_default_that_is_not_pure "the initializer of ghost 'g' calls 'seven', which is not pure" <<'CPP'
verified unsigned seven() ensures (result == 7u) { return 7u; }
pure unsigned keep(unsigned x, unsigned k = seven()) { return x + k; }
verified unsigned relies(unsigned x) ensures (result == x) {
    ghost unsigned g = keep(x);
    return x;
}
int main() { return 0; }
CPP

# A default whose end depends on lookup Clang has not done when its probes are
# written is refused at the declaration; parenthesized, it ends where it does.
accept a_template_id_default_in_parentheses <<'CPP'
template <unsigned A, unsigned B> verified pure unsigned first() ensures (result == A) { return A; }
verified unsigned picked(unsigned p = (first<4u, 5u>()), unsigned q = 3u) ensures (result == p + q) {
    return p + q;
}
verified unsigned relies() ensures (result == 7u) { return picked(); }
int main() { return relies() == 7u ? 0 : 1; }
CPP
refuse a_template_id_default_without_parentheses \
    "a default argument of verified function 'picked' is not delimited: a comma after a '<' it leaves open may separate the parameters or the arguments of a template-id" <<'CPP'
template <unsigned A, unsigned B> verified pure unsigned first() ensures (result == A) { return A; }
verified unsigned picked(unsigned p = first<4u, 5u>(), unsigned q = 3u) ensures (result == p + q) {
    return p + q;
}
verified unsigned relies() ensures (result == 7u) { return picked(); }
int main() { return 0; }
CPP

# Across translation units a caller relies on its own declaration's default:
# the contract the other unit proved is the same whatever default a
# declaration states, and the default it relies on is verified here.
cat > "$run/need.cpp" <<'CPP'
verified int need(int p) expects (p > 60) ensures (result == p) { return p; }
CPP
"$CPPL" -std=c++17 -c "$run/need.cpp" -o "$run/need.o" "--cppl-emit-interface=$run/need.cppli" \
    > "$run/need.log" 2>&1 || { cat "$run/need.log" >&2; fail "the defining unit was refused"; }
accept_unit() {
    local name="$1"
    cat > "$run/$name.cpp"
    if ! "$CPPL" -std=c++17 -c "$run/$name.cpp" -o "$run/$name.o" "--cppl-import-interface=$run/need.cppli" \
        > "$run/$name.log" 2>&1; then
        cat "$run/$name.log" >&2
        fail "valid source was refused: $name"
    fi
}
refuse_unit() {
    local name="$1" expected="$2"
    cat > "$run/$name.cpp"
    if "$CPPL" -std=c++17 -c "$run/$name.cpp" -o "$run/$name.o" "--cppl-import-interface=$run/need.cppli" \
        > "$run/$name.log" 2>&1; then
        cat "$run/$name.log" >&2
        fail "an invalid default argument was accepted: $name"
    fi
    [ ! -e "$run/$name.o" ] || fail "$name wrote an object"
    if ! grep -qF -- "$expected" "$run/$name.log"; then
        cat "$run/$name.log" >&2
        fail "$name was refused, but not because: $expected"
    fi
}
accept_unit a_declaration_default_inside_the_precondition <<'CPP'
verified int need(int p = 70) expects (p > 60) ensures (result == p);
verified int relies() ensures (result == 70) { return need(); }
CPP
refuse_unit a_declaration_default_outside_the_precondition "call-site precondition for 'relies -> need' is not proven" <<'CPP'
verified int need(int p = 50) expects (p > 60) ensures (result == p);
verified int relies() ensures (result == 50) { return need(); }
CPP

echo "a default argument owes at the call relying on it what the value written there would"
