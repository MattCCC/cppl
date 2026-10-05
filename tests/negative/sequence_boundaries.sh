#!/usr/bin/env bash
# SPEC: STDMODEL-010, STDMODEL-011, STDMODEL-012, STDMODEL-013, STDMODEL-014, STDMODEL-015, STDMODEL-016
# SPEC: STDMODEL-017, STDMODEL-019, STDMODEL-020, STDMODEL-021, STDMODEL-023, STDMODEL-027, ARITH-003
# TRUST.md TCB-LIB-006, TCB-LIB-007
#
# The edges of the verified sequence subset (RFC 0020), each case refused for
# its stated reason: every index at or past a length a modeled operation leaves,
# guards that wrap or test the wrong length, stale values read through another
# name of one element, a view and its container handed to one call, members and
# types the model leaves out, and refined element types. The accepted twins are
# in `e2e/sequence_boundaries.sh`. `x_span_local_*`, `x_span_param_passed_on`
# and `c_span_local_writable_call` are regressions: a span local or parameter
# passed by value was not followed to the storage it views (TRUST.md 36.3).
set -euo pipefail
CPPL="$1"
WORK="$3"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/sequence-boundaries.XXXXXX")
fail() {
    echo "$1" >&2
    exit 1
}

# A unit gathers refused cases, each a verified function named for its case.
# The unit must be refused and write no object; every error must carry a
# location inside a refused case, so none is collateral damage of another; and
# each case must be refused for its stated reason.
unit=""
cases=()
begin() {
    unit="$1"
    cases=()
    prelude > "$run/$unit.cpp"
}
# refused <name> <reason>: the case's function follows on stdin.
refused() {
    local first
    first=$(($(wc -l < "$run/$unit.cpp") + 1))
    cat >> "$run/$unit.cpp"
    cases+=("$1|$first|$(wc -l < "$run/$unit.cpp")|$2")
}
check() {
    local log="$run/$unit.log" status=0 entry name first last reason line owned
    echo 'int main() { return 0; }' >> "$run/$unit.cpp"
    (cd "$run" && "$CPPL" -std=c++20 -c "$unit.cpp" -o "$unit.o") > "$log" 2>&1 || status=$?
    [ "$status" -ne 0 ] || fail "unit '$unit' was accepted"
    # A refusal is an ordinary failure; a crash or a missing tool is not one.
    [ "$status" -eq 1 ] || { cat "$log" >&2; fail "unit '$unit' ended with status $status, not a refusal"; }
    [ ! -e "$run/$unit.o" ] || fail "an object was written for unit '$unit'"
    if grep -qE 'PROVEN|C\+\+L Trust Report' "$log"; then
        cat "$log" >&2
        fail "unit '$unit' was described as proven"
    fi
    if grep 'error \[' "$log" | grep -qv "^$unit\.cpp:[0-9]*:[0-9]*: error \["; then
        cat "$log" >&2
        fail "unit '$unit' has an error without a source location"
    fi
    for line in $(awk -F: -v file="$unit.cpp" '$1 == file && $4 ~ /^ error \[/ { print $2 }' "$log"); do
        owned=0
        for entry in "${cases[@]}"; do
            IFS='|' read -r name first last reason <<< "$entry"
            if [ "$line" -ge "$first" ] && [ "$line" -le "$last" ]; then
                owned=1
                break
            fi
        done
        [ "$owned" -eq 1 ] || { cat "$log" >&2; fail "unit '$unit' has an error at line $line, in no refused case"; }
    done
    for entry in "${cases[@]}"; do
        IFS='|' read -r name first last reason <<< "$entry"
        if ! awk -F: -v file="$unit.cpp" -v from="$first" -v to="$last" -v reason="$reason" '
                $1 == file && $2 >= from && $2 <= to && index($0, reason) { found = 1 }
                END { exit !found }' "$log"; then
            cat "$log" >&2
            fail "case '$name' of unit '$unit' was not refused for its stated reason: $reason"
        fi
    done
    echo "unit '$unit': ${#cases[@]} cases refused, each for its stated reason"
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
refused b_empty_index "law 'b_empty_index element index' is not proven" <<'CPP'
verified unsigned b_empty_index()
    ensures (result == result)
{
    std::vector<unsigned> v;
    return v[0];
}

CPP
refused b_empty_braces_index "law 'b_empty_braces_index element index' is not proven" <<'CPP'
verified unsigned b_empty_braces_index()
    ensures (result == result)
{
    std::vector<unsigned> v{};
    return v[0];
}

CPP
refused b_sized_zero_index "law 'b_sized_zero_index element index' is not proven" <<'CPP'
verified unsigned b_sized_zero_index()
    ensures (result == result)
{
    std::vector<unsigned> v(0ul);
    return v[0];
}

CPP
refused b_sized_past "law 'b_sized_past element index' is not proven" <<'CPP'
verified unsigned b_sized_past()
    ensures (result == result)
{
    std::vector<unsigned> v(3ul);
    return v[3];
}

CPP
refused b_fill_past "law 'b_fill_past element index' is not proven" <<'CPP'
verified unsigned b_fill_past()
    ensures (result == result)
{
    std::vector<unsigned> v(3ul, 7u);
    return v[3];
}

CPP
refused b_list_size_wrong "return path 'b_list_size_wrong path" <<'CPP'
verified unsigned b_list_size_wrong()
    ensures (result == 2u)
{
    std::vector<unsigned> v{1u, 2u, 3u};
    return static_cast<unsigned>(v.size());
}

CPP
refused b_at_size "law 'b_at_size element index' is not proven" <<'CPP'
verified unsigned b_at_size()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    return v[v.size()];
}

