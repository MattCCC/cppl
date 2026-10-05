#!/usr/bin/env bash
# SPEC: STDMODEL-015, STDMODEL-023, STDMODEL-025
# TRUST.md TCB-LIB-006
#
# The accepted twins of `negative/sequence_generations.sh`: every view and
# element reference survives what leaves its storage as it was, and every event
# applied to another container; each is proven and computes the value its
# contract states, which is the value the view reads.
set -euo pipefail
CPPL="$1"
WORK="$3"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/sequence-generations.XXXXXX")
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
#include <cstddef>
#include <cstdio>
#include <span>
#include <string>
#include <utility>
#include <vector>
void grow(std::vector<unsigned>& v) { v.push_back(2u); }
void grow_string(std::string& s) { s += 'z'; }
verified void touch(std::vector<unsigned>& x) ensures (true) { x.push_back(0u); }
verified void touch_string(std::string& x) ensures (true) { x += 'q'; }
verified void keep(const std::vector<unsigned>& x) ensures (true) { }
verified std::size_t keep_copy(std::vector<unsigned> x) ensures (true) { x.push_back(1u); return x.size(); }
verified void look(std::span<const unsigned> s) expects (readable(s)) ensures (true) { }
verified void fill(std::span<unsigned> s) expects (writable(s)) ensures (true) { if (0ul < s.size()) { s[0] = 0u; } }
CPP
}

# --- What leaves a vector's storage as it was keeps every view of it (STDMODEL-015, STDMODEL-023)
begin vector_kept
accepted vector_kept_element_write__reference 1 <<'CPP'
verified unsigned vector_kept_element_write__reference()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    v[1] = 9u;
    return r;
}

CPP
accepted vector_kept_element_write__const_reference 1 <<'CPP'
verified unsigned vector_kept_element_write__const_reference()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    const unsigned& r = v[0];
    v[1] = 9u;
    return r;
}

CPP
accepted vector_kept_element_write__reference_write 5 <<'CPP'
verified unsigned vector_kept_element_write__reference_write()
    ensures (result == 5u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    v[1] = 9u;
    r = 5u;
    return v[0];
}

CPP
accepted vector_kept_element_write__span 1 <<'CPP'
verified unsigned vector_kept_element_write__span()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    v[1] = 9u;
    return s[0];
}

CPP
accepted vector_kept_element_write__span_write 5 <<'CPP'
verified unsigned vector_kept_element_write__span_write()
    ensures (result == 5u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    v[1] = 9u;
    s[0] = 5u;
    return v[0];
}

CPP
accepted vector_kept_element_write__const_span 1 <<'CPP'
verified unsigned vector_kept_element_write__const_span()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<const unsigned> s(v);
    v[1] = 9u;
    return s[0];
}

CPP
accepted vector_kept_element_write__span_copy_initialized 1 <<'CPP'
verified unsigned vector_kept_element_write__span_copy_initialized()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s = v;
    v[1] = 9u;
    return s[0];
}

CPP
accepted vector_kept_element_write__span_braced 1 <<'CPP'
verified unsigned vector_kept_element_write__span_braced()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s{v};
    v[1] = 9u;
    return s[0];
}

CPP
accepted vector_kept_element_write__span_size 2 <<'CPP'
verified unsigned vector_kept_element_write__span_size()
    ensures (result == 2u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    v[1] = 9u;
    return static_cast<unsigned>(s.size());
}

CPP
accepted vector_kept_element_write__span_empty 0 <<'CPP'
verified unsigned vector_kept_element_write__span_empty()
    ensures (result == 0u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    v[1] = 9u;
    return s.empty() ? 1u : 0u;
}

CPP
accepted vector_kept_const_call__reference 1 <<'CPP'
verified unsigned vector_kept_const_call__reference()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    keep(v);
    return r;
}

CPP
accepted vector_kept_const_call__const_reference 1 <<'CPP'
verified unsigned vector_kept_const_call__const_reference()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    const unsigned& r = v[0];
    keep(v);
    return r;
}

CPP
accepted vector_kept_const_call__reference_write 5 <<'CPP'
verified unsigned vector_kept_const_call__reference_write()
    ensures (result == 5u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    keep(v);
    r = 5u;
    return v[0];
}

CPP
accepted vector_kept_const_call__span 1 <<'CPP'
verified unsigned vector_kept_const_call__span()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    keep(v);
    return s[0];
}

CPP
accepted vector_kept_const_call__span_write 5 <<'CPP'
verified unsigned vector_kept_const_call__span_write()
    ensures (result == 5u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    keep(v);
    s[0] = 5u;
    return v[0];
}

CPP
accepted vector_kept_const_call__const_span 1 <<'CPP'
verified unsigned vector_kept_const_call__const_span()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<const unsigned> s(v);
    keep(v);
    return s[0];
}

CPP
accepted vector_kept_const_call__span_copy_initialized 1 <<'CPP'
verified unsigned vector_kept_const_call__span_copy_initialized()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s = v;
    keep(v);
    return s[0];
}

CPP
accepted vector_kept_const_call__span_braced 1 <<'CPP'
verified unsigned vector_kept_const_call__span_braced()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s{v};
    keep(v);
    return s[0];
}

CPP
accepted vector_kept_const_call__span_size 2 <<'CPP'
verified unsigned vector_kept_const_call__span_size()
    ensures (result == 2u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    keep(v);
    return static_cast<unsigned>(s.size());
}

CPP
accepted vector_kept_const_call__span_empty 0 <<'CPP'
verified unsigned vector_kept_const_call__span_empty()
    ensures (result == 0u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    keep(v);
    return s.empty() ? 1u : 0u;
}

CPP
accepted vector_kept_copy_call__reference 1 <<'CPP'
verified unsigned vector_kept_copy_call__reference()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    const std::size_t copied = keep_copy(v);
    return r;
}

CPP
accepted vector_kept_copy_call__const_reference 1 <<'CPP'
verified unsigned vector_kept_copy_call__const_reference()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    const unsigned& r = v[0];
    const std::size_t copied = keep_copy(v);
    return r;
}

CPP
accepted vector_kept_copy_call__reference_write 5 <<'CPP'
verified unsigned vector_kept_copy_call__reference_write()
    ensures (result == 5u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    const std::size_t copied = keep_copy(v);
    r = 5u;
    return v[0];
}

CPP
accepted vector_kept_copy_call__span 1 <<'CPP'
verified unsigned vector_kept_copy_call__span()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    const std::size_t copied = keep_copy(v);
    return s[0];
}

CPP
accepted vector_kept_copy_call__span_write 5 <<'CPP'
verified unsigned vector_kept_copy_call__span_write()
    ensures (result == 5u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    const std::size_t copied = keep_copy(v);
    s[0] = 5u;
    return v[0];
}

CPP
accepted vector_kept_copy_call__const_span 1 <<'CPP'
verified unsigned vector_kept_copy_call__const_span()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<const unsigned> s(v);
    const std::size_t copied = keep_copy(v);
    return s[0];
}

CPP
accepted vector_kept_copy_call__span_copy_initialized 1 <<'CPP'
verified unsigned vector_kept_copy_call__span_copy_initialized()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s = v;
    const std::size_t copied = keep_copy(v);
    return s[0];
}

CPP
accepted vector_kept_copy_call__span_braced 1 <<'CPP'
verified unsigned vector_kept_copy_call__span_braced()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s{v};
    const std::size_t copied = keep_copy(v);
    return s[0];
}

CPP
accepted vector_kept_copy_call__span_size 2 <<'CPP'
verified unsigned vector_kept_copy_call__span_size()
    ensures (result == 2u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    const std::size_t copied = keep_copy(v);
    return static_cast<unsigned>(s.size());
}

