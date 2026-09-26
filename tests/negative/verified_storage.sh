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
# (SPEC.md 12.10 VERIFIED-038, RFC 0014 §17 step 7). Both sides are values, so
# the kernel proves it rather than the access being trusted.
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
# (RFC 0014 §17 step 7, SPEC.md 12.10 VERIFIED-038).
#
# The sized form `readable(a, n)` states the region's extent, so an index into
# it owes `index < n`. The capability permits reaching the storage; it never
# decides which element the subscript names. Here nothing relates `i` to `n`,
# so the bound is unproven and the access is refused: were it admitted,
# `readable` would grant access to memory past the region it names.
# SPEC: VERIFIED-038, VERIFIED-043
reject a_capability_does_not_bound_a_symbolic_index "element index' is not proven" <<'CPP'
verified unsigned f(unsigned* a, unsigned n, unsigned i) expects (readable(a, n)) ensures (result == result) {
    return a[i];
}
CPP

# The same holds for an index that is constant. `999 < n` is not proven by `999`
# being a literal, so the obligation is the one a variable index owes and it
# fails the same way.
# SPEC: VERIFIED-038
reject a_capability_does_not_bound_a_constant_index "element index' is not proven" <<'CPP'
verified unsigned f(unsigned* a, unsigned n) expects (readable(a, n)) ensures (result == result) {
    return a[999u];
}
CPP

# The unsized form names a single object, so it states no extent at all. There
# is no bound to compare an index against, and an unstated extent is not an
# unbounded one: the access fails closed rather than treating the absent extent
# as permission to reach any element.
# SPEC: VERIFIED-038, VERIFIED-043
reject an_unsized_capability_does_not_cover_an_element 'the one-object form bounds no element' <<'CPP'
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
# the obligation layer (RFC 0014 §10). A contract states one `expects` clause,
# so the two may be conjoined there, and the clause is read apart: it states
# both, and a caller owes both (SPEC.md STDMODEL-016). A caller holding the
# capability and not proving the predicate is refused, and so is one proving
# the predicate and holding no capability.
# SPEC: STDMODEL-016
reject a_conjoined_predicate_is_still_owed 'call-site precondition' <<'CPP'
verified int read(int* p, int n) expects (readable(p) && n > 0) ensures (result == result) { return *p; }
verified int f(int* p) expects (readable(p)) ensures (result == result) { return read(p, 0); }
CPP
reject a_conjoined_capability_is_still_owed "requires 'readable\(p\)'" <<'CPP'
verified int read(int* p, int n) expects (readable(p) && n > 0) ensures (result == result) { return *p; }
verified int f(int* p) ensures (result == result) { return read(p, 1); }
CPP
reject a_capability_does_not_combine_by_disjunction "combines only with '&&'" <<'CPP'
verified int f(int* p, int* q) expects (readable(p) || readable(q)) ensures (result == result) { return *p; }
CPP

# A symbolic element place is the storage its index selects, so two subscripts
# are one place only when their indices are one value. A path records that a
# step was symbolic and not which element it chose, so matching on the path
# alone would make every symbolic subscript of one array the same place: a write
# at `i` would become a fact about `j`, which is false wherever they differ
# (RFC 0014 §4, SPEC.md 12.10).
# SPEC: STORAGE-010
reject a_write_at_one_index_is_not_a_fact_at_another 'does not satisfy its contract' <<'CPP'
verified int f(unsigned i, unsigned j) expects (i < 3u && j < 3u) ensures (result == 7) {
    int a[3] = {1, 1, 1};
    a[i] = 7;
    return a[j];
}
CPP

# The same holds at depth: an array that is a member is reached by a longer
# path, and the symbolic step at its end identifies an element no differently.
reject a_member_element_write_is_not_a_fact_at_another 'does not satisfy its contract' <<'CPP'
struct Holder { int items[3]; };
verified int f(unsigned i, unsigned j) expects (i < 3u && j < 3u) ensures (result == 7) {
    Holder h = {{1, 1, 1}};
    h.items[i] = 7;
    return h.items[j];
}
CPP

# Nothing relates two symbolic reads of one array either: distinct indices may
# select distinct elements, so their values are not known equal.
reject two_symbolic_reads_are_not_known_equal 'does not satisfy its contract' <<'CPP'
verified int f(unsigned i, unsigned j) expects (i < 3u && j < 3u) ensures (result == 0) {
    int a[3] = {1, 2, 3};
    return a[i] == a[j] ? 0 : 1;
}
CPP