CPP
refused b_push_then_past "law 'b_push_then_past element index' is not proven" <<'CPP'
verified unsigned b_push_then_past()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    v.push_back(3u);
    return v[3];
}

CPP
refused b_pop_then_old_last "law 'b_pop_then_old_last element index' is not proven" <<'CPP'
verified unsigned b_pop_then_old_last()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    v.pop_back();
    return v[1];
}

CPP
refused b_pop_empty "call-site precondition for 'b_pop_empty -> std::vector::pop_back' is not proven" <<'CPP'
verified unsigned b_pop_empty()
    ensures (result == result)
{
    std::vector<unsigned> v;
    v.pop_back();
    return 0u;
}

CPP
refused b_pop_twice_one "call-site precondition for 'b_pop_twice_one -> std::vector::pop_back' is not proven" <<'CPP'
verified unsigned b_pop_twice_one()
    ensures (result == result)
{
    std::vector<unsigned> v{1u};
    v.pop_back();
    v.pop_back();
    return 0u;
}

CPP
refused b_pop_param_unguarded "call-site precondition for 'b_pop_param_unguarded -> std::vector::pop_back' is not proven" <<'CPP'
verified unsigned b_pop_param_unguarded(std::vector<unsigned>& v)
    ensures (result == result)
{
    v.pop_back();
    return 0u;
}

CPP
refused b_clear_then_index "law 'b_clear_then_index element index' is not proven" <<'CPP'
verified unsigned b_clear_then_index()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    v.clear();
    return v[0];
}

CPP
refused b_reserve_then_size_index "law 'b_reserve_then_size_index element index' is not proven" <<'CPP'
verified unsigned b_reserve_then_size_index()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    v.reserve(100ul);
    return v[2];
}

CPP
refused b_copy_then_past "law 'b_copy_then_past element index' is not proven" <<'CPP'
verified unsigned b_copy_then_past()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w = v;
    return w[2];
}

CPP
refused b_moved_index "law 'b_moved_index element index' is not proven" <<'CPP'
verified unsigned b_moved_index()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w = std::move(v);
    return v[0];
}

CPP
refused b_moved_size_zero "return path 'b_moved_size_zero path" <<'CPP'
verified unsigned b_moved_size_zero()
    ensures (result == 0u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w = std::move(v);
    return static_cast<unsigned>(v.size());
}

CPP
refused b_moved_size_two "return path 'b_moved_size_two path" <<'CPP'
verified unsigned b_moved_size_two()
    ensures (result == 2u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w = std::move(v);
    return static_cast<unsigned>(v.size());
}

CPP
refused b_moved_then_clear_index "law 'b_moved_then_clear_index element index' is not proven" <<'CPP'
verified unsigned b_moved_then_clear_index()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w = std::move(v);
    v.clear();
    return v[0];
}

CPP
refused b_move_assign_source_index "law 'b_move_assign_source_index element index' is not proven" <<'CPP'
verified unsigned b_move_assign_source_index()
    ensures (result == result)
{
    std::vector<unsigned> v{1u};
    std::vector<unsigned> w{1u, 2u};
    v = std::move(w);
    return w[0];
}

CPP
refused b_copy_assign_target_past "law 'b_copy_assign_target_past element index' is not proven" <<'CPP'
verified unsigned b_copy_assign_target_past()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u, 3u};
    std::vector<unsigned> w{1u};
    v = w;
    return v[1];
}

CPP
refused b_self_copy_assign "assigning 'v' to itself is not modeled" <<'CPP'
verified unsigned b_self_copy_assign()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    v = v;
    return v[0];
}

CPP
refused b_self_move_assign "assigning 'v' to itself is not modeled" <<'CPP'
verified unsigned b_self_move_assign()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    v = std::move(v);
    return 0u;
}

CPP
refused b_move_param "'w' is moved from 'v', which is caller storage" <<'CPP'
verified unsigned b_move_param(std::vector<unsigned>& v)
    ensures (result == result)
{
    std::vector<unsigned> w = std::move(v);
    return 0u;
}

CPP
refused b_string_nul_past "law 'b_string_nul_past element index' is not proven" <<'CPP'
verified unsigned b_string_nul_past()
    ensures (result == result)
{
    std::string s = "ab\0cd";
    return static_cast<unsigned>(s[2]);
}

CPP
refused b_string_empty_index "law 'b_string_empty_index element index' is not proven" <<'CPP'
verified unsigned b_string_empty_index()
    ensures (result == result)
{
    std::string s = "";
    return static_cast<unsigned>(s[0]);
}

CPP
refused b_string_append_past "law 'b_string_append_past element index' is not proven" <<'CPP'
verified unsigned b_string_append_past()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cde";
    s.append(t);
    return static_cast<unsigned>(s[5]);
}

CPP
refused b_span_size_index "law 'b_span_size_index element index' is not proven" <<'CPP'
verified unsigned b_span_size_index()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::span<unsigned> s(v);
    return s[s.size()];
}

CPP
refused b_array_past "proof array index must be a constant within the resolved extent" <<'CPP'
verified unsigned b_array_past()
    ensures (result == result)
{
    std::array<unsigned, 3> a{1u, 2u, 3u};
    return a[3];
}