CPP
accepted vector_kept_copy_call__span_empty 0 <<'CPP'
verified unsigned vector_kept_copy_call__span_empty()
    ensures (result == 0u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    const std::size_t copied = keep_copy(v);
    return s.empty() ? 1u : 0u;
}

CPP
accepted vector_kept_copy_out__reference 1 <<'CPP'
verified unsigned vector_kept_copy_out__reference()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    std::vector<unsigned> m = v;
    return r;
}

CPP
accepted vector_kept_copy_out__const_reference 1 <<'CPP'
verified unsigned vector_kept_copy_out__const_reference()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    const unsigned& r = v[0];
    std::vector<unsigned> m = v;
    return r;
}

CPP
accepted vector_kept_copy_out__reference_write 5 <<'CPP'
verified unsigned vector_kept_copy_out__reference_write()
    ensures (result == 5u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    std::vector<unsigned> m = v;
    r = 5u;
    return v[0];
}

CPP
accepted vector_kept_copy_out__span 1 <<'CPP'
verified unsigned vector_kept_copy_out__span()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    std::vector<unsigned> m = v;
    return s[0];
}

CPP
accepted vector_kept_copy_out__span_write 5 <<'CPP'
verified unsigned vector_kept_copy_out__span_write()
    ensures (result == 5u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    std::vector<unsigned> m = v;
    s[0] = 5u;
    return v[0];
}

CPP
accepted vector_kept_copy_out__const_span 1 <<'CPP'
verified unsigned vector_kept_copy_out__const_span()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<const unsigned> s(v);
    std::vector<unsigned> m = v;
    return s[0];
}

CPP
accepted vector_kept_copy_out__span_copy_initialized 1 <<'CPP'
verified unsigned vector_kept_copy_out__span_copy_initialized()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s = v;
    std::vector<unsigned> m = v;
    return s[0];
}

CPP
accepted vector_kept_copy_out__span_braced 1 <<'CPP'
verified unsigned vector_kept_copy_out__span_braced()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s{v};
    std::vector<unsigned> m = v;
    return s[0];
}

CPP
accepted vector_kept_copy_out__span_size 2 <<'CPP'
verified unsigned vector_kept_copy_out__span_size()
    ensures (result == 2u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    std::vector<unsigned> m = v;
    return static_cast<unsigned>(s.size());
}

CPP
accepted vector_kept_copy_out__span_empty 0 <<'CPP'
verified unsigned vector_kept_copy_out__span_empty()
    ensures (result == 0u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    std::vector<unsigned> m = v;
    return s.empty() ? 1u : 0u;
}

CPP
accepted vector_kept_copy_assign_out__reference 1 <<'CPP'
verified unsigned vector_kept_copy_assign_out__reference()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    w = v;
    return r;
}

CPP
accepted vector_kept_copy_assign_out__const_reference 1 <<'CPP'
verified unsigned vector_kept_copy_assign_out__const_reference()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    const unsigned& r = v[0];
    w = v;
    return r;
}

CPP
accepted vector_kept_copy_assign_out__reference_write 5 <<'CPP'
verified unsigned vector_kept_copy_assign_out__reference_write()
    ensures (result == 5u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    w = v;
    r = 5u;
    return v[0];
}

CPP
accepted vector_kept_copy_assign_out__span 1 <<'CPP'
verified unsigned vector_kept_copy_assign_out__span()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    w = v;
    return s[0];
}

CPP
accepted vector_kept_copy_assign_out__span_write 5 <<'CPP'
verified unsigned vector_kept_copy_assign_out__span_write()
    ensures (result == 5u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    w = v;
    s[0] = 5u;
    return v[0];
}

CPP
accepted vector_kept_copy_assign_out__const_span 1 <<'CPP'
verified unsigned vector_kept_copy_assign_out__const_span()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<const unsigned> s(v);
    w = v;
    return s[0];
}

CPP
accepted vector_kept_copy_assign_out__span_copy_initialized 1 <<'CPP'
verified unsigned vector_kept_copy_assign_out__span_copy_initialized()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s = v;
    w = v;
    return s[0];
}

CPP
accepted vector_kept_copy_assign_out__span_braced 1 <<'CPP'
verified unsigned vector_kept_copy_assign_out__span_braced()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s{v};
    w = v;
    return s[0];
}

CPP
accepted vector_kept_copy_assign_out__span_size 2 <<'CPP'
verified unsigned vector_kept_copy_assign_out__span_size()
    ensures (result == 2u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    w = v;
    return static_cast<unsigned>(s.size());
}

CPP
accepted vector_kept_copy_assign_out__span_empty 0 <<'CPP'
verified unsigned vector_kept_copy_assign_out__span_empty()
    ensures (result == 0u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    w = v;
    return s.empty() ? 1u : 0u;
}

CPP
accepted vector_kept_readable_view_call__reference 1 <<'CPP'
verified unsigned vector_kept_readable_view_call__reference()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    look(v);
    return r;
}

CPP
accepted vector_kept_readable_view_call__const_reference 1 <<'CPP'
verified unsigned vector_kept_readable_view_call__const_reference()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    const unsigned& r = v[0];
    look(v);
    return r;
}

CPP
accepted vector_kept_readable_view_call__reference_write 5 <<'CPP'
verified unsigned vector_kept_readable_view_call__reference_write()
    ensures (result == 5u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    look(v);
    r = 5u;
    return v[0];
}

CPP
accepted vector_kept_readable_view_call__span 1 <<'CPP'
verified unsigned vector_kept_readable_view_call__span()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    look(v);
    return s[0];
}

CPP
accepted vector_kept_readable_view_call__span_write 5 <<'CPP'
verified unsigned vector_kept_readable_view_call__span_write()
    ensures (result == 5u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    look(v);
    s[0] = 5u;
    return v[0];
}

CPP
accepted vector_kept_readable_view_call__const_span 1 <<'CPP'
verified unsigned vector_kept_readable_view_call__const_span()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<const unsigned> s(v);
    look(v);
    return s[0];
}

CPP
accepted vector_kept_readable_view_call__span_copy_initialized 1 <<'CPP'
verified unsigned vector_kept_readable_view_call__span_copy_initialized()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s = v;
    look(v);
    return s[0];
}

CPP
accepted vector_kept_readable_view_call__span_braced 1 <<'CPP'
verified unsigned vector_kept_readable_view_call__span_braced()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s{v};
    look(v);
    return s[0];
}

CPP
accepted vector_kept_readable_view_call__span_size 2 <<'CPP'
verified unsigned vector_kept_readable_view_call__span_size()
    ensures (result == 2u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    look(v);
    return static_cast<unsigned>(s.size());
}

CPP
accepted vector_kept_readable_view_call__span_empty 0 <<'CPP'
verified unsigned vector_kept_readable_view_call__span_empty()
    ensures (result == 0u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    look(v);
    return s.empty() ? 1u : 0u;
}

CPP
accepted vector_kept_length_read__reference 1 <<'CPP'
verified unsigned vector_kept_length_read__reference()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    const std::size_t n = v.size();
    return r;
}

CPP
accepted vector_kept_length_read__const_reference 1 <<'CPP'
verified unsigned vector_kept_length_read__const_reference()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    const unsigned& r = v[0];
    const std::size_t n = v.size();
    return r;
}

CPP
accepted vector_kept_length_read__reference_write 5 <<'CPP'
verified unsigned vector_kept_length_read__reference_write()
    ensures (result == 5u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    const std::size_t n = v.size();
    r = 5u;
    return v[0];
}

CPP
accepted vector_kept_length_read__span 1 <<'CPP'
verified unsigned vector_kept_length_read__span()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    const std::size_t n = v.size();
    return s[0];
}

