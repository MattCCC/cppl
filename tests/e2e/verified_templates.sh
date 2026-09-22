#!/usr/bin/env bash
# Verified function templates, checked per specialization (SPEC.md 42).
#
# A contract on a template is parameterized by the template's own parameters and
# means what it means after substitution. Clang performs that substitution and
# selects the specialization; what is checked here is each specialization it
# produced, with its own instantiated contract and its own proof identity
# (TEMPLATE-001, TEMPLATE-003).
set -euo pipefail
CPPL="$1"
WORK="$3"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/verified-templates.XXXXXX")

# A program this implementation must accept, with the number of contracts it
# must report proven. The count is asserted because a specialization that was
# silently not checked would otherwise look exactly like one that passed.
accept() {
    local name="$1" proven="$2"
    cat > "$run/$name.cpp"
    if ! "$CPPL" -std=c++17 "$run/$name.cpp" -o "$run/$name" --cppl-trust-report \
        > "$run/$name.log" 2>&1; then
        echo "a valid verified template was refused: $name" >&2
        cat "$run/$name.log" >&2
        exit 1
    fi
    if ! grep -Eq "^Function contracts proven: +$proven\$" "$run/$name.log"; then
        echo "expected $proven proven contracts in $name" >&2
        grep -E 'contracts proven|Unresolved' "$run/$name.log" >&2
        exit 1
    fi
    if ! grep -Eq '^Unresolved obligations: +0$' "$run/$name.log"; then
        echo "unresolved obligations remained in $name" >&2
        exit 1
    fi
}

# A program this implementation must refuse, and the reason it must give.
refuse() {
    local name="$1" pattern="$2"
    cat > "$run/$name.cpp"
    if "$CPPL" -std=c++17 "$run/$name.cpp" -o "$run/$name" > "$run/$name.log" 2>&1; then
        echo "an unproven verified template was accepted: $name" >&2
        cat "$run/$name.log" >&2
        exit 1
    fi
    test ! -e "$run/$name"
    if ! grep -Eq "$pattern" "$run/$name.log"; then
        echo "wrong reason for $name" >&2
        cat "$run/$name.log" >&2
        exit 1
    fi
}

# One specialization, one obligation. The contract is stated over `N` and means
# `x < 4 -> result < 4` here, because that is what Clang substituted.
accept one_specialization 1 <<'CPP'
template <unsigned N>
verified unsigned clamp_to(unsigned x) expects (x < N) ensures (result < N) {
    return x;
}
int main() { return static_cast<int>(clamp_to<4u>(3u)); }
CPP

# Each specialization is its own function to check. Three instantiations are
# three obligations, not one obligation proved once and reused.
# SPEC: TEMPLATE-001
accept every_specialization_is_checked 3 <<'CPP'
template <unsigned N>
verified unsigned clamp_to(unsigned x) expects (x < N) ensures (result < N) {
    return x;
}
int main() {
    return static_cast<int>(clamp_to<4u>(3u) + clamp_to<8u>(7u) + clamp_to<16u>(1u));
}
CPP

# A type parameter substitutes the same way: the parameter and result types of
# the specialization are the instantiated ones, and the contract is read at
# those types.
accept a_type_parameter_specializes 2 <<'CPP'
template <typename T>
verified T identity(T x) ensures (result == x) {
    return x;
}
int main() {
    return static_cast<int>(identity<int>(1)) + static_cast<int>(identity<unsigned>(2u));
}
CPP

# The same template may be instantiated at arguments that make its contract true
# in one specialization and false in another. Only the false one is refused, and
# the proof of the true one never discharges it (TEMPLATE-003).
# SPEC: TEMPLATE-003
refuse one_specialization_does_not_prove_another 'does not satisfy its contract' <<'CPP'
template <unsigned N>
verified unsigned pick(unsigned x) expects (x < N) ensures (result < 4u) {
    return x;
}
int main() { return static_cast<int>(pick<4u>(3u) + pick<9u>(1u)); }
CPP

# The goal a failing specialization reports is stated at its own arguments, so
# the diagnostic names the instantiation that is actually wrong.
refuse a_false_contract_is_refused_at_its_arguments 'lt:u32\(#0, 9:u32\)' <<'CPP'
template <unsigned N>
verified unsigned pick(unsigned x) expects (x < N) ensures (result < 4u) {
    return x;
}
int main() { return static_cast<int>(pick<9u>(1u)); }
CPP

# A template that is never instantiated has no specialization, so there is no
# obligation and equally nothing proven. Reporting it as verified would claim a
# result no specialization established.
# SPEC: TEMPLATE-001
refuse an_uninstantiated_template_proves_nothing 'is not instantiated in this translation unit' <<'CPP'
template <unsigned N>
verified unsigned unused(unsigned x) expects (x < N) ensures (result < N) {
    return x;
}
int main() { return 0; }
CPP

# A refinement type applied at a template's own parameter is an ordinary
# instantiated type: `Index<N>` at `N = 4` is the refinement `self < 4`, and the
# result owes that predicate.
# SPEC: TEMPLATE-001, REFINE-008
accept an_indexed_refinement_in_a_template 1 <<'CPP'
type Index(unsigned n) = unsigned where (self < n);
template <unsigned N>
verified Index<N> make_index(unsigned x) expects (x < N) {
    return x;
}
int main() { return static_cast<int>(make_index<4u>(3u)); }
CPP