CPP
refused b_array_zero_index "local 'a' has type 'std::array<unsigned int, 0>', which is not modeled" <<'CPP'
verified unsigned b_array_zero_index()
    ensures (result == result)
{
    std::array<unsigned, 0> a{};
    return a[0];
}

CPP
refused b_array_zero_empty "local 'a' has type 'std::array<unsigned int, 0>', which is not modeled" <<'CPP'
verified unsigned b_array_zero_empty()
    ensures (result == 1u)
{
    std::array<unsigned, 0> a{};
    return a.empty() ? 1u : 0u;
}

CPP
refused b_array_param_symbolic "this expression form has no formal meaning in this implementation" <<'CPP'
verified unsigned b_array_param_symbolic(std::array<unsigned, 4> a, std::size_t i)
    ensures (result == result)
{
    return a[i];
}

CPP
refused b_array_param_guarded "this expression form has no formal meaning in this implementation" <<'CPP'
verified unsigned b_array_param_guarded(std::array<unsigned, 4> a, std::size_t i)
    ensures (result == result)
{
    if (i < 4ul) {
        return a[i];
    }
    return 0u;
}

CPP
refused b_array_ref_param "an element of the std::array a reference designates is not modeled" <<'CPP'
verified unsigned b_array_ref_param(const std::array<unsigned, 4>& a)
    ensures (result == result)
{
    return a[0];
}

CPP
refused b_guard_le "law 'b_guard_le element index' is not proven" <<'CPP'
verified unsigned b_guard_le(const std::vector<unsigned>& v, std::size_t i)
    ensures (result == result)
{
    if (i <= v.size()) {
        return v[i];
    }
    return 0u;
}

CPP
refused b_guard_size_minus_one_wraps "law 'b_guard_size_minus_one_wraps element index' is not proven" <<'CPP'
verified unsigned b_guard_size_minus_one_wraps(const std::vector<unsigned>& v, std::size_t i)
    ensures (result == result)
{
    if (i < v.size() - 1ul) {
        return v[i];
    }
    return 0u;
}

CPP
refused b_guard_plus_one_wraps "law 'b_guard_plus_one_wraps element index' is not proven" <<'CPP'
verified unsigned b_guard_plus_one_wraps(const std::vector<unsigned>& v, std::size_t i)
    ensures (result == result)
{
    if (i + 1ul < v.size()) {
        return v[i];
    }
    return 0u;
}

CPP
refused b_guard_other_vector "law 'b_guard_other_vector element index' is not proven" <<'CPP'
verified unsigned b_guard_other_vector(const std::vector<unsigned>& v, const std::vector<unsigned>& w, std::size_t i)
    ensures (result == result)
{
    if (i < w.size()) {
        return v[i];
    }
    return 0u;
}

CPP
refused b_guard_negated_wrong "law 'b_guard_negated_wrong element index' is not proven" <<'CPP'
verified unsigned b_guard_negated_wrong(const std::vector<unsigned>& v, std::size_t i)
    ensures (result == result)
{
    if (!(i < v.size())) {
        return v[i];
    }
    return 0u;
}

CPP
refused b_guard_empty_wrong "law 'b_guard_empty_wrong element index' is not proven" <<'CPP'
verified unsigned b_guard_empty_wrong(const std::vector<unsigned>& v)
    ensures (result == result)
{
    if (v.empty()) {
        return v[0];
    }
    return 0u;
}

CPP
refused b_guard_max "law 'b_guard_max element index' is not proven" <<'CPP'
verified unsigned b_guard_max(const std::vector<unsigned>& v, std::size_t i)
    ensures (result == result)
{
    if (i < 18446744073709551615ul) {
        return v[i];
    }
    return 0u;
}

CPP
refused b_loop_le "law 'b_loop_le element index' is not proven" <<'CPP'
verified unsigned b_loop_le(const std::vector<unsigned>& v)
    expects (v.size() < 100ul)
    ensures (result == result)
{
    unsigned sum = 0u;
    std::size_t i = 0ul;
    while (i <= v.size())
        invariant (i <= v.size() + 1ul)
        decreases (v.size() + 1ul - i)
    {
        sum = v[i];
        ++i;
    }
    return sum;
}

CPP
refused b_loop_reverse_off_by_one "law 'b_loop_reverse_off_by_one element index' is not proven" <<'CPP'
verified unsigned b_loop_reverse_off_by_one(const std::vector<unsigned>& v)
    ensures (result == result)
{
    unsigned last = 0u;
    std::size_t i = v.size();
    while (0ul < i)
        invariant (i <= v.size())
        decreases (i)
    {
        last = v[i];
        --i;
    }
    return last;
}

CPP
refused b_loop_stale_invariant "call-site precondition for 'b_loop_stale_invariant -> std::vector::pop_back' is not proven" <<'CPP'
verified unsigned b_loop_stale_invariant(std::vector<unsigned>& v)
    ensures (result == result)
{
    unsigned last = 0u;
    std::size_t i = 0ul;
    const std::size_t n = v.size();
    while (i < n)
        invariant (i <= n)
        decreases (n - i)
    {
        v.pop_back();
        last = v[i];
        ++i;
    }
    return last;
}