# The index is a term read at the versions current where the subscript stands,
# so writing the index names a different element afterwards. A place matched by
# spelling would survive the write and carry the old element's value.
reject a_reassigned_index_names_another_element 'does not satisfy its contract' <<'CPP'
verified int f(unsigned i) expects (i < 3u) ensures (result == 7) {
    int a[3] = {1, 1, 1};
    a[i] = 7;
    i = 0u;
    return a[i];
}
CPP

# The same holds through a capability: `p[i]` and `p[j]` are two places of the
# pointee region, so a write through one proves nothing about the other.
reject a_pointee_write_at_one_index_is_not_a_fact_at_another 'does not satisfy its contract' <<'CPP'
verified int f(int* p, unsigned n, unsigned i, unsigned j)
    expects (writable(p, n) && readable(p, n))
    ensures (result == 7)
{
    if (i < n) {
        if (j < n) {
            p[i] = 7;
            return p[j];
        }
    }
    return 7;
}
CPP

# Each subscript forms its own place and owes the capability its own access
# needs. Writing `p[i]` establishes nothing about `p[j]`, so reading that
# element is a read of storage this body never wrote and requires `readable`,
# which `writable` does not entail.
# SPEC: VERIFIED-037, VERIFIED-043
reject a_written_element_does_not_grant_a_read_of_another "requires 'readable" <<'CPP'
verified int f(int* p, unsigned n, unsigned i, unsigned j) expects (writable(p, n)) ensures (result == result) {
    if (i < n) {
        if (j < n) {
            p[i] = 7;
            return p[j];
        }
    }
    return 0;
}
CPP

# Identity and aliasing answer opposite questions. Two selections whose indices
# are not established equal are distinct for carrying a fact, which is not a
# claim that they are disjoint storage: the write at `i` may be the write at
# `j`, so it invalidates the fact written there.
# SPEC: STORAGE-010
reject a_write_invalidates_a_fact_at_another_index 'does not satisfy its contract' <<'CPP'
verified int f(unsigned i, unsigned j) expects (i < 3u && j < 3u) ensures (result == 5) {
    int a[3] = {1, 1, 1};
    a[j] = 5;
    a[i] = 7;
    return a[j];
}
CPP

# A constant index states which element it selects and a term does not, so the
# two selections are never one place. They may still be one element, and the
# symbolic write invalidates the constant element's fact.
reject a_symbolic_write_invalidates_a_constant_element_fact 'does not satisfy its contract' <<'CPP'
verified int f(unsigned i) expects (i < 3u) ensures (result == 5) {
    int a[3] = {1, 1, 1};
    a[0] = 5;
    a[i] = 7;
    return a[0];
}
CPP

# Written-out fixtures, each refused for the stated reason, and the accepted
# halves of their matched pairs.
FIXTURES="$2"
refuse_fixture() {
    local name="$1" pattern="$2"
    if "$CPPL" -std=c++20 "$FIXTURES/negative/$name.cpp" -o "$run/$name" > "$run/$name.out" 2> "$run/$name.err"; then
        echo "accepted invalid storage reasoning: $name" >&2
        exit 1
    fi
    test ! -e "$run/$name"
    if grep -q PROVEN "$run/$name.out" "$run/$name.err" || ! grep -Eq "$pattern" "$run/$name.err"; then
        cat "$run/$name.err" >&2
        exit 1
    fi
}

# A member the representation cannot model leaves a gap in its components, and
# tracking the object anyway put one member's place under another's number: an
# access to `s.a` read what `s.b` held, and a claim false at run time was
# proven. Such an object is not tracked as places; its members are read by name.
# SPEC: STORAGE-002
refuse_fixture member_numbering_gap 'does not satisfy its contract'

# An object passed by reference may hold what another reference parameter
# designates, so a write through that parameter replaces what was known of the
# object: a member read after it was once the value it arrived with, and a
# claim false at run time was proven.
# SPEC: VERIFIED-030, VERIFIED-031
refuse_fixture reference_aggregate_stale 'does not satisfy its contract'

"$CPPL" -std=c++20 "$FIXTURES/untracked_members.cpp" -o "$run/untracked_members" > "$run/untracked_members.out"
test "$("$run/untracked_members")" = '2 3 1 5'
