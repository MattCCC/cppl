#!/usr/bin/env bash
# SPEC: STDMODEL-011, STDMODEL-012, STDMODEL-013, STDMODEL-015, STDMODEL-016, STDMODEL-017, STDMODEL-020
# SPEC: STDMODEL-021, STDMODEL-023, STDMODEL-027
# TRUST.md TCB-LIB-006
#
# The accepted twins of `negative/sequence_boundaries.sh`: each differs from a
# refused case in the one step that makes that case unsound, is proven, and
# computes the result its contract states.
set -euo pipefail
CPPL="$1"
WORK="$3"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/sequence-boundaries.XXXXXX")
fail() {
    echo "$1" >&2
    exit 1
}

# A unit gathers accepted cases, each a verified function named for its case.
# Every verified function in the unit must be proven, with no obligation left
# unresolved; the program is built and run, and each case whose contract
# states its exact result must compute it.
unit=""
calls=""
expected=""
begin() {
    unit="$1"
    calls=""
    expected=""
    prelude > "$run/$unit.cpp"
}
# accepted <name> [<result>]: the case's function follows on stdin. A case
# given a result takes no argument and its contract states that result.
accepted() {
    cat >> "$run/$unit.cpp"
    if [ $# -ge 2 ]; then
        calls="$calls    std::printf(\"%u \", $1());"$'\n'
        expected="$expected$2 "
    fi
}
check() {
    local report="$run/$unit.report" functions
    {
        echo 'int main() {'
        printf '%s' "$calls"
        echo '    return 0;'
        echo '}'
    } >> "$run/$unit.cpp"
    (cd "$run" && "$CPPL" -std=c++20 "$unit.cpp" -o "$unit" --cppl-trust-report) > "$report" 2>&1 ||
        { cat "$report" >&2; fail "unit '$unit' was refused"; }
    functions=$(grep -c '^verified ' "$run/$unit.cpp")
    grep -Eq "^Function contracts proven: +$functions\$" "$report" ||
        { cat "$report" >&2; fail "unit '$unit' does not prove all $functions contracts"; }
    grep -Eq '^Unresolved obligations: +0$' "$report" ||
        { cat "$report" >&2; fail "unit '$unit' leaves an obligation unresolved"; }
    local printed
    printed=$("$run/$unit")
    [ "$printed" = "$expected" ] || fail "unit '$unit' printed '$printed', its contracts state '$expected'"
    echo "unit '$unit': $functions contracts proven, and each stated result computed"
}

prelude() {
    cat <<'CPP'
#include <array>
#include <cstddef>
#include <cstdio>
#include <span>
#include <string>
#include <utility>
#include <vector>
type Positive = unsigned where (self > 0u);
void grow(std::vector<unsigned>& v) { v.push_back(2u); }
verified void touch(std::vector<unsigned>& x) ensures (true) { x.push_back(0u); }
verified void keep(const std::vector<unsigned>& x) ensures (true) { }
verified void fill(std::span<unsigned> s) expects (writable(s)) ensures (true) { if (0ul < s.size()) { s[0] = 0u; } }
verified void look(std::span<const unsigned> s) expects (readable(s)) ensures (true) { }
verified void fill_and_grow(std::span<unsigned> s, std::vector<unsigned>& w) expects (writable(s)) ensures (true) { }
verified void fill_and_set(std::span<unsigned> s, unsigned& e) expects (writable(s)) ensures (true) { e = 1u; }
verified void two_writable(std::span<unsigned> a, std::span<unsigned> b) expects (writable(a) && writable(b)) ensures (true) { }
verified void put(unsigned* p, std::size_t n) expects (writable(p, n)) ensures (true) { }
verified void put_and_grow(unsigned* p, std::size_t n, std::vector<unsigned>& w) expects (writable(p, n)) ensures (true) { }
verified void read_n(const unsigned* p, std::size_t n) expects (readable(p, n)) ensures (true) { }
verified void look_and_grow(std::span<const unsigned> s, std::vector<unsigned>& w) expects (readable(s)) ensures (true) { }
CPP
}

# --- Bounds at 0, size(), SIZE_MAX and every length a modeled operation leaves (STDMODEL-011, STDMODEL-012, STDMODEL-013, STDMODEL-021, ARITH-003)
begin bounds
accepted b_sized_last <<'CPP'
verified unsigned b_sized_last()
    ensures (result == result)
{
    std::vector<unsigned> v(3ul);
    return v[2];
}

CPP
accepted b_fill_last <<'CPP'
verified unsigned b_fill_last()
    ensures (result == result)
{
    std::vector<unsigned> v(3ul, 7u);
    return v[2];
}

CPP
accepted b_list_size 3 <<'CPP'
verified unsigned b_list_size()
    ensures (result == 3u)
{
    std::vector<unsigned> v{1u, 2u, 3u};
    return static_cast<unsigned>(v.size());
}

CPP
accepted b_push_then_old_size <<'CPP'
verified unsigned b_push_then_old_size()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    v.push_back(3u);
    return v[2];
}