CPP
refused b_span_param_read_no_cap "reading an element of 's' requires 'readable(s)', which was not established: a span does not make the storage it views valid" <<'CPP'
verified unsigned b_span_param_read_no_cap(std::span<const unsigned> s)
    ensures (result == result)
{
    if (0ul < s.size()) {
        return s[0];
    }
    return 0u;
}

CPP
refused b_span_param_write_readable "writing an element of 's' requires 'writable(s)', which was not established: a span does not make the storage it views valid" <<'CPP'
verified unsigned b_span_param_write_readable(std::span<unsigned> s)
    expects (readable(s))
    ensures (result == result)
{
    if (0ul < s.size()) {
        s[0] = 1u;
    }
    return 0u;
}

CPP
refused b_span_const_writable "the precondition of verified function 'b_span_const_writable' is not modeled by this implementation: 'writable(s)' names elements declared const" <<'CPP'
verified unsigned b_span_const_writable(std::span<const unsigned> s)
    expects (writable(s))
    ensures (result == result)
{
    return 0u;
}

CPP
refused b_span_param_unguarded "law 'b_span_param_unguarded element index' is not proven" <<'CPP'
verified unsigned b_span_param_unguarded(std::span<const unsigned> s)
    expects (readable(s))
    ensures (result == result)
{
    return s[0];
}

CPP
refused b_capability_in_ensures "the postcondition of verified function 'b_capability_in_ensures' conjoins a memory capability with a predicate, which only a verified function's expects clause may do" <<'CPP'
verified unsigned b_capability_in_ensures(std::span<const unsigned> s)
    ensures (readable(s) && result == 0u)
{
    return 0u;
}

CPP
check

# --- A write through one name of an element is seen through every other name of it, and through no name of another (STDMODEL-015, STDMODEL-016)
begin aliases
refused w_write_v_read_r_stale "return path 'w_write_v_read_r_stale path" <<'CPP'
verified unsigned w_write_v_read_r_stale()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    unsigned& r = v[0];
    v[0] = 5u;
    return r;
}

CPP
refused w_write_r_read_v_stale "return path 'w_write_r_read_v_stale path" <<'CPP'
verified unsigned w_write_r_read_v_stale()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    unsigned& r = v[0];
    r = 7u;
    return v[0];
}

CPP
refused w_write_span_read_v_stale "return path 'w_write_span_read_v_stale path" <<'CPP'
verified unsigned w_write_span_read_v_stale()
    ensures (result == 2u)
{
    std::vector<unsigned> v{1u, 2u};
    std::span<unsigned> s(v);
    s[1] = 8u;
    return v[1];
}

CPP
refused w_write_v_read_span_stale "return path 'w_write_v_read_span_stale path" <<'CPP'
verified unsigned w_write_v_read_span_stale()
    ensures (result == 2u)
{
    std::vector<unsigned> v{1u, 2u};
    std::span<unsigned> s(v);
    v[1] = 8u;
    return s[1];
}

CPP
refused w_symbolic_write_may_alias "return path 'w_symbolic_write_may_alias path" <<'CPP'
verified unsigned w_symbolic_write_may_alias(std::size_t j)
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    unsigned& r = v[0];
    if (j < v.size()) {
        v[j] = 5u;
    }
    return r;
}

CPP
refused w_two_refs_same_element_stale "return path 'w_two_refs_same_element_stale path" <<'CPP'
verified unsigned w_two_refs_same_element_stale()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    unsigned& r = v[0];
    unsigned& q = v[0];
    q = 9u;
    return r;
}

CPP
refused w_two_spans_same_vector_stale "return path 'w_two_spans_same_vector_stale path" <<'CPP'
verified unsigned w_two_spans_same_vector_stale()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::span<unsigned> a(v);
    std::span<const unsigned> b(v);
    a[0] = 4u;
    return b[0];
}

CPP
refused w_string_through_ref_stale "return path 'w_string_through_ref_stale path" <<'CPP'
verified unsigned w_string_through_ref_stale()
    ensures (result == 97u)
{
    std::string s = "ab";
    char& c = s[0];
    c = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
refused w_array_through_ref_stale "verified function 'w_array_through_ref_stale' does not satisfy its contract" <<'CPP'
verified unsigned w_array_through_ref_stale()
    ensures (result == 2u)
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
refused x_span_and_container_mut "'v' is handed to 'fill_and_grow' as a view or data pointer and, in the same call, by a reference through which the callee may reallocate it" <<'CPP'
verified unsigned x_span_and_container_mut()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    fill_and_grow(v, v);
    return 0u;
}

CPP
refused x_span_and_element "'v[0]', an element of 'v', is passed to 'fill_and_set' by mutable reference, and the same call hands it a view or data pointer through which it may write the elements of 'v'" <<'CPP'
verified unsigned x_span_and_element()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    fill_and_set(v, v[0]);
    return 0u;
}

CPP
refused x_data_and_container_mut "'v' is handed to 'put_and_grow' as a view or data pointer and, in the same call, by a reference through which the callee may reallocate it" <<'CPP'
verified unsigned x_data_and_container_mut()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    put_and_grow(v.data(), v.size(), v);
    return 0u;
}

CPP
refused x_data_count_past "call-site precondition for 'x_data_count_past -> put' is not proven" <<'CPP'
verified unsigned x_data_count_past()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    put(v.data(), v.size() + 1ul);
    return 0u;
}

