#!/usr/bin/env bash
set -euo pipefail
CPPL="$1"
WORK="$3"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/rejected-storage.XXXXXX")
reject() {
    local name="$1" pattern="$2"
    cat > "$run/$name.cpp"
    if "$CPPL" -std=c++20 -c "$run/$name.cpp" -o "$run/$name.o" > "$run/$name.out" 2> "$run/$name.err"; then
        echo "accepted invalid storage reasoning: $name" >&2
        exit 1
    fi
    test ! -e "$run/$name.o"
    if ! grep -Eq "$pattern" "$run/$name.err"; then
        cat "$run/$name.err" >&2
        exit 1
    fi
}
reject stale_reference 'does not satisfy its contract' <<'CPP'
verified int f(int& x) expects (x > 0) ensures (result > 0) { x = 0; return x; }
CPP
reject stale_second_reference 'does not satisfy its contract' <<'CPP'
verified int f(int& x, const int& y) expects (y > 0) ensures (result > 0) { x = 0; return y; }
CPP
reject stale_reference_alias 'does not satisfy its contract' <<'CPP'
verified int f(int& x, const int& y) expects (y > 0) ensures (result > 0) { int& r = x; r = 0; return y; }
CPP
reject refined_reference_write 'not shown to satisfy refinement type' <<'CPP'
type Positive = int where (self > 0);
verified void f(Positive& x, int y) ensures (x > 0) { x = y; }
CPP
reject maybe_refined_alias 'not shown to satisfy refinement type' <<'CPP'
type Positive = int where (self > 0);
verified void f(int& x, const Positive& y) ensures (x == 0) { x = 0; }
CPP
reject stale_call 'does not satisfy its contract' <<'CPP'
verified void zero(int& x) ensures (x == 0) { x = 0; }
verified int f() ensures (result > 0) { int x = 1; zero(x); return x; }
CPP
reject unproved_call_crossing 'not shown to satisfy refinement type' <<'CPP'
type Positive = int where (self > 0);
verified void zero(int& x) ensures (x == 0) { x = 0; }
verified int f() ensures (result == 0) { Positive x = 1; zero(x); return x; }
CPP
reject call_cannot_prove_own_precondition 'call-site precondition' <<'CPP'
verified void positive(int& x) expects (x > 0) ensures (x > 0) { x = 1; }
verified int f() ensures (result == 1) { int x = 0; positive(x); return x; }
CPP
reject stale_external_call_alias 'does not satisfy its contract' <<'CPP'
verified void zero(int& x) ensures (x == 0) { x = 0; }
verified int f(int& x, const int& y) expects (y > 0) ensures (result > 0) { zero(x); return y; }
CPP
reject failed_void_callee 'does not satisfy its contract|unproven contract' <<'CPP'
verified void liar(int& x) ensures (x > 0) { x = 0; }
verified int f() ensures (result > 0) { int x = 0; liar(x); return x; }
CPP
reject early_return_not_checked 'does not satisfy its contract' <<'CPP'
verified void f(int& x, bool b) ensures (x > 0) { if (b) return; x = 1; }
CPP
reject result_not_in_void_scope 'undeclared identifier.*result' <<'CPP'
verified void f() ensures (result == 0) { return; }
CPP
reject const_is_clang_checked 'cpp-semantic' <<'CPP'
verified void f(const int& x) ensures (x == 0) { x = 0; }
CPP
reject loop_alias_fact 'does not satisfy its contract|invariant.*not' <<'CPP'
verified int f(unsigned& x, const unsigned& y) expects (y > 0u) ensures (result > 0) {
    while (x < 1u) invariant (x <= 1u) { x = 1u; }
    if (y > 0u) return 1;
    return 0;
}
CPP
# A dereference requires a memory capability, and non-nullness is not one. A
# non-null precondition is necessary and insufficient, and the pointer's state
# model may never supply the difference (SPEC.md VERIFIED-037), so every
# dereference form below stays refused for want of the capability itself.
reject pointer_read "requires 'readable" <<'CPP'
verified int f(int* p) expects (p != nullptr) ensures (result == 0) { return *p; }
CPP
reject pointer_write "requires 'writable" <<'CPP'
verified void f(int* p) expects (p != nullptr) ensures (true) { *p = 0; }
CPP
reject pointer_member "requires 'readable" <<'CPP'
struct S { int m; };
verified int f(S* p) expects (p != nullptr) ensures (result == 0) { return p->m; }
CPP
reject pointer_subscript "requires 'readable" <<'CPP'
verified int f(int* p) expects (p != nullptr) ensures (result == 0) { return p[0]; }
CPP
# A pointer computed by arithmetic names storage this implementation cannot
# identify, so it is refused whatever capability is in scope: the capability
# names a place, and there is no place here to name.
reject pointer_arithmetic_write 'only a local variable is assigned|capability|cannot identify' <<'CPP'
verified void f(int* p) expects (writable(p)) ensures (true) { *(p + 1) = 0; }
CPP