CPP
accepted vector_kept_length_read__span_write 5 <<'CPP'
verified unsigned vector_kept_length_read__span_write()
    ensures (result == 5u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    const std::size_t n = v.size();
    s[0] = 5u;
    return v[0];
}

CPP
accepted vector_kept_length_read__const_span 1 <<'CPP'
verified unsigned vector_kept_length_read__const_span()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<const unsigned> s(v);
    const std::size_t n = v.size();
    return s[0];
}

CPP
accepted vector_kept_length_read__span_copy_initialized 1 <<'CPP'
verified unsigned vector_kept_length_read__span_copy_initialized()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s = v;
    const std::size_t n = v.size();
    return s[0];
}

CPP
accepted vector_kept_length_read__span_braced 1 <<'CPP'
verified unsigned vector_kept_length_read__span_braced()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s{v};
    const std::size_t n = v.size();
    return s[0];
}

CPP
accepted vector_kept_length_read__span_size 2 <<'CPP'
verified unsigned vector_kept_length_read__span_size()
    ensures (result == 2u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    const std::size_t n = v.size();
    return static_cast<unsigned>(s.size());
}

CPP
accepted vector_kept_length_read__span_empty 0 <<'CPP'
verified unsigned vector_kept_length_read__span_empty()
    ensures (result == 0u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    const std::size_t n = v.size();
    return s.empty() ? 1u : 0u;
}

CPP
check

# --- Every such event on another vector leaves the views of this one (STDMODEL-015, STDMODEL-025)
begin vector_other
accepted vector_other_push_back__reference 1 <<'CPP'
verified unsigned vector_other_push_back__reference()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    w.push_back(3u);
    return r;
}

CPP
accepted vector_other_push_back__const_reference 1 <<'CPP'
verified unsigned vector_other_push_back__const_reference()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    const unsigned& r = v[0];
    w.push_back(3u);
    return r;
}

CPP
accepted vector_other_push_back__reference_write 5 <<'CPP'
verified unsigned vector_other_push_back__reference_write()
    ensures (result == 5u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    w.push_back(3u);
    r = 5u;
    return v[0];
}

CPP
accepted vector_other_push_back__span 1 <<'CPP'
verified unsigned vector_other_push_back__span()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    w.push_back(3u);
    return s[0];
}

CPP
accepted vector_other_push_back__span_write 5 <<'CPP'
verified unsigned vector_other_push_back__span_write()
    ensures (result == 5u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    w.push_back(3u);
    s[0] = 5u;
    return v[0];
}

CPP
accepted vector_other_push_back__const_span 1 <<'CPP'
verified unsigned vector_other_push_back__const_span()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<const unsigned> s(v);
    w.push_back(3u);
    return s[0];
}

CPP
accepted vector_other_push_back__span_copy_initialized 1 <<'CPP'
verified unsigned vector_other_push_back__span_copy_initialized()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s = v;
    w.push_back(3u);
    return s[0];
}

CPP
accepted vector_other_push_back__span_braced 1 <<'CPP'
verified unsigned vector_other_push_back__span_braced()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s{v};
    w.push_back(3u);
    return s[0];
}

CPP
accepted vector_other_push_back__span_size 2 <<'CPP'
verified unsigned vector_other_push_back__span_size()
    ensures (result == 2u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    w.push_back(3u);
    return static_cast<unsigned>(s.size());
}

CPP
accepted vector_other_push_back__span_empty 0 <<'CPP'
verified unsigned vector_other_push_back__span_empty()
    ensures (result == 0u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    w.push_back(3u);
    return s.empty() ? 1u : 0u;
}

CPP
accepted vector_other_pop_back__reference 1 <<'CPP'
verified unsigned vector_other_pop_back__reference()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    w.pop_back();
    return r;
}

CPP
accepted vector_other_pop_back__const_reference 1 <<'CPP'
verified unsigned vector_other_pop_back__const_reference()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    const unsigned& r = v[0];
    w.pop_back();
    return r;
}

CPP
accepted vector_other_pop_back__reference_write 5 <<'CPP'
verified unsigned vector_other_pop_back__reference_write()
    ensures (result == 5u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    w.pop_back();
    r = 5u;
    return v[0];
}

CPP
accepted vector_other_pop_back__span 1 <<'CPP'
verified unsigned vector_other_pop_back__span()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    w.pop_back();
    return s[0];
}

CPP
accepted vector_other_pop_back__span_write 5 <<'CPP'
verified unsigned vector_other_pop_back__span_write()
    ensures (result == 5u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    w.pop_back();
    s[0] = 5u;
    return v[0];
}

CPP
accepted vector_other_pop_back__const_span 1 <<'CPP'
verified unsigned vector_other_pop_back__const_span()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<const unsigned> s(v);
    w.pop_back();
    return s[0];
}

CPP
accepted vector_other_pop_back__span_copy_initialized 1 <<'CPP'
verified unsigned vector_other_pop_back__span_copy_initialized()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s = v;
    w.pop_back();
    return s[0];
}

CPP
accepted vector_other_pop_back__span_braced 1 <<'CPP'
verified unsigned vector_other_pop_back__span_braced()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s{v};
    w.pop_back();
    return s[0];
}

CPP
accepted vector_other_pop_back__span_size 2 <<'CPP'
verified unsigned vector_other_pop_back__span_size()
    ensures (result == 2u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    w.pop_back();
    return static_cast<unsigned>(s.size());
}

CPP
accepted vector_other_pop_back__span_empty 0 <<'CPP'
verified unsigned vector_other_pop_back__span_empty()
    ensures (result == 0u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    w.pop_back();
    return s.empty() ? 1u : 0u;
}

CPP
accepted vector_other_clear__reference 1 <<'CPP'
verified unsigned vector_other_clear__reference()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    w.clear();
    return r;
}

CPP
accepted vector_other_clear__const_reference 1 <<'CPP'
verified unsigned vector_other_clear__const_reference()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    const unsigned& r = v[0];
    w.clear();
    return r;
}

CPP
accepted vector_other_clear__reference_write 5 <<'CPP'
verified unsigned vector_other_clear__reference_write()
    ensures (result == 5u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    w.clear();
    r = 5u;
    return v[0];
}

CPP
accepted vector_other_clear__span 1 <<'CPP'
verified unsigned vector_other_clear__span()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    w.clear();
    return s[0];
}

CPP
accepted vector_other_clear__span_write 5 <<'CPP'
verified unsigned vector_other_clear__span_write()
    ensures (result == 5u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    w.clear();
    s[0] = 5u;
    return v[0];
}

CPP
accepted vector_other_clear__const_span 1 <<'CPP'
verified unsigned vector_other_clear__const_span()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<const unsigned> s(v);
    w.clear();
    return s[0];
}

CPP
accepted vector_other_clear__span_copy_initialized 1 <<'CPP'
verified unsigned vector_other_clear__span_copy_initialized()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s = v;
    w.clear();
    return s[0];
}

CPP
accepted vector_other_clear__span_braced 1 <<'CPP'
verified unsigned vector_other_clear__span_braced()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s{v};
    w.clear();
    return s[0];
}

CPP
accepted vector_other_clear__span_size 2 <<'CPP'
verified unsigned vector_other_clear__span_size()
    ensures (result == 2u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    w.clear();
    return static_cast<unsigned>(s.size());
}

CPP
accepted vector_other_clear__span_empty 0 <<'CPP'
verified unsigned vector_other_clear__span_empty()
    ensures (result == 0u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    w.clear();
    return s.empty() ? 1u : 0u;
}

CPP
accepted vector_other_reserve__reference 1 <<'CPP'
verified unsigned vector_other_reserve__reference()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    w.reserve(100ul);
    return r;
}