CPP
refused x_data_empty_one "call-site precondition for 'x_data_empty_one -> read_n' is not proven" <<'CPP'
verified unsigned x_data_empty_one()
    ensures (result == result)
{
    std::vector<unsigned> v;
    read_n(v.data(), 1ul);
    return 0u;
}

CPP
refused x_data_outside_call "local 'p' has type 'unsigned int *', which is not modeled" <<'CPP'
verified unsigned x_data_outside_call()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    unsigned* p = v.data();
    return 0u;
}

CPP
refused x_two_writable_same_elements "'v' is handed to 'two_writable' through two views or data pointers the callee may write" <<'CPP'
verified unsigned x_two_writable_same_elements()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    two_writable(v, v);
    return v[0];
}

CPP
# One storage written through two arguments of one call has no single
# post-state (STDMODEL-017), whatever the caller reads afterwards. Accepted
# twin: `x_two_writable_distinct`.
refused x_two_writable_same "'v' is handed to 'two_writable' through two views or data pointers the callee may write" <<'CPP'
verified unsigned x_two_writable_same()
    ensures (result == 2u)
{
    std::vector<unsigned> v{1u, 2u};
    two_writable(v, v);
    return static_cast<unsigned>(v.size());
}

CPP
refused x_two_writable_span_local_and_container "'v' is handed to 'two_writable' through two views or data pointers the callee may write" <<'CPP'
verified unsigned x_two_writable_span_local_and_container()
    ensures (result == 2u)
{
    std::vector<unsigned> v{1u, 2u};
    std::span<unsigned> s(v);
    two_writable(s, v);
    return static_cast<unsigned>(v.size());
}

CPP
refused x_two_writable_span_parameter_twice "span parameter 's' is handed to 'two_writable' twice as a view the callee may write" <<'CPP'
verified unsigned x_two_writable_span_parameter_twice(std::span<unsigned> s)
    expects (writable(s))
    ensures (result == result)
{
    two_writable(s, s);
    return static_cast<unsigned>(s.size());
}

CPP
refused x_span_param_and_vector_param "'v' is handed to 'fill_and_grow' as a view or data pointer and, in the same call, by a reference through which the callee may reallocate it" <<'CPP'
verified unsigned x_span_param_and_vector_param()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::span<unsigned> s(v);
    fill_and_grow(s, v);
    return 0u;
}

CPP
refused x_span_without_capability "calling 'fill' requires 'writable(s)', which is not established: the contract of 'x_span_without_capability' states no such capability, and 'p != nullptr' does not imply it" <<'CPP'
verified unsigned x_span_without_capability(std::span<unsigned> s)
    ensures (result == result)
{
    fill(s);
    return 0u;
}

CPP
refused x_readable_to_writable "calling 'fill' requires 'writable(s)', which is not established: the contract of 'x_readable_to_writable' states no such capability, and 'p != nullptr' does not imply it" <<'CPP'
verified unsigned x_readable_to_writable(std::span<unsigned> s)
    expects (readable(s))
    ensures (result == result)
{
    fill(s);
    return 0u;
}

CPP
refused x_span_local_and_container_mut "'v' is handed to 'fill_and_grow' as a view or data pointer and, in the same call, by a reference through which the callee may reallocate it" <<'CPP'
verified unsigned x_span_local_and_container_mut()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::span<unsigned> s(v);
    fill_and_grow(s, v);
    return 0u;
}

CPP
refused x_const_span_local_and_container_mut "'v' is handed to 'look_and_grow' as a view or data pointer and, in the same call, by a reference through which the callee may reallocate it" <<'CPP'
verified unsigned x_const_span_local_and_container_mut()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::span<const unsigned> s(v);
    look_and_grow(s, v);
    return 0u;
}

CPP
refused x_span_local_copy_and_container_mut "'v' is handed to 'fill_and_grow' as a view or data pointer and, in the same call, by a reference through which the callee may reallocate it" <<'CPP'
verified unsigned x_span_local_copy_and_container_mut()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::span<unsigned> s(v);
    fill_and_grow(std::span<unsigned>(s), v);
    return 0u;
}

CPP
refused x_span_local_write_then_container "return path 'x_span_local_write_then_container path" <<'CPP'
verified unsigned x_span_local_write_then_container()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::span<unsigned> s(v);
    fill(s);
    return v[0];
}

CPP
refused x_span_local_write_then_span "return path 'x_span_local_write_then_span path" <<'CPP'
verified unsigned x_span_local_write_then_span()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::span<unsigned> s(v);
    fill(s);
    return s[0];
}

CPP
refused x_span_local_write_then_reference "return path 'x_span_local_write_then_reference path" <<'CPP'
verified unsigned x_span_local_write_then_reference()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    unsigned& r = v[0];
    std::span<unsigned> s(v);
    fill(s);
    return r;
}

CPP
refused x_span_local_and_element "'v[0]', an element of 'v', is passed to 'fill_and_set' by mutable reference, and the same call hands it a view or data pointer through which it may write the elements of 'v'" <<'CPP'
verified unsigned x_span_local_and_element()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::span<unsigned> s(v);
    fill_and_set(s, v[0]);
    return 0u;
}

CPP
refused x_span_param_passed_on "return path 'x_span_param_passed_on path" <<'CPP'
verified unsigned x_span_param_passed_on(std::span<unsigned> s)
    expects (readable(s) && writable(s) && 0ul < s.size())
    ensures (result == 1u)
{
    s[0] = 1u;
    fill(s);
    return s[0];
}