CPP
accepted b_pop_then_first <<'CPP'
verified unsigned b_pop_then_first()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    v.pop_back();
    return v[0];
}

CPP
accepted b_pop_param_guarded <<'CPP'
verified unsigned b_pop_param_guarded(std::vector<unsigned>& v)
    ensures (result == result)
{
    if (!v.empty()) {
        v.pop_back();
    }
    return 0u;
}

CPP
accepted b_pop_param_expects <<'CPP'
verified unsigned b_pop_param_expects(std::vector<unsigned>& v)
    expects (0ul < v.size())
    ensures (result == result)
{
    v.pop_back();
    return 0u;
}

CPP
accepted b_clear_then_size 0 <<'CPP'
verified unsigned b_clear_then_size()
    ensures (result == 0u)
{
    std::vector<unsigned> v{1u, 2u};
    v.clear();
    return static_cast<unsigned>(v.size());
}

CPP
accepted b_reserve_then_last <<'CPP'
verified unsigned b_reserve_then_last()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    v.reserve(100ul);
    return v[1];
}

CPP
accepted b_copy_then_index <<'CPP'
verified unsigned b_copy_then_index()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w = v;
    return w[1];
}

CPP
accepted b_moved_target_index <<'CPP'
verified unsigned b_moved_target_index()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w = std::move(v);
    return w[1];
}

CPP
accepted b_moved_then_push_first <<'CPP'
verified unsigned b_moved_then_push_first()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w = std::move(v);
    v.push_back(5u);
    return v[0];
}

CPP
accepted b_move_assign_target_index <<'CPP'
verified unsigned b_move_assign_target_index()
    ensures (result == result)
{
    std::vector<unsigned> v{1u};
    std::vector<unsigned> w{1u, 2u};
    v = std::move(w);
    return v[1];
}

CPP
accepted b_string_nul_length 2 <<'CPP'
verified unsigned b_string_nul_length()
    ensures (result == 2u)
{
    std::string s = "ab\0cd";
    return static_cast<unsigned>(s.size());
}

CPP
accepted b_string_length 3 <<'CPP'
verified unsigned b_string_length()
    ensures (result == 3u)
{
    std::string s = "abc";
    return static_cast<unsigned>(s.length());
}

CPP
accepted b_string_append_length 5 <<'CPP'
verified unsigned b_string_append_length()
    ensures (result == 5u)
{
    std::string s = "ab";
    std::string t = "cde";
    s.append(t);
    return static_cast<unsigned>(s.size());
}

CPP
accepted b_span_last <<'CPP'
verified unsigned b_span_last()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::span<unsigned> s(v);
    return s[1];
}

CPP
accepted b_array_last <<'CPP'
verified unsigned b_array_last()
    ensures (result == result)
{
    std::array<unsigned, 3> a{1u, 2u, 3u};
    return a[2];
}

CPP
accepted b_array_size 3 <<'CPP'
verified unsigned b_array_size()
    ensures (result == 3u)
{
    std::array<unsigned, 3> a{1u, 2u, 3u};
    return static_cast<unsigned>(a.size());
}

