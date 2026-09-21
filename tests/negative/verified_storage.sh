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
verified int f(int& x) expects(x > 0) ensures(result > 0) { x = 0; return x; }
CPP
reject stale_second_reference 'does not satisfy its contract' <<'CPP'
verified int f(int& x, const int& y) expects(y > 0) ensures(result > 0) { x = 0; return y; }
CPP
reject stale_reference_alias 'does not satisfy its contract' <<'CPP'
verified int f(int& x, const int& y) expects(y > 0) ensures(result > 0) { int& r = x; r = 0; return y; }
CPP
reject refined_reference_write 'not shown to satisfy refinement type' <<'CPP'
type Positive = int where(self > 0);
verified void f(Positive& x, int y) ensures(x > 0) { x = y; }
CPP
reject maybe_refined_alias 'not shown to satisfy refinement type' <<'CPP'
type Positive = int where(self > 0);
verified void f(int& x, const Positive& y) ensures(x == 0) { x = 0; }
CPP
reject stale_call 'does not satisfy its contract' <<'CPP'
verified void zero(int& x) ensures(x == 0) { x = 0; }
verified int f() ensures(result > 0) { int x = 1; zero(x); return x; }
CPP
reject unproved_call_crossing 'not shown to satisfy refinement type' <<'CPP'
type Positive = int where(self > 0);
verified void zero(int& x) ensures(x == 0) { x = 0; }
verified int f() ensures(result == 0) { Positive x = 1; zero(x); return x; }
CPP
reject call_cannot_prove_own_precondition 'call-site precondition' <<'CPP'
verified void positive(int& x) expects(x > 0) ensures(x > 0) { x = 1; }
verified int f() ensures(result == 1) { int x = 0; positive(x); return x; }
CPP
reject stale_external_call_alias 'does not satisfy its contract' <<'CPP'
verified void zero(int& x) ensures(x == 0) { x = 0; }
verified int f(int& x, const int& y) expects(y > 0) ensures(result > 0) { zero(x); return y; }
CPP
reject failed_void_callee 'does not satisfy its contract|unproven contract' <<'CPP'
verified void liar(int& x) ensures(x > 0) { x = 0; }
verified int f() ensures(result > 0) { int x = 0; liar(x); return x; }
CPP
reject early_return_not_checked 'does not satisfy its contract' <<'CPP'
verified void f(int& x, bool b) ensures(x > 0) { if (b) return; x = 1; }
CPP
reject result_not_in_void_scope 'undeclared identifier.*result' <<'CPP'
verified void f() ensures(result == 0) { return; }
CPP
reject const_is_clang_checked 'cpp-semantic' <<'CPP'
verified void f(const int& x) ensures(x == 0) { x = 0; }
CPP
reject loop_alias_fact 'does not satisfy its contract|invariant.*not' <<'CPP'
verified int f(unsigned& x, const unsigned& y) expects(y > 0u) ensures(result > 0) {
    while (x < 1u) invariant(x <= 1u) { x = 1u; }
    if (y > 0u) return 1;
    return 0;
}
CPP
# Dereference awaits the memory-validity obligations of RFC 0014. A non-null
# precondition is necessary and insufficient, and the pointer's state model may
# never supply the difference, so every dereference form stays refused.
reject pointer_read 'memory-validity obligations of RFC 0014' <<'CPP'
verified int f(int* p) expects(p != nullptr) ensures(result == 0) { return *p; }
CPP
reject pointer_write 'memory-validity obligations of RFC 0014' <<'CPP'
verified void f(int* p) expects(p != nullptr) ensures(true) { *p = 0; }
CPP
reject pointer_member 'not modeled|memory-validity' <<'CPP'
struct S { int m; };
verified int f(S* p) expects(p != nullptr) ensures(result == 0) { return p->m; }
CPP
reject pointer_subscript 'not modeled|memory-validity' <<'CPP'
verified int f(int* p) expects(p != nullptr) ensures(result == 0) { return p[0]; }
CPP
reject pointer_arithmetic_write 'memory-validity obligations of RFC 0014' <<'CPP'
verified void f(int* p) expects(p != nullptr) ensures(true) { *(p + 1) = 0; }
CPP