CPP
accepted vector_other_reserve__const_reference 1 <<'CPP'
verified unsigned vector_other_reserve__const_reference()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    const unsigned& r = v[0];
    w.reserve(100ul);
    return r;
}

CPP
accepted vector_other_reserve__reference_write 5 <<'CPP'
verified unsigned vector_other_reserve__reference_write()
    ensures (result == 5u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    w.reserve(100ul);
    r = 5u;
    return v[0];
}

CPP
accepted vector_other_reserve__span 1 <<'CPP'
verified unsigned vector_other_reserve__span()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    w.reserve(100ul);
    return s[0];
}

CPP
accepted vector_other_reserve__span_write 5 <<'CPP'
verified unsigned vector_other_reserve__span_write()
    ensures (result == 5u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    w.reserve(100ul);
    s[0] = 5u;
    return v[0];
}

CPP
accepted vector_other_reserve__const_span 1 <<'CPP'
verified unsigned vector_other_reserve__const_span()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<const unsigned> s(v);
    w.reserve(100ul);
    return s[0];
}

CPP
accepted vector_other_reserve__span_copy_initialized 1 <<'CPP'
verified unsigned vector_other_reserve__span_copy_initialized()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s = v;
    w.reserve(100ul);
    return s[0];
}

CPP
accepted vector_other_reserve__span_braced 1 <<'CPP'
verified unsigned vector_other_reserve__span_braced()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s{v};
    w.reserve(100ul);
    return s[0];
}

CPP
accepted vector_other_reserve__span_size 2 <<'CPP'
verified unsigned vector_other_reserve__span_size()
    ensures (result == 2u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    w.reserve(100ul);
    return static_cast<unsigned>(s.size());
}

CPP
accepted vector_other_reserve__span_empty 0 <<'CPP'
verified unsigned vector_other_reserve__span_empty()
    ensures (result == 0u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    w.reserve(100ul);
    return s.empty() ? 1u : 0u;
}

CPP
accepted vector_other_copy_assign__reference 1 <<'CPP'
verified unsigned vector_other_copy_assign__reference()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    w = z;
    return r;
}

CPP
accepted vector_other_copy_assign__const_reference 1 <<'CPP'
verified unsigned vector_other_copy_assign__const_reference()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    const unsigned& r = v[0];
    w = z;
    return r;
}

CPP
accepted vector_other_copy_assign__reference_write 5 <<'CPP'
verified unsigned vector_other_copy_assign__reference_write()
    ensures (result == 5u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    w = z;
    r = 5u;
    return v[0];
}

CPP
accepted vector_other_copy_assign__span 1 <<'CPP'
verified unsigned vector_other_copy_assign__span()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    w = z;
    return s[0];
}

CPP
accepted vector_other_copy_assign__span_write 5 <<'CPP'
verified unsigned vector_other_copy_assign__span_write()
    ensures (result == 5u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    w = z;
    s[0] = 5u;
    return v[0];
}

CPP
accepted vector_other_copy_assign__const_span 1 <<'CPP'
verified unsigned vector_other_copy_assign__const_span()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<const unsigned> s(v);
    w = z;
    return s[0];
}

CPP
accepted vector_other_copy_assign__span_copy_initialized 1 <<'CPP'
verified unsigned vector_other_copy_assign__span_copy_initialized()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s = v;
    w = z;
    return s[0];
}

CPP
accepted vector_other_copy_assign__span_braced 1 <<'CPP'
verified unsigned vector_other_copy_assign__span_braced()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s{v};
    w = z;
    return s[0];
}

CPP
accepted vector_other_copy_assign__span_size 2 <<'CPP'
verified unsigned vector_other_copy_assign__span_size()
    ensures (result == 2u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    w = z;
    return static_cast<unsigned>(s.size());
}

CPP
accepted vector_other_copy_assign__span_empty 0 <<'CPP'
verified unsigned vector_other_copy_assign__span_empty()
    ensures (result == 0u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    w = z;
    return s.empty() ? 1u : 0u;
}

CPP
accepted vector_other_move_assign__reference 1 <<'CPP'
verified unsigned vector_other_move_assign__reference()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    w = std::move(z);
    return r;
}

CPP
accepted vector_other_move_assign__const_reference 1 <<'CPP'
verified unsigned vector_other_move_assign__const_reference()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    const unsigned& r = v[0];
    w = std::move(z);
    return r;
}

CPP
accepted vector_other_move_assign__reference_write 5 <<'CPP'
verified unsigned vector_other_move_assign__reference_write()
    ensures (result == 5u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    w = std::move(z);
    r = 5u;
    return v[0];
}

CPP
accepted vector_other_move_assign__span 1 <<'CPP'
verified unsigned vector_other_move_assign__span()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    w = std::move(z);
    return s[0];
}

CPP
accepted vector_other_move_assign__span_write 5 <<'CPP'
verified unsigned vector_other_move_assign__span_write()
    ensures (result == 5u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    w = std::move(z);
    s[0] = 5u;
    return v[0];
}

CPP
accepted vector_other_move_assign__const_span 1 <<'CPP'
verified unsigned vector_other_move_assign__const_span()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<const unsigned> s(v);
    w = std::move(z);
    return s[0];
}

CPP
accepted vector_other_move_assign__span_copy_initialized 1 <<'CPP'
verified unsigned vector_other_move_assign__span_copy_initialized()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s = v;
    w = std::move(z);
    return s[0];
}

CPP
accepted vector_other_move_assign__span_braced 1 <<'CPP'
verified unsigned vector_other_move_assign__span_braced()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s{v};
    w = std::move(z);
    return s[0];
}

CPP
accepted vector_other_move_assign__span_size 2 <<'CPP'
verified unsigned vector_other_move_assign__span_size()
    ensures (result == 2u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    w = std::move(z);
    return static_cast<unsigned>(s.size());
}

CPP
accepted vector_other_move_assign__span_empty 0 <<'CPP'
verified unsigned vector_other_move_assign__span_empty()
    ensures (result == 0u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    w = std::move(z);
    return s.empty() ? 1u : 0u;
}

CPP
accepted vector_other_move_from__reference 1 <<'CPP'
verified unsigned vector_other_move_from__reference()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    std::vector<unsigned> m = std::move(w);
    return r;
}

CPP
accepted vector_other_move_from__const_reference 1 <<'CPP'
verified unsigned vector_other_move_from__const_reference()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    const unsigned& r = v[0];
    std::vector<unsigned> m = std::move(w);
    return r;
}

CPP
accepted vector_other_move_from__reference_write 5 <<'CPP'
verified unsigned vector_other_move_from__reference_write()
    ensures (result == 5u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    std::vector<unsigned> m = std::move(w);
    r = 5u;
    return v[0];
}

CPP
accepted vector_other_move_from__span 1 <<'CPP'
verified unsigned vector_other_move_from__span()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    std::vector<unsigned> m = std::move(w);
    return s[0];
}

CPP
accepted vector_other_move_from__span_write 5 <<'CPP'
verified unsigned vector_other_move_from__span_write()
    ensures (result == 5u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    std::vector<unsigned> m = std::move(w);
    s[0] = 5u;
    return v[0];
}

CPP
accepted vector_other_move_from__const_span 1 <<'CPP'
verified unsigned vector_other_move_from__const_span()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<const unsigned> s(v);
    std::vector<unsigned> m = std::move(w);
    return s[0];
}

CPP
accepted vector_other_move_from__span_copy_initialized 1 <<'CPP'
verified unsigned vector_other_move_from__span_copy_initialized()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s = v;
    std::vector<unsigned> m = std::move(w);
    return s[0];
}

CPP
accepted vector_other_move_from__span_braced 1 <<'CPP'
verified unsigned vector_other_move_from__span_braced()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s{v};
    std::vector<unsigned> m = std::move(w);
    return s[0];
}