CPP
accepted b_guard_size_minus_one_nonempty <<'CPP'
verified unsigned b_guard_size_minus_one_nonempty(const std::vector<unsigned>& v, std::size_t i)
    ensures (result == result)
{
    if (0ul < v.size() && i < v.size() - 1ul) {
        return v[i];
    }
    return 0u;
}

CPP
accepted b_guard_plus_one_index_plus_one <<'CPP'
verified unsigned b_guard_plus_one_index_plus_one(const std::vector<unsigned>& v, std::size_t i)
    ensures (result == result)
{
    if (i + 1ul < v.size()) {
        return v[i + 1ul];
    }
    return 0u;
}

CPP
accepted b_guard_not_empty <<'CPP'
verified unsigned b_guard_not_empty(const std::vector<unsigned>& v)
    ensures (result == result)
{
    if (!v.empty()) {
        return v[0];
    }
    return 0u;
}

CPP
accepted b_loop_reverse <<'CPP'
verified unsigned b_loop_reverse(const std::vector<unsigned>& v)
    ensures (result == result)
{
    unsigned last = 0u;
    std::size_t i = v.size();
    while (0ul < i)
        invariant (i <= v.size())
        decreases (i)
    {
        last = v[i - 1ul];
        --i;
    }
    return last;
}

CPP
accepted b_loop_forward <<'CPP'
verified unsigned b_loop_forward(const std::vector<unsigned>& v)
    ensures (result == result)
{
    unsigned last = 0u;
    for (std::size_t i = 0ul; i < v.size(); ++i)
        invariant (i <= v.size())
        decreases (v.size() - i)
    {
        last = v[i];
    }
    return last;
}

CPP
check

# --- A write through one name of an element is seen through every other name of it, and through no name of another (STDMODEL-015, STDMODEL-016)
begin aliases
accepted w_write_v_read_r 5 <<'CPP'
verified unsigned w_write_v_read_r()
    ensures (result == 5u)
{
    std::vector<unsigned> v{1u, 2u};
    unsigned& r = v[0];
    v[0] = 5u;
    return r;
}

CPP
accepted w_write_r_read_v 7 <<'CPP'
verified unsigned w_write_r_read_v()
    ensures (result == 7u)
{
    std::vector<unsigned> v{1u, 2u};
    unsigned& r = v[0];
    r = 7u;
    return v[0];
}

CPP
accepted w_write_span_read_v 8 <<'CPP'
verified unsigned w_write_span_read_v()
    ensures (result == 8u)
{
    std::vector<unsigned> v{1u, 2u};
    std::span<unsigned> s(v);
    s[1] = 8u;
    return v[1];
}

CPP
accepted w_write_v_read_span 8 <<'CPP'
verified unsigned w_write_v_read_span()
    ensures (result == 8u)
{
    std::vector<unsigned> v{1u, 2u};
    std::span<unsigned> s(v);
    v[1] = 8u;
    return s[1];
}

CPP
accepted w_other_element_kept 1 <<'CPP'
verified unsigned w_other_element_kept()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    unsigned& r = v[0];
    v[1] = 5u;
    return r;
}

CPP
accepted w_two_refs_same_element 9 <<'CPP'
verified unsigned w_two_refs_same_element()
    ensures (result == 9u)
{
    std::vector<unsigned> v{1u, 2u};
    unsigned& r = v[0];
    unsigned& q = v[0];
    q = 9u;
    return r;
}

CPP
accepted w_two_spans_same_vector 4 <<'CPP'
verified unsigned w_two_spans_same_vector()
    ensures (result == 4u)
{
    std::vector<unsigned> v{1u, 2u};
    std::span<unsigned> a(v);
    std::span<const unsigned> b(v);
    a[0] = 4u;
    return b[0];
}

CPP
accepted w_copy_unaffected 1 <<'CPP'
verified unsigned w_copy_unaffected()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w = v;
    w[0] = 4u;
    return v[0];
}