CPP
check

# --- Whatever the model leaves out is refused by name, never approximated (STDMODEL-010, STDMODEL-014, STDMODEL-019)
begin names
refused n_at "'std::vector::at' is not a modeled operation of std::vector" <<'CPP'
verified unsigned n_at()
    ensures (result == result)
{
    std::vector<unsigned> v{1u};
    return v.at(0ul);
}

CPP
refused n_front "'std::vector::front' is not a modeled operation of std::vector" <<'CPP'
verified unsigned n_front()
    ensures (result == result)
{
    std::vector<unsigned> v{1u};
    return v.front();
}

CPP
refused n_back "'std::vector::back' is not a modeled operation of std::vector" <<'CPP'
verified unsigned n_back()
    ensures (result == result)
{
    std::vector<unsigned> v{1u};
    return v.back();
}

CPP
refused n_begin "call does not resolve to an ordinary function or to a member function named on its object" <<'CPP'
verified unsigned n_begin()
    ensures (result == result)
{
    std::vector<unsigned> v{1u};
    return *v.begin();
}

CPP
refused n_insert "'std::vector::insert' is not a modeled operation of std::vector" <<'CPP'
verified unsigned n_insert()
    ensures (result == result)
{
    std::vector<unsigned> v{1u};
    v.insert(v.begin(), 2u);
    return 0u;
}

CPP
refused n_erase "'std::vector::erase' is not a modeled operation of std::vector" <<'CPP'
verified unsigned n_erase()
    ensures (result == result)
{
    std::vector<unsigned> v{1u};
    v.erase(v.begin());
    return 0u;
}

CPP
refused n_emplace_back "'std::vector::emplace_back' is not a modeled operation of std::vector" <<'CPP'
verified unsigned n_emplace_back()
    ensures (result == result)
{
    std::vector<unsigned> v{1u};
    v.emplace_back(2u);
    return 0u;
}

CPP
refused n_resize "'std::vector::resize' is not a modeled operation of std::vector" <<'CPP'
verified unsigned n_resize()
    ensures (result == result)
{
    std::vector<unsigned> v{1u};
    v.resize(3ul);
    return 0u;
}

CPP
refused n_shrink_to_fit "'std::vector::shrink_to_fit' is not a modeled operation of std::vector" <<'CPP'
verified unsigned n_shrink_to_fit()
    ensures (result == result)
{
    std::vector<unsigned> v{1u};
    v.shrink_to_fit();
    return 0u;
}

CPP
refused n_swap "'std::vector::swap' is not a modeled operation of std::vector" <<'CPP'
verified unsigned n_swap()
    ensures (result == result)
{
    std::vector<unsigned> v{1u};
    std::vector<unsigned> w{2u};
    v.swap(w);
    return 0u;
}

CPP
refused n_std_swap "it calls a function that is not declared pure, so its value is not a mathematical function of its arguments" <<'CPP'
verified unsigned n_std_swap()
    ensures (result == result)
{
    std::vector<unsigned> v{1u};
    std::vector<unsigned> w{2u};
    std::swap(v, w);
    return 0u;
}

CPP
refused n_assign "'std::vector::assign' is not a modeled operation of std::vector" <<'CPP'
verified unsigned n_assign()
    ensures (result == result)
{
    std::vector<unsigned> v{1u};
    v.assign(3ul, 2u);
    return 0u;
}

CPP
refused n_capacity "'std::vector::capacity' is not a modeled operation of std::vector" <<'CPP'
verified unsigned n_capacity()
    ensures (result == result)
{
    std::vector<unsigned> v{1u};
    return static_cast<unsigned>(v.capacity());
}

CPP
refused n_max_size "'std::vector::max_size' is not a modeled operation of std::vector" <<'CPP'
verified unsigned n_max_size()
    ensures (result == result)
{
    std::vector<unsigned> v{1u};
    return static_cast<unsigned>(v.max_size());
}

CPP
refused n_substr "local 't' of type 'std::basic_string<char>' is not initialized by a modeled constructor" <<'CPP'
verified unsigned n_substr()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = s.substr(1ul);
    return 0u;
}

CPP
refused n_c_str "local 'p' has type 'const char *', which is not modeled" <<'CPP'
verified unsigned n_c_str()
    ensures (result == result)
{
    std::string s = "ab";
    const char* p = s.c_str();
    return 0u;
}

CPP
refused n_find "'std::basic_string<char>::find' is not a modeled operation of std::basic_string<char>" <<'CPP'
verified unsigned n_find()
    ensures (result == result)
{
    std::string s = "ab";
    return static_cast<unsigned>(s.find('b'));
}

CPP
refused n_string_insert "'std::basic_string<char>::insert' is not a modeled operation of std::basic_string<char>" <<'CPP'
verified unsigned n_string_insert()
    ensures (result == result)
{
    std::string s = "ab";
    s.insert(0ul, "x");
    return 0u;
}

CPP
refused n_string_plus "local 't' of type 'std::basic_string<char>' is not initialized by a modeled constructor" <<'CPP'
verified unsigned n_string_plus()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = s + s;
    return 0u;
}

CPP
refused n_vector_bool "local 'v' has type 'std::vector<bool>', which is not modeled: std::vector<bool> is not modeled: its operator[] yields a proxy object rather than an element" <<'CPP'
verified unsigned n_vector_bool()
    ensures (result == result)
{
    std::vector<bool> v{true};
    return v[0] ? 1u : 0u;
}