CPP
accepted vector_other_move_from__span_size 2 <<'CPP'
verified unsigned vector_other_move_from__span_size()
    ensures (result == 2u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    std::vector<unsigned> m = std::move(w);
    return static_cast<unsigned>(s.size());
}

CPP
accepted vector_other_move_from__span_empty 0 <<'CPP'
verified unsigned vector_other_move_from__span_empty()
    ensures (result == 0u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    std::vector<unsigned> m = std::move(w);
    return s.empty() ? 1u : 0u;
}

CPP
accepted vector_other_mutable_call__reference 1 <<'CPP'
verified unsigned vector_other_mutable_call__reference()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    touch(w);
    return r;
}

CPP
accepted vector_other_mutable_call__const_reference 1 <<'CPP'
verified unsigned vector_other_mutable_call__const_reference()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    const unsigned& r = v[0];
    touch(w);
    return r;
}

CPP
accepted vector_other_mutable_call__reference_write 5 <<'CPP'
verified unsigned vector_other_mutable_call__reference_write()
    ensures (result == 5u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    touch(w);
    r = 5u;
    return v[0];
}

CPP
accepted vector_other_mutable_call__span 1 <<'CPP'
verified unsigned vector_other_mutable_call__span()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    touch(w);
    return s[0];
}

CPP
accepted vector_other_mutable_call__span_write 5 <<'CPP'
verified unsigned vector_other_mutable_call__span_write()
    ensures (result == 5u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    touch(w);
    s[0] = 5u;
    return v[0];
}

CPP
accepted vector_other_mutable_call__const_span 1 <<'CPP'
verified unsigned vector_other_mutable_call__const_span()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<const unsigned> s(v);
    touch(w);
    return s[0];
}

CPP
accepted vector_other_mutable_call__span_copy_initialized 1 <<'CPP'
verified unsigned vector_other_mutable_call__span_copy_initialized()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s = v;
    touch(w);
    return s[0];
}

CPP
accepted vector_other_mutable_call__span_braced 1 <<'CPP'
verified unsigned vector_other_mutable_call__span_braced()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s{v};
    touch(w);
    return s[0];
}

CPP
accepted vector_other_mutable_call__span_size 2 <<'CPP'
verified unsigned vector_other_mutable_call__span_size()
    ensures (result == 2u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    touch(w);
    return static_cast<unsigned>(s.size());
}

CPP
accepted vector_other_mutable_call__span_empty 0 <<'CPP'
verified unsigned vector_other_mutable_call__span_empty()
    ensures (result == 0u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    touch(w);
    return s.empty() ? 1u : 0u;
}

CPP
accepted vector_other_unsafe_block__reference 1 <<'CPP'
verified unsigned vector_other_unsafe_block__reference()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    unsafe {
        grow(w);
    }
    return r;
}

CPP
accepted vector_other_unsafe_block__const_reference 1 <<'CPP'
verified unsigned vector_other_unsafe_block__const_reference()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    const unsigned& r = v[0];
    unsafe {
        grow(w);
    }
    return r;
}

CPP
accepted vector_other_unsafe_block__reference_write 5 <<'CPP'
verified unsigned vector_other_unsafe_block__reference_write()
    ensures (result == 5u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    unsafe {
        grow(w);
    }
    r = 5u;
    return v[0];
}

CPP
accepted vector_other_unsafe_block__span 1 <<'CPP'
verified unsigned vector_other_unsafe_block__span()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    unsafe {
        grow(w);
    }
    return s[0];
}

CPP
accepted vector_other_unsafe_block__span_write 5 <<'CPP'
verified unsigned vector_other_unsafe_block__span_write()
    ensures (result == 5u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    unsafe {
        grow(w);
    }
    s[0] = 5u;
    return v[0];
}

CPP
accepted vector_other_unsafe_block__const_span 1 <<'CPP'
verified unsigned vector_other_unsafe_block__const_span()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<const unsigned> s(v);
    unsafe {
        grow(w);
    }
    return s[0];
}

CPP
accepted vector_other_unsafe_block__span_copy_initialized 1 <<'CPP'
verified unsigned vector_other_unsafe_block__span_copy_initialized()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s = v;
    unsafe {
        grow(w);
    }
    return s[0];
}

CPP
accepted vector_other_unsafe_block__span_braced 1 <<'CPP'
verified unsigned vector_other_unsafe_block__span_braced()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s{v};
    unsafe {
        grow(w);
    }
    return s[0];
}

CPP
accepted vector_other_unsafe_block__span_size 2 <<'CPP'
verified unsigned vector_other_unsafe_block__span_size()
    ensures (result == 2u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    unsafe {
        grow(w);
    }
    return static_cast<unsigned>(s.size());
}

CPP
accepted vector_other_unsafe_block__span_empty 0 <<'CPP'
verified unsigned vector_other_unsafe_block__span_empty()
    ensures (result == 0u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    unsafe {
        grow(w);
    }
    return s.empty() ? 1u : 0u;
}

CPP
accepted vector_other_loop__reference 1 <<'CPP'
verified unsigned vector_other_loop__reference()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    std::size_t k = 0ul;
    while (k < 1ul)
        invariant (k <= 1ul)
        decreases (1ul - k)
    {
        w.push_back(0u);
        ++k;
    }
    return r;
}

CPP
accepted vector_other_loop__const_reference 1 <<'CPP'
verified unsigned vector_other_loop__const_reference()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    const unsigned& r = v[0];
    std::size_t k = 0ul;
    while (k < 1ul)
        invariant (k <= 1ul)
        decreases (1ul - k)
    {
        w.push_back(0u);
        ++k;
    }
    return r;
}

CPP
accepted vector_other_loop__reference_write 5 <<'CPP'
verified unsigned vector_other_loop__reference_write()
    ensures (result == 5u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    std::size_t k = 0ul;
    while (k < 1ul)
        invariant (k <= 1ul)
        decreases (1ul - k)
    {
        w.push_back(0u);
        ++k;
    }
    r = 5u;
    return v[0];
}

CPP
accepted vector_other_loop__span 1 <<'CPP'
verified unsigned vector_other_loop__span()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    std::size_t k = 0ul;
    while (k < 1ul)
        invariant (k <= 1ul)
        decreases (1ul - k)
    {
        w.push_back(0u);
        ++k;
    }
    return s[0];
}

CPP
accepted vector_other_loop__span_write 5 <<'CPP'
verified unsigned vector_other_loop__span_write()
    ensures (result == 5u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    std::size_t k = 0ul;
    while (k < 1ul)
        invariant (k <= 1ul)
        decreases (1ul - k)
    {
        w.push_back(0u);
        ++k;
    }
    s[0] = 5u;
    return v[0];
}

CPP
accepted vector_other_loop__const_span 1 <<'CPP'
verified unsigned vector_other_loop__const_span()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<const unsigned> s(v);
    std::size_t k = 0ul;
    while (k < 1ul)
        invariant (k <= 1ul)
        decreases (1ul - k)
    {
        w.push_back(0u);
        ++k;
    }
    return s[0];
}

CPP
accepted vector_other_loop__span_copy_initialized 1 <<'CPP'
verified unsigned vector_other_loop__span_copy_initialized()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s = v;
    std::size_t k = 0ul;
    while (k < 1ul)
        invariant (k <= 1ul)
        decreases (1ul - k)
    {
        w.push_back(0u);
        ++k;
    }
    return s[0];
}

CPP
accepted vector_other_loop__span_braced 1 <<'CPP'
verified unsigned vector_other_loop__span_braced()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s{v};
    std::size_t k = 0ul;
    while (k < 1ul)
        invariant (k <= 1ul)
        decreases (1ul - k)
    {
        w.push_back(0u);
        ++k;
    }
    return s[0];
}