# The refinement is owed at the specialization's own argument, so a value that
# would satisfy a wider bound does not enter the narrower one.
refuse an_indexed_refinement_owes_its_own_bound 'does not satisfy its contract' <<'CPP'
type Index(unsigned n) = unsigned where (self < n);
template <unsigned N>
verified Index<N> make_index(unsigned x) expects (x < N + 1u) {
    return x;
}
int main() { return static_cast<int>(make_index<4u>(3u)); }
CPP

# A refined parameter crossing into a specialization carries its predicate the
# same way an ordinary one does.
accept a_refined_parameter_in_a_template 1 <<'CPP'
type Positive = int where (self > 0);
template <typename T>
verified int at_least_one(Positive p) ensures (result > 0) {
    return p;
}
int main() { return at_least_one<int>(5); }
CPP

# A verified specialization calling another verified specialization composes
# through contracts: the caller owes the callee's precondition at its own
# arguments and may use only what that specialization's postcondition states.
# SPEC: TEMPLATE-001
accept a_specialization_calls_a_specialization 2 <<'CPP'
template <unsigned N>
verified unsigned checked(unsigned x) expects (x < N) ensures (result < N) {
    return x;
}
template <unsigned N>
verified unsigned uses(unsigned x) expects (x < N) ensures (result < N) {
    return checked<N>(x);
}
int main() { return static_cast<int>(uses<4u>(3u)); }
CPP

# The callee's precondition is owed at the call, and a caller that cannot prove
# it is refused even though the callee itself verifies.
refuse a_call_owes_the_callee_precondition 'call-site precondition' <<'CPP'
template <unsigned N>
verified unsigned checked(unsigned x) expects (x < N) ensures (result < N) {
    return x;
}
template <unsigned N>
verified unsigned uses(unsigned x) ensures (result < N) {
    return checked<N>(x);
}
int main() { return static_cast<int>(uses<4u>(3u)); }
CPP

# An array parameter whose extent is the template's own parameter has, in each
# specialization, the extent Clang substituted. The subscript owes `i < N`
# against that extent.
# SPEC: TEMPLATE-001, STORAGE-005
accept a_template_array_extent_bounds_a_subscript 1 <<'CPP'
template <unsigned N>
verified unsigned first(unsigned i) expects (i < N) ensures (result == result) {
    unsigned a[4] = {0u, 1u, 2u, 3u};
    return a[i];
}
int main() { return static_cast<int>(first<4u>(3u)); }
CPP

# Without the bound the same subscript is refused, so the extent is proved
# rather than assumed from the array's presence.
refuse a_template_array_subscript_owes_its_bound "element index' is not proven" <<'CPP'
template <unsigned N>
verified unsigned first(unsigned i) ensures (result == result) {
    unsigned a[4] = {0u, 1u, 2u, 3u};
    return a[i];
}
int main() { return static_cast<int>(first<4u>(3u)); }
CPP

# A dependent extent: the array's size is the template parameter itself, so
# there is no literal to compare against until Clang substitutes one. The
# extent comes from each specialization's resolved type, and no element of the
# array is observed before the symbolic subscript.
# SPEC: TEMPLATE-001, STORAGE-005
accept a_dependent_array_extent_bounds_a_symbolic_subscript 1 <<'CPP'
template <unsigned N>
verified unsigned get(const unsigned (&a)[N], unsigned i)
    expects (i < N)
    ensures (result == result)
{
    return a[i];
}
int main() { unsigned b[4] = {}; return static_cast<int>(get<4u>(b, 0u)); }
CPP

# Two specializations of one template, each bounded by its own extent. A single
# shared bound would prove one of them wrong.
# SPEC: TEMPLATE-001, TEMPLATE-003
accept two_dependent_extents_are_bounded_separately 2 <<'CPP'
template <unsigned N>
verified unsigned get(const unsigned (&a)[N], unsigned i)
    expects (i < N)
    ensures (result == result)
{
    return a[i];
}
int main() {
    unsigned small[4] = {};
    unsigned large[8] = {};
    return static_cast<int>(get<4u>(small, 0u) + get<8u>(large, 0u));
}
CPP

# The premise must bound the index by *this* specialization's extent. `i < 8`
# is true of the `<8>` specialization and not of the `<4>` one, so a template
# stating it is refused exactly where it is false.
# SPEC: TEMPLATE-003, STORAGE-005
refuse a_wider_bound_does_not_carry_to_a_narrower_extent "element index' is not proven" <<'CPP'
template <unsigned N>
verified unsigned get(const unsigned (&a)[N], unsigned i)
    expects (i < 8u)
    ensures (result == result)
{
    return a[i];
}
int main() {
    unsigned b[4] = {};
    return static_cast<int>(get<4u>(b, 0u));
}
CPP

# A C++ constraint controls which specialization Clang selects; it is not a
# formal premise. A contract that would need the constraint as a theorem is
# refused, so satisfaction never becomes evidence (TEMPLATE-002).
# SPEC: TEMPLATE-002
refuse a_constraint_is_not_a_theorem 'does not satisfy its contract' <<'CPP'
template <typename T>
verified unsigned bounded(unsigned x) ensures (result < 10u) {
    return x;
}
int main() { return static_cast<int>(bounded<int>(3u)); }
CPP

echo 'verified templates are checked per specialization'