CPP
refused n_vector_float "local 'v' has type 'std::vector<float>', which is not modeled: its element type 'float' is not modeled: a modeled sequence holds built-in integers or bool" <<'CPP'
verified unsigned n_vector_float()
    ensures (result == result)
{
    std::vector<float> v{1.0f};
    return 0u;
}

CPP
refused n_vector_nested "local 'v' has type 'std::vector<std::vector<unsigned int>>', which is not modeled: its element type 'std::vector<unsigned int>' is not modeled: a modeled sequence holds built-in integers or bool" <<'CPP'
verified unsigned n_vector_nested()
    ensures (result == result)
{
    std::vector<std::vector<unsigned>> v;
    return 0u;
}

CPP
refused n_span_static_extent "local 's' has type 'std::span<unsigned int, 2>', which is not modeled: only a span of dynamic extent is modeled" <<'CPP'
verified unsigned n_span_static_extent()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::span<unsigned, 2> s(v.data(), 2);
    return s[0];
}

CPP
refused n_wstring "local 's' has type 'std::basic_string<wchar_t>', which is not modeled: only std::string, std::basic_string<char> with the standard traits and allocator, is modeled" <<'CPP'
verified unsigned n_wstring()
    ensures (result == result)
{
    std::wstring s = L"ab";
    return 0u;
}

CPP
refused n_u8string "local 's' has type 'std::basic_string<char8_t>', which is not modeled: only std::string, std::basic_string<char> with the standard traits and allocator, is modeled" <<'CPP'
verified unsigned n_u8string()
    ensures (result == result)
{
    std::u8string s = u8"ab";
    return 0u;
}

CPP
refused n_range_for "range-based for loops are not modeled" <<'CPP'
verified unsigned n_range_for()
    ensures (result == result)
{
    std::vector<unsigned> v{1u};
    unsigned t = 0u;
    for (unsigned x : v) {
        t = x;
    }
    return t;
}

CPP
refused n_span_from_span "span 't' is modeled only as a view of a whole vector or string this body tracks" <<'CPP'
verified unsigned n_span_from_span()
    ensures (result == result)
{
    std::vector<unsigned> v{1u};
    std::span<unsigned> s(v);
    std::span<unsigned> t(s);
    return 0u;
}

CPP
refused n_span_assigned "'std::span::operator=' is modeled only on a vector or string this body names directly" <<'CPP'
verified unsigned n_span_assigned()
    ensures (result == result)
{
    std::vector<unsigned> v{1u};
    std::vector<unsigned> w{1u};
    std::span<unsigned> s(v);
    s = std::span<unsigned>(w);
    return 0u;
}

CPP
refused n_span_subspan "local 't' of type 'std::span<unsigned int>' is not initialized by a modeled constructor" <<'CPP'
verified unsigned n_span_subspan()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::span<unsigned> s(v);
    std::span<unsigned> t = s.subspan(1ul);
    return 0u;
}

CPP
refused n_span_first "'std::span::front' is not a modeled operation of std::span" <<'CPP'
verified unsigned n_span_first()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::span<unsigned> s(v);
    return s.front();
}

CPP
refused n_span_result "it returns a span, and a view is not returned: it could outlive the storage it views" <<'CPP'
verified std::span<unsigned> n_span_result(std::vector<unsigned>& v)
    ensures (true)
{
    return std::span<unsigned>(v);
}

CPP
refused n_span_of_array_local "span 's' is modeled only as a view of a whole vector or string this body tracks" <<'CPP'
verified unsigned n_span_of_array_local()
    ensures (result == result)
{
    std::array<unsigned, 2> a{1u, 2u};
    std::span<unsigned> s(a);
    return s[0];
}

CPP
refused n_vector_of_array_ref "reference 'r' must bind a tracked local object" <<'CPP'
verified unsigned n_vector_of_array_ref(std::array<unsigned, 2>& a)
    ensures (result == result)
{
    unsigned& r = a[0];
    return r;
}

CPP
check

# --- Content invariants of a refined element type (STDMODEL-020, STDMODEL-027)
begin content
refused c_list_violates "this value is not shown to satisfy refinement type 'Positive'" <<'CPP'
verified unsigned c_list_violates()
    ensures (result == result)
{
    std::vector<Positive> v{1u, 0u};
    return 0u;
}

CPP
refused c_sized_value_init "this value is not shown to satisfy refinement type 'Positive'" <<'CPP'
verified unsigned c_sized_value_init()
    ensures (result == result)
{
    std::vector<Positive> v(3ul);
    return 0u;
}

CPP
refused c_fill_zero "this value is not shown to satisfy refinement type 'Positive'" <<'CPP'
verified unsigned c_fill_zero()
    ensures (result == result)
{
    std::vector<Positive> v(3ul, 0u);
    return 0u;
}

CPP
refused c_push_zero "this value is not shown to satisfy refinement type 'Positive'" <<'CPP'
verified unsigned c_push_zero()
    ensures (result == result)
{
    std::vector<Positive> v{1u};
    v.push_back(0u);
    return 0u;
}

CPP
refused c_push_unknown "this value is not shown to satisfy refinement type 'Positive'" <<'CPP'
verified unsigned c_push_unknown(unsigned x)
    ensures (result == result)
{
    std::vector<Positive> v{1u};
    v.push_back(x);
    return 0u;
}