# A capability is not symmetric. Reading is not permission to write, and writing
# is not permission to read: an output buffer may be writable and not readable
# (RFC 0014 §3).
reject readable_does_not_permit_a_write "requires 'writable" <<'CPP'
verified void f(int* p) expects (readable(p)) ensures (true) { *p = 0; }
CPP
reject writable_does_not_permit_a_read "requires 'readable" <<'CPP'
verified int f(int* p) expects (writable(p)) ensures (result == result) { return *p; }
CPP

# A capability names one pointer's storage. Holding it for one pointer says
# nothing about another.
reject a_capability_does_not_transfer_to_another_pointer "requires 'readable" <<'CPP'
verified int f(int* p, int* q) expects (readable(p)) ensures (result == result) { return *q; }
CPP

# A write through a pointer to refined storage owes the predicate at the
# pointee's own place, exactly as a write to a refined local does
# (SPEC.md REFINEOBL-007).
reject refined_pointee_write 'not shown to satisfy refinement type' <<'CPP'
type Positive = int where (self > 0);
verified void f(Positive* p) expects (writable(p)) ensures (true) { *p = 0; }
CPP

# A symbolic subscript owes `index < extent`, and the extent is the array's own
# (SPEC.md 12.10 VERIFIED-038, RFC 0014 §7). Both sides are values, so the
# kernel proves it rather than the access being trusted.
reject symbolic_index_owes_its_bound "element index' is not proven" <<'CPP'
verified unsigned f(unsigned i) ensures (result == result) {
    unsigned a[4] = {0u, 1u, 2u, 3u};
    return a[i];
}
CPP

# Two symbolic indices are disjoint only when proved unequal. Nothing here
# proves `i != j`, so the write may hit the element already read and the earlier
# fact does not survive it (RFC 0014 §4).
reject symbolic_indices_are_not_assumed_distinct 'does not satisfy its contract' <<'CPP'
verified unsigned f(unsigned i, unsigned j) expects (i < 4u && j < 4u) ensures (result == 1u) {
    unsigned a[4] = {1u, 1u, 1u, 1u};
    unsigned seen = a[i];
    a[j] = 0u;
    return seen;
}
CPP

# Two dereferences may designate one object, so a write through either
# invalidates what was known through the other. Nothing here proves `p` and `q`
# distinct, and distinctness is proved, never assumed (RFC 0014 §4).
reject a_pointer_write_invalidates_another_pointee 'does not satisfy its contract' <<'CPP'
verified int f(int* p, int* q) expects (readable(p) && writable(q)) ensures (result > 0) {
    int seen = *p;
    *q = 0;
    return seen > 0 ? *p : 1;
}
CPP

# A capability comes from the recognized `readable`/`writable` form, never from
# text that resembles what the projection emits for one. The probe happens to be
# a lambda over the pointer, so writing that shape by hand is the obvious
# forgery to try: it must grant nothing, and the dereference must still owe its
# capability (SPEC.md 12.10 VERIFIED-043, TRUST.md TCB-CAP-003).
# SPEC: VERIFIED-043
reject a_lambda_shaped_like_a_probe_grants_no_capability "requires 'readable" <<'CPP'
verified unsigned f(unsigned* p)
    expects (([](auto&& cppl_place) { return true; })(p))
    ensures (result == result)
{
    return *p;
}
CPP