CPP
accepted vector_other_loop__span_size 2 <<'CPP'
verified unsigned vector_other_loop__span_size()
    ensures (result == 2u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    std::size_t k = 0ul;
    while (k < 1ul)
        invariant (k <= 1ul)
        decreases (1ul - k)
    {
        w.push_back(0u);
        ++k;
    }
    return static_cast<unsigned>(s.size());
}

CPP
accepted vector_other_loop__span_empty 0 <<'CPP'
verified unsigned vector_other_loop__span_empty()
    ensures (result == 0u)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    std::size_t k = 0ul;
    while (k < 1ul)
        invariant (k <= 1ul)
        decreases (1ul - k)
    {
        w.push_back(0u);
        ++k;
    }
    return s.empty() ? 1u : 0u;
}

CPP
check

# --- The same for std::string (STDMODEL-015, STDMODEL-025)
begin string_kept
accepted string_kept_element_write__reference 97 <<'CPP'
verified unsigned string_kept_element_write__reference()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    s[1] = 'q';
    return static_cast<unsigned>(c);
}

CPP
accepted string_kept_element_write__const_reference 97 <<'CPP'
verified unsigned string_kept_element_write__const_reference()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    const char& c = s[0];
    s[1] = 'q';
    return static_cast<unsigned>(c);
}

CPP
accepted string_kept_element_write__reference_write 122 <<'CPP'
verified unsigned string_kept_element_write__reference_write()
    ensures (result == 122u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    s[1] = 'q';
    c = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
accepted string_kept_element_write__span 97 <<'CPP'
verified unsigned string_kept_element_write__span()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    s[1] = 'q';
    return static_cast<unsigned>(c[0]);
}

CPP
accepted string_kept_element_write__span_write 122 <<'CPP'
verified unsigned string_kept_element_write__span_write()
    ensures (result == 122u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    s[1] = 'q';
    c[0] = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
accepted string_kept_element_write__const_span 97 <<'CPP'
verified unsigned string_kept_element_write__const_span()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<const char> c(s);
    s[1] = 'q';
    return static_cast<unsigned>(c[0]);
}

CPP
accepted string_kept_copy_out__reference 97 <<'CPP'
verified unsigned string_kept_copy_out__reference()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    std::string m = s;
    return static_cast<unsigned>(c);
}

CPP
accepted string_kept_copy_out__const_reference 97 <<'CPP'
verified unsigned string_kept_copy_out__const_reference()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    const char& c = s[0];
    std::string m = s;
    return static_cast<unsigned>(c);
}

CPP
accepted string_kept_copy_out__reference_write 122 <<'CPP'
verified unsigned string_kept_copy_out__reference_write()
    ensures (result == 122u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    std::string m = s;
    c = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
accepted string_kept_copy_out__span 97 <<'CPP'
verified unsigned string_kept_copy_out__span()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    std::string m = s;
    return static_cast<unsigned>(c[0]);
}

CPP
accepted string_kept_copy_out__span_write 122 <<'CPP'
verified unsigned string_kept_copy_out__span_write()
    ensures (result == 122u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    std::string m = s;
    c[0] = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
accepted string_kept_copy_out__const_span 97 <<'CPP'
verified unsigned string_kept_copy_out__const_span()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<const char> c(s);
    std::string m = s;
    return static_cast<unsigned>(c[0]);
}

CPP
accepted string_kept_copy_assign_out__reference 97 <<'CPP'
verified unsigned string_kept_copy_assign_out__reference()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    t = s;
    return static_cast<unsigned>(c);
}

CPP
accepted string_kept_copy_assign_out__const_reference 97 <<'CPP'
verified unsigned string_kept_copy_assign_out__const_reference()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    const char& c = s[0];
    t = s;
    return static_cast<unsigned>(c);
}

CPP
accepted string_kept_copy_assign_out__reference_write 122 <<'CPP'
verified unsigned string_kept_copy_assign_out__reference_write()
    ensures (result == 122u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    t = s;
    c = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
accepted string_kept_copy_assign_out__span 97 <<'CPP'
verified unsigned string_kept_copy_assign_out__span()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    t = s;
    return static_cast<unsigned>(c[0]);
}

CPP
accepted string_kept_copy_assign_out__span_write 122 <<'CPP'
verified unsigned string_kept_copy_assign_out__span_write()
    ensures (result == 122u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    t = s;
    c[0] = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
accepted string_kept_copy_assign_out__const_span 97 <<'CPP'
verified unsigned string_kept_copy_assign_out__const_span()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<const char> c(s);
    t = s;
    return static_cast<unsigned>(c[0]);
}

CPP
accepted string_kept_append_from__reference 97 <<'CPP'
verified unsigned string_kept_append_from__reference()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    t += s;
    return static_cast<unsigned>(c);
}

CPP
accepted string_kept_append_from__const_reference 97 <<'CPP'
verified unsigned string_kept_append_from__const_reference()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    const char& c = s[0];
    t += s;
    return static_cast<unsigned>(c);
}

CPP
accepted string_kept_append_from__reference_write 122 <<'CPP'
verified unsigned string_kept_append_from__reference_write()
    ensures (result == 122u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    t += s;
    c = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
accepted string_kept_append_from__span 97 <<'CPP'
verified unsigned string_kept_append_from__span()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    t += s;
    return static_cast<unsigned>(c[0]);
}

CPP
accepted string_kept_append_from__span_write 122 <<'CPP'
verified unsigned string_kept_append_from__span_write()
    ensures (result == 122u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    t += s;
    c[0] = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
accepted string_kept_append_from__const_span 97 <<'CPP'
verified unsigned string_kept_append_from__const_span()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<const char> c(s);
    t += s;
    return static_cast<unsigned>(c[0]);
}

CPP
accepted string_kept_append_call_from__reference 97 <<'CPP'
verified unsigned string_kept_append_call_from__reference()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    t.append(s);
    return static_cast<unsigned>(c);
}

CPP
accepted string_kept_append_call_from__const_reference 97 <<'CPP'
verified unsigned string_kept_append_call_from__const_reference()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    const char& c = s[0];
    t.append(s);
    return static_cast<unsigned>(c);
}

CPP
accepted string_kept_append_call_from__reference_write 122 <<'CPP'
verified unsigned string_kept_append_call_from__reference_write()
    ensures (result == 122u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    t.append(s);
    c = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
accepted string_kept_append_call_from__span 97 <<'CPP'
verified unsigned string_kept_append_call_from__span()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    t.append(s);
    return static_cast<unsigned>(c[0]);
}

CPP
accepted string_kept_append_call_from__span_write 122 <<'CPP'
verified unsigned string_kept_append_call_from__span_write()
    ensures (result == 122u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    t.append(s);
    c[0] = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
accepted string_kept_append_call_from__const_span 97 <<'CPP'
verified unsigned string_kept_append_call_from__const_span()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<const char> c(s);
    t.append(s);
    return static_cast<unsigned>(c[0]);
}

CPP
accepted string_other_push_back__reference 97 <<'CPP'
verified unsigned string_other_push_back__reference()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    t.push_back('x');
    return static_cast<unsigned>(c);
}

CPP
accepted string_other_push_back__const_reference 97 <<'CPP'
verified unsigned string_other_push_back__const_reference()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    const char& c = s[0];
    t.push_back('x');
    return static_cast<unsigned>(c);
}

CPP
accepted string_other_push_back__reference_write 122 <<'CPP'
verified unsigned string_other_push_back__reference_write()
    ensures (result == 122u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    t.push_back('x');
    c = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
accepted string_other_push_back__span 97 <<'CPP'
verified unsigned string_other_push_back__span()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    t.push_back('x');
    return static_cast<unsigned>(c[0]);
}