CPP
refused c_write_zero "this value is not shown to satisfy refinement type 'Positive'" <<'CPP'
verified unsigned c_write_zero()
    ensures (result == result)
{
    std::vector<Positive> v{1u};
    v[0] = 0u;
    return 0u;
}

CPP
refused c_write_through_span_zero "this value is not shown to satisfy refinement type 'Positive'" <<'CPP'
verified unsigned c_write_through_span_zero()
    ensures (result == result)
{
    std::vector<Positive> v{1u};
    std::span<unsigned> s(v);
    s[0] = 0u;
    return 0u;
}

CPP
refused c_write_through_ref_zero "this value is not shown to satisfy refinement type 'Positive'" <<'CPP'
verified unsigned c_write_through_ref_zero()
    ensures (result == result)
{
    std::vector<Positive> v{1u};
    unsigned& r = v[0];
    r = 0u;
    return 0u;
}

CPP
refused c_copy_from_plain "the elements of 'w' are not known to satisfy 'Positive', which the elements of 'v' require" <<'CPP'
verified unsigned c_copy_from_plain()
    ensures (result == result)
{
    std::vector<unsigned> w{1u};
    std::vector<Positive> v = w;
    return 0u;
}

CPP
refused c_move_from_plain "the elements of 'w' are not known to satisfy 'Positive', which the elements of 'v' require" <<'CPP'
verified unsigned c_move_from_plain()
    ensures (result == result)
{
    std::vector<unsigned> w{1u};
    std::vector<Positive> v = std::move(w);
    return 0u;
}

CPP
refused c_assign_from_plain "the elements of 'w' are not known to satisfy 'Positive', which the elements of 'v' require" <<'CPP'
verified unsigned c_assign_from_plain()
    ensures (result == result)
{
    std::vector<unsigned> w{1u};
    std::vector<Positive> v{2u};
    v = w;
    return 0u;
}

CPP
refused c_mutable_call "'v' is passed to 'touch' by mutable reference, and its elements must satisfy 'Positive'" <<'CPP'
verified unsigned c_mutable_call()
    ensures (result == result)
{
    std::vector<Positive> v{1u};
    touch(v);
    return 0u;
}

CPP
refused c_writable_span_call "the elements of 'v' are handed to a callee that may write them, and nothing obliges it to write values satisfying 'Positive'" <<'CPP'
verified unsigned c_writable_span_call()
    ensures (result == result)
{
    std::vector<Positive> v{1u};
    fill(v);
    return 0u;
}

CPP
refused c_data_writable_call "the elements of 'v' are handed to a callee that may write them, and nothing obliges it to write values satisfying 'Positive'" <<'CPP'
verified unsigned c_data_writable_call()
    ensures (result == result)
{
    std::vector<Positive> v{1u};
    put(v.data(), v.size());
    return 0u;
}

CPP
refused c_after_unsafe_read "law 'c_after_unsafe_read element index' is not proven" <<'CPP'
verified unsigned c_after_unsafe_read()
    ensures (0u < result)
{
    std::vector<Positive> v{1u};
    unsafe {
        grow(v);
    }
    return v[0];
}

CPP
refused c_param_refused "parameter 'v' is a container of refined elements, whose element validity no call can establish" <<'CPP'
verified unsigned c_param_refused(std::vector<Positive> v)
    ensures (result == result)
{
    return 0u;
}

CPP
refused c_array_refused "local 'a' has type 'std::array<unsigned int, 2>', which is not modeled: its element type is written as the refinement 'Positive', which std::array does not state" <<'CPP'
verified unsigned c_array_refused()
    ensures (result == result)
{
    std::array<Positive, 2> a{1u, 2u};
    return 0u;
}

CPP
refused c_span_refused "parameter 's' is a container of refined elements, whose element validity no call can establish" <<'CPP'
verified unsigned c_span_refused(std::span<Positive> s)
    ensures (result == result)
{
    return 0u;
}

CPP
refused c_moved_into_from_refined_then_read_source "law 'c_moved_into_from_refined_then_read_source element index' is not proven" <<'CPP'
verified unsigned c_moved_into_from_refined_then_read_source()
    ensures (0u < result)
{
    std::vector<Positive> w{1u};
    std::vector<Positive> v = std::move(w);
    return w[0];
}

CPP
refused c_span_local_writable_call "the elements of 'v' are handed to a callee that may write them, and nothing obliges it to write values satisfying 'Positive'" <<'CPP'
verified unsigned c_span_local_writable_call()
    ensures (result == result)
{
    std::vector<Positive> v{1u};
    std::span<unsigned> s(v);
    fill(s);
    return 0u;
}

CPP
check

# --- Constness survives every conversion to a span and through data() (STDMODEL-016)
begin constness
refused x_const_container_writable "no matching function for call to 'put'" <<'CPP'
verified unsigned x_const_container_writable(const std::vector<unsigned>& v)
    ensures (result == result)
{
    put(v.data(), v.size());
    return 0u;
}

CPP
refused x_const_span_to_writable "no matching function for call to 'fill'" <<'CPP'
verified unsigned x_const_span_to_writable()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::span<const unsigned> s(v);
    fill(s);
    return 0u;
}

CPP
check

echo 'every edge case of the sequence subset is refused for its stated reason'