# A callee taking a pointer to non-const may write through it, so what the
# caller knew about the pointee does not survive the call. The callee's contract
# says nothing about preserving it, and "it was not mentioned" is not evidence
# that it is unchanged (SPEC.md 12.10 VERIFIED-040, VERIFIED-041).
#
# This is the pointer twin of the by-reference case: a pointer is passed by
# value, so the parameter keeps its own version while the storage it designates
# goes stale. Missing that distinction once made this exact program verify while
# returning 0 from a contract promising a positive result.
# SPEC: VERIFIED-040, VERIFIED-041
reject a_call_through_a_pointer_invalidates_the_pointee 'does not satisfy its contract' <<'CPP'
verified void touch(int* q) expects (writable(q)) ensures (true) { *q = 0; }
verified int f(int* p) expects (readable(p) && writable(p)) ensures (result > 0) {
    int seen = *p;
    touch(p);
    return seen > 0 ? *p : 1;
}
CPP

# What a caller may rely on is exactly the callee's `ensures`, never more. The
# callee here promises only that its result is non-negative, so the crossing
# into a type requiring a positive value is not discharged by the call.
# SPEC: REFINEOBL-004
reject a_callers_fact_is_only_the_callees_ensures 'not shown to satisfy refinement type' <<'CPP'
type Positive = int where (self > 0);
verified int weak(int x) expects (x >= 0) ensures (result >= 0) { return x; }
verified int f(int x) expects (x >= 0) ensures (result > 0) {
    Positive p = weak(x);
    return p;
}
CPP

# A capability permits reaching a pointer's storage; it does not decide which
# element of that storage a subscript names. The index still owes
# `index < extent`, exactly as a subscript of a local array does
# (RFC 0014 §7, SPEC.md 12.10 VERIFIED-038).
#
# The sized form `readable(a, n)` states the region's extent, so an index into
# it must be proved to lie within `n`. That obligation is not implemented, and
# an unimplemented obligation must refuse the access rather than permit it: a
# capability that silently admitted any index would make `readable` grant
# unbounded access to memory past the region it names.
# SPEC: VERIFIED-038, VERIFIED-043
reject a_capability_does_not_bound_a_symbolic_index 'extent|element index|not implemented' <<'CPP'
verified unsigned f(unsigned* a, unsigned n, unsigned i) expects (readable(a, n)) ensures (result == result) {
    return a[i];
}
CPP

# The same holds for an index that is constant and plainly outside the stated
# extent. Nothing relates `999` to `n`, so the access is refused for want of
# the bound rather than accepted because the index happens to be a literal.
# SPEC: VERIFIED-038
reject a_capability_does_not_bound_a_constant_index 'extent|element index|not implemented' <<'CPP'
verified unsigned f(unsigned* a, unsigned n) expects (readable(a, n)) ensures (result == result) {
    return a[999u];
}
CPP

# The unsized form names a single object, so a subscript of it past element
# zero reaches storage the capability never described.
# SPEC: VERIFIED-038
reject an_unsized_capability_does_not_cover_an_element 'extent|element index|not implemented' <<'CPP'
verified unsigned f(unsigned* p, unsigned i) expects (readable(p)) ensures (result == result) {
    return p[i];
}
CPP

# This implementation spells a capability over the pointer, `readable(p)`, while
# RFC 0014 §12 spells the same capability over the place, `readable(*p)`. The
# difference is one character and the meaning is identical, so the place form is
# refused by name. Projected as written it would become a dereference -- the very
# thing the capability exists to permit -- and the author would be told that a
# capability they spelled as the RFC specifies established nothing.
reject a_capability_names_its_pointer_not_a_dereference 'names the pointer whose storage it describes' <<'CPP'
verified int f(int* p) expects (readable(*p)) ensures (result == result) { return *p; }
CPP

# A capability and an ordinary predicate travel on different channels: only the
# predicate reaches the kernel, while the capability is a context hypothesis of
# the obligation layer (RFC 0014 §10). Conjoining them in one clause would put a
# term the kernel never sees inside a proposition it is asked to prove, so the
# two are kept apart. A contract states one `expects` clause, so several
# capabilities necessarily arrive joined by `&&`, and that stays legal.
reject a_capability_does_not_conjoin_with_a_predicate 'belong in separate clauses' <<'CPP'
verified int f(int* p, int n) expects (readable(p) && n > 0) ensures (result == result) { return *p; }
CPP
reject a_capability_does_not_combine_by_disjunction "combines only with '&&'" <<'CPP'
verified int f(int* p, int* q) expects (readable(p) || readable(q)) ensures (result == result) { return *p; }
CPP