CPP
accepted string_other_push_back__span_write 122 <<'CPP'
verified unsigned string_other_push_back__span_write()
    ensures (result == 122u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    t.push_back('x');
    c[0] = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
accepted string_other_push_back__const_span 97 <<'CPP'
verified unsigned string_other_push_back__const_span()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<const char> c(s);
    t.push_back('x');
    return static_cast<unsigned>(c[0]);
}

CPP
accepted string_other_pop_back__reference 97 <<'CPP'
verified unsigned string_other_pop_back__reference()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    t.pop_back();
    return static_cast<unsigned>(c);
}

CPP
accepted string_other_pop_back__const_reference 97 <<'CPP'
verified unsigned string_other_pop_back__const_reference()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    const char& c = s[0];
    t.pop_back();
    return static_cast<unsigned>(c);
}

CPP
accepted string_other_pop_back__reference_write 122 <<'CPP'
verified unsigned string_other_pop_back__reference_write()
    ensures (result == 122u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    t.pop_back();
    c = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
accepted string_other_pop_back__span 97 <<'CPP'
verified unsigned string_other_pop_back__span()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    t.pop_back();
    return static_cast<unsigned>(c[0]);
}

CPP
accepted string_other_pop_back__span_write 122 <<'CPP'
verified unsigned string_other_pop_back__span_write()
    ensures (result == 122u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    t.pop_back();
    c[0] = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
accepted string_other_pop_back__const_span 97 <<'CPP'
verified unsigned string_other_pop_back__const_span()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<const char> c(s);
    t.pop_back();
    return static_cast<unsigned>(c[0]);
}

CPP
accepted string_other_clear__reference 97 <<'CPP'
verified unsigned string_other_clear__reference()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    t.clear();
    return static_cast<unsigned>(c);
}

CPP
accepted string_other_clear__const_reference 97 <<'CPP'
verified unsigned string_other_clear__const_reference()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    const char& c = s[0];
    t.clear();
    return static_cast<unsigned>(c);
}

CPP
accepted string_other_clear__reference_write 122 <<'CPP'
verified unsigned string_other_clear__reference_write()
    ensures (result == 122u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    t.clear();
    c = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
accepted string_other_clear__span 97 <<'CPP'
verified unsigned string_other_clear__span()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    t.clear();
    return static_cast<unsigned>(c[0]);
}

CPP
accepted string_other_clear__span_write 122 <<'CPP'
verified unsigned string_other_clear__span_write()
    ensures (result == 122u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    t.clear();
    c[0] = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
accepted string_other_clear__const_span 97 <<'CPP'
verified unsigned string_other_clear__const_span()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<const char> c(s);
    t.clear();
    return static_cast<unsigned>(c[0]);
}

CPP
accepted string_other_reserve__reference 97 <<'CPP'
verified unsigned string_other_reserve__reference()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    t.reserve(100ul);
    return static_cast<unsigned>(c);
}

CPP
accepted string_other_reserve__const_reference 97 <<'CPP'
verified unsigned string_other_reserve__const_reference()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    const char& c = s[0];
    t.reserve(100ul);
    return static_cast<unsigned>(c);
}

CPP
accepted string_other_reserve__reference_write 122 <<'CPP'
verified unsigned string_other_reserve__reference_write()
    ensures (result == 122u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    t.reserve(100ul);
    c = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
accepted string_other_reserve__span 97 <<'CPP'
verified unsigned string_other_reserve__span()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    t.reserve(100ul);
    return static_cast<unsigned>(c[0]);
}

CPP
accepted string_other_reserve__span_write 122 <<'CPP'
verified unsigned string_other_reserve__span_write()
    ensures (result == 122u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    t.reserve(100ul);
    c[0] = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
accepted string_other_reserve__const_span 97 <<'CPP'
verified unsigned string_other_reserve__const_span()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<const char> c(s);
    t.reserve(100ul);
    return static_cast<unsigned>(c[0]);
}

CPP
accepted string_other_append_char__reference 97 <<'CPP'
verified unsigned string_other_append_char__reference()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    t += 'x';
    return static_cast<unsigned>(c);
}

CPP
accepted string_other_append_char__const_reference 97 <<'CPP'
verified unsigned string_other_append_char__const_reference()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    const char& c = s[0];
    t += 'x';
    return static_cast<unsigned>(c);
}

CPP
accepted string_other_append_char__reference_write 122 <<'CPP'
verified unsigned string_other_append_char__reference_write()
    ensures (result == 122u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    t += 'x';
    c = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
accepted string_other_append_char__span 97 <<'CPP'
verified unsigned string_other_append_char__span()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    t += 'x';
    return static_cast<unsigned>(c[0]);
}

CPP
accepted string_other_append_char__span_write 122 <<'CPP'
verified unsigned string_other_append_char__span_write()
    ensures (result == 122u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    t += 'x';
    c[0] = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
accepted string_other_append_char__const_span 97 <<'CPP'
verified unsigned string_other_append_char__const_span()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<const char> c(s);
    t += 'x';
    return static_cast<unsigned>(c[0]);
}

CPP
accepted string_other_append_string__reference 97 <<'CPP'
verified unsigned string_other_append_string__reference()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    t += u;
    return static_cast<unsigned>(c);
}

CPP
accepted string_other_append_string__const_reference 97 <<'CPP'
verified unsigned string_other_append_string__const_reference()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    const char& c = s[0];
    t += u;
    return static_cast<unsigned>(c);
}

CPP
accepted string_other_append_string__reference_write 122 <<'CPP'
verified unsigned string_other_append_string__reference_write()
    ensures (result == 122u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    t += u;
    c = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
accepted string_other_append_string__span 97 <<'CPP'
verified unsigned string_other_append_string__span()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    t += u;
    return static_cast<unsigned>(c[0]);
}

CPP
accepted string_other_append_string__span_write 122 <<'CPP'
verified unsigned string_other_append_string__span_write()
    ensures (result == 122u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    t += u;
    c[0] = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
accepted string_other_append_string__const_span 97 <<'CPP'
verified unsigned string_other_append_string__const_span()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<const char> c(s);
    t += u;
    return static_cast<unsigned>(c[0]);
}

CPP
accepted string_other_append__reference 97 <<'CPP'
verified unsigned string_other_append__reference()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    t.append(u);
    return static_cast<unsigned>(c);
}

CPP
accepted string_other_append__const_reference 97 <<'CPP'
verified unsigned string_other_append__const_reference()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    const char& c = s[0];
    t.append(u);
    return static_cast<unsigned>(c);
}

CPP
accepted string_other_append__reference_write 122 <<'CPP'
verified unsigned string_other_append__reference_write()
    ensures (result == 122u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    t.append(u);
    c = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
accepted string_other_append__span 97 <<'CPP'
verified unsigned string_other_append__span()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    t.append(u);
    return static_cast<unsigned>(c[0]);
}

CPP
accepted string_other_append__span_write 122 <<'CPP'
verified unsigned string_other_append__span_write()
    ensures (result == 122u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    t.append(u);
    c[0] = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
accepted string_other_append__const_span 97 <<'CPP'
verified unsigned string_other_append__const_span()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<const char> c(s);
    t.append(u);
    return static_cast<unsigned>(c[0]);
}

CPP
accepted string_other_copy_assign__reference 97 <<'CPP'
verified unsigned string_other_copy_assign__reference()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    t = u;
    return static_cast<unsigned>(c);
}

CPP
accepted string_other_copy_assign__const_reference 97 <<'CPP'
verified unsigned string_other_copy_assign__const_reference()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    const char& c = s[0];
    t = u;
    return static_cast<unsigned>(c);
}