CPP
accepted w_string_through_ref 122 <<'CPP'
verified unsigned w_string_through_ref()
    ensures (result == 122u)
{
    std::string s = "ab";
    char& c = s[0];
    c = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
accepted w_array_through_ref 6 <<'CPP'
verified unsigned w_array_through_ref()
    ensures (result == 6u)
{
    std::array<unsigned, 2> a{1u, 2u};
    unsigned& r = a[1];
    r = 6u;
    return a[1];
}

CPP
check

# --- What a call is handed: capabilities, a view and its container in one call, and spans passed on (STDMODEL-016, STDMODEL-017)
begin calls
accepted x_span_and_other_container <<'CPP'
verified unsigned x_span_and_other_container()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{1u};
    fill_and_grow(v, w);
    return 0u;
}

CPP
accepted x_span_and_other_element <<'CPP'
verified unsigned x_span_and_other_element()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{1u};
    fill_and_set(v, w[0]);
    return 0u;
}

CPP
accepted x_data_count_exact <<'CPP'
verified unsigned x_data_count_exact()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    put(v.data(), v.size());
    return 0u;
}

CPP
accepted x_data_empty_zero <<'CPP'
verified unsigned x_data_empty_zero()
    ensures (result == result)
{
    std::vector<unsigned> v;
    read_n(v.data(), 0ul);
    return 0u;
}

CPP
accepted x_two_writable_distinct 2 <<'CPP'
verified unsigned x_two_writable_distinct()
    ensures (result == 2u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{3u};
    two_writable(v, w);
    return static_cast<unsigned>(v.size());
}

CPP
accepted x_writable_passed <<'CPP'
verified unsigned x_writable_passed(std::span<unsigned> s)
    expects (writable(s))
    ensures (result == result)
{
    fill(s);
    return 0u;
}

CPP
accepted x_span_local_write_keeps_length 2 <<'CPP'
verified unsigned x_span_local_write_keeps_length()
    ensures (result == 2u)
{
    std::vector<unsigned> v{1u, 2u};
    std::span<unsigned> s(v);
    fill(s);
    return static_cast<unsigned>(v.size());
}

CPP
accepted x_span_local_read_keeps_element 1 <<'CPP'
verified unsigned x_span_local_read_keeps_element()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::span<const unsigned> s(v);
    look(s);
    return v[0];
}

CPP
accepted x_span_local_and_other_container <<'CPP'
verified unsigned x_span_local_and_other_container()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{1u};
    std::span<unsigned> s(v);
    fill_and_grow(s, w);
    return 0u;
}

CPP
accepted x_span_param_passed_with_other_container <<'CPP'
verified unsigned x_span_param_passed_with_other_container(std::span<unsigned> s, std::vector<unsigned>& w)
    expects (writable(s))
    ensures (result == result)
{
    fill_and_grow(s, w);
    return 0u;
}

CPP
check

# --- Content invariants of a refined element type (STDMODEL-020, STDMODEL-027)
begin content
accepted c_list_holds <<'CPP'
verified unsigned c_list_holds()
    ensures (0u < result)
{
    std::vector<Positive> v{1u, 2u};
    return v[1];
}

CPP
accepted c_fill_one <<'CPP'
verified unsigned c_fill_one()
    ensures (0u < result)
{
    std::vector<Positive> v(3ul, 1u);
    return v[2];
}

CPP
accepted c_push_guarded <<'CPP'
verified unsigned c_push_guarded(unsigned x)
    ensures (0u < result)
{
    std::vector<Positive> v{1u};
    if (0u < x) {
        v.push_back(x);
    }
    return v[0];
}

CPP
accepted c_copy_from_refined <<'CPP'
verified unsigned c_copy_from_refined()
    ensures (0u < result)
{
    std::vector<Positive> w{1u};
    std::vector<Positive> v = w;
    return v[0];
}

CPP
accepted c_readable_span_call <<'CPP'
verified unsigned c_readable_span_call()
    ensures (0u < result)
{
    std::vector<Positive> v{1u};
    look(v);
    return v[0];
}

CPP
check

echo 'every accepted twin of a sequence edge case is proven and computes what it states'