CPP
accepted string_other_copy_assign__reference_write 122 <<'CPP'
verified unsigned string_other_copy_assign__reference_write()
    ensures (result == 122u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    t = u;
    c = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
accepted string_other_copy_assign__span 97 <<'CPP'
verified unsigned string_other_copy_assign__span()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    t = u;
    return static_cast<unsigned>(c[0]);
}

CPP
accepted string_other_copy_assign__span_write 122 <<'CPP'
verified unsigned string_other_copy_assign__span_write()
    ensures (result == 122u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    t = u;
    c[0] = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
accepted string_other_copy_assign__const_span 97 <<'CPP'
verified unsigned string_other_copy_assign__const_span()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<const char> c(s);
    t = u;
    return static_cast<unsigned>(c[0]);
}

CPP
accepted string_other_move_assign__reference 97 <<'CPP'
verified unsigned string_other_move_assign__reference()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    t = std::move(u);
    return static_cast<unsigned>(c);
}

CPP
accepted string_other_move_assign__const_reference 97 <<'CPP'
verified unsigned string_other_move_assign__const_reference()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    const char& c = s[0];
    t = std::move(u);
    return static_cast<unsigned>(c);
}

CPP
accepted string_other_move_assign__reference_write 122 <<'CPP'
verified unsigned string_other_move_assign__reference_write()
    ensures (result == 122u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    t = std::move(u);
    c = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
accepted string_other_move_assign__span 97 <<'CPP'
verified unsigned string_other_move_assign__span()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    t = std::move(u);
    return static_cast<unsigned>(c[0]);
}

CPP
accepted string_other_move_assign__span_write 122 <<'CPP'
verified unsigned string_other_move_assign__span_write()
    ensures (result == 122u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    t = std::move(u);
    c[0] = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
accepted string_other_move_assign__const_span 97 <<'CPP'
verified unsigned string_other_move_assign__const_span()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<const char> c(s);
    t = std::move(u);
    return static_cast<unsigned>(c[0]);
}

CPP
accepted string_other_move_from__reference 97 <<'CPP'
verified unsigned string_other_move_from__reference()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    std::string m = std::move(t);
    return static_cast<unsigned>(c);
}

CPP
accepted string_other_move_from__const_reference 97 <<'CPP'
verified unsigned string_other_move_from__const_reference()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    const char& c = s[0];
    std::string m = std::move(t);
    return static_cast<unsigned>(c);
}

CPP
accepted string_other_move_from__reference_write 122 <<'CPP'
verified unsigned string_other_move_from__reference_write()
    ensures (result == 122u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    std::string m = std::move(t);
    c = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
accepted string_other_move_from__span 97 <<'CPP'
verified unsigned string_other_move_from__span()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    std::string m = std::move(t);
    return static_cast<unsigned>(c[0]);
}

CPP
accepted string_other_move_from__span_write 122 <<'CPP'
verified unsigned string_other_move_from__span_write()
    ensures (result == 122u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    std::string m = std::move(t);
    c[0] = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
accepted string_other_move_from__const_span 97 <<'CPP'
verified unsigned string_other_move_from__const_span()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<const char> c(s);
    std::string m = std::move(t);
    return static_cast<unsigned>(c[0]);
}

CPP
accepted string_other_mutable_call__reference 97 <<'CPP'
verified unsigned string_other_mutable_call__reference()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    touch_string(t);
    return static_cast<unsigned>(c);
}

CPP
accepted string_other_mutable_call__const_reference 97 <<'CPP'
verified unsigned string_other_mutable_call__const_reference()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    const char& c = s[0];
    touch_string(t);
    return static_cast<unsigned>(c);
}

CPP
accepted string_other_mutable_call__reference_write 122 <<'CPP'
verified unsigned string_other_mutable_call__reference_write()
    ensures (result == 122u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    touch_string(t);
    c = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
accepted string_other_mutable_call__span 97 <<'CPP'
verified unsigned string_other_mutable_call__span()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    touch_string(t);
    return static_cast<unsigned>(c[0]);
}

CPP
accepted string_other_mutable_call__span_write 122 <<'CPP'
verified unsigned string_other_mutable_call__span_write()
    ensures (result == 122u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    touch_string(t);
    c[0] = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
accepted string_other_mutable_call__const_span 97 <<'CPP'
verified unsigned string_other_mutable_call__const_span()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<const char> c(s);
    touch_string(t);
    return static_cast<unsigned>(c[0]);
}

CPP
accepted string_other_unsafe_block__reference 97 <<'CPP'
verified unsigned string_other_unsafe_block__reference()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    unsafe {
        grow_string(t);
    }
    return static_cast<unsigned>(c);
}

CPP
accepted string_other_unsafe_block__const_reference 97 <<'CPP'
verified unsigned string_other_unsafe_block__const_reference()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    const char& c = s[0];
    unsafe {
        grow_string(t);
    }
    return static_cast<unsigned>(c);
}

CPP
accepted string_other_unsafe_block__reference_write 122 <<'CPP'
verified unsigned string_other_unsafe_block__reference_write()
    ensures (result == 122u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    unsafe {
        grow_string(t);
    }
    c = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
accepted string_other_unsafe_block__span 97 <<'CPP'
verified unsigned string_other_unsafe_block__span()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    unsafe {
        grow_string(t);
    }
    return static_cast<unsigned>(c[0]);
}

CPP
accepted string_other_unsafe_block__span_write 122 <<'CPP'
verified unsigned string_other_unsafe_block__span_write()
    ensures (result == 122u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    unsafe {
        grow_string(t);
    }
    c[0] = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
accepted string_other_unsafe_block__const_span 97 <<'CPP'
verified unsigned string_other_unsafe_block__const_span()
    ensures (result == 97u)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<const char> c(s);
    unsafe {
        grow_string(t);
    }
    return static_cast<unsigned>(c[0]);
}

CPP
check

# --- The accepted placements (STDMODEL-015, STDMODEL-025)
begin placement
accepted placement_event_path_returns <<'CPP'
verified unsigned placement_event_path_returns(bool b)
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    unsigned& r = v[0];
    if (b) {
        v.push_back(1u);
        return 0u;
    }
    return r;
}

CPP
accepted placement_view_per_iteration <<'CPP'
verified unsigned placement_view_per_iteration()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    unsigned seen = 0u;
    std::size_t k = 0ul;
    while (k < 2ul)
        invariant (k <= 2ul && 0ul < v.size())
        decreases (2ul - k)
    {
        unsigned& r = v[0];
        seen = r;
        v.push_back(1u);
        ++k;
    }
    return seen;
}

CPP
accepted placement_unsafe_other_vector <<'CPP'
verified unsigned placement_unsafe_other_vector()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    unsigned& r = v[0];
    unsafe {
        grow(w);
    }
    return r;
}

CPP
accepted placement_mutable_call_other_vector <<'CPP'
verified unsigned placement_mutable_call_other_vector()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    unsigned& r = v[0];
    touch(w);
    return r;
}

CPP
accepted placement_copy_then_mutate_copy <<'CPP'
verified unsigned placement_copy_then_mutate_copy()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    unsigned& r = v[0];
    std::vector<unsigned> z = v;
    z.push_back(1u);
    return r;
}

CPP
accepted placement_span_after_fill_call 2 <<'CPP'
verified unsigned placement_span_after_fill_call()
    ensures (result == 2u)
{
    std::vector<unsigned> v{1u, 2u};
    fill(v);
    return static_cast<unsigned>(v.size());
}

CPP
accepted placement_element_after_look_call 2 <<'CPP'
verified unsigned placement_element_after_look_call()
    ensures (result == 2u)
{
    std::vector<unsigned> v{1u, 2u};
    look(v);
    return v[1];
}

CPP
check

echo 'every view and element reference whose storage is unchanged stays usable, and reads what it states'
