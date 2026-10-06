#!/usr/bin/env bash
# SPEC: STDMODEL-015, STDMODEL-024, STDMODEL-025, STDMODEL-026, STDMODEL-023, UNSAFE-003
# TRUST.md TCB-LIB-006, TCB-LIB-007
#
# Storage generations of the verified sequence subset (RFC 0020), exhaustively:
# every event that may replace the storage of a vector or string, against every
# kind of view or element reference formed over it before the event, read or
# written after it; and the event placed in a branch, after a return, in a loop
# or behind another name of the container. Each case is refused, naming the
# view, its container and the event. The accepted twins, which change only
# what the event touches, are in `e2e/sequence_generations.sh`.
set -euo pipefail
CPPL="$1"
WORK="$3"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/sequence-generations.XXXXXX")
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
verified void keep_copy(std::vector<unsigned> x) ensures (true) { x.push_back(1u); }
verified void look(std::span<const unsigned> s) expects (readable(s)) ensures (true) { }
verified void fill(std::span<unsigned> s) expects (writable(s)) ensures (true) { if (0ul < s.size()) { s[0] = 0u; } }
CPP
}

# --- Every event that may replace a vector's storage, against every view and element reference formed before it (STDMODEL-015, STDMODEL-025)
begin vector_generations
refused vector_push_back__reference "'r' refers to an element of 'v', which may have been reallocated or ended by 'std::vector::push_back'" <<'CPP'
verified unsigned vector_push_back__reference()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    v.push_back(3u);
    return r;
}

CPP
refused vector_push_back__const_reference "'r' refers to an element of 'v', which may have been reallocated or ended by 'std::vector::push_back'" <<'CPP'
verified unsigned vector_push_back__const_reference()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    const unsigned& r = v[0];
    v.push_back(3u);
    return r;
}

CPP
refused vector_push_back__reference_write "'r' refers to an element of 'v', which may have been reallocated or ended by 'std::vector::push_back'" <<'CPP'
verified unsigned vector_push_back__reference_write()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    v.push_back(3u);
    r = 5u;
    return v[0];
}

CPP
refused vector_push_back__span "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::push_back'" <<'CPP'
verified unsigned vector_push_back__span()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    v.push_back(3u);
    return s[0];
}

CPP
refused vector_push_back__span_write "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::push_back'" <<'CPP'
verified unsigned vector_push_back__span_write()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    v.push_back(3u);
    s[0] = 5u;
    return v[0];
}

CPP
refused vector_push_back__const_span "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::push_back'" <<'CPP'
verified unsigned vector_push_back__const_span()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<const unsigned> s(v);
    v.push_back(3u);
    return s[0];
}

CPP
refused vector_push_back__span_copy_initialized "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::push_back'" <<'CPP'
verified unsigned vector_push_back__span_copy_initialized()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s = v;
    v.push_back(3u);
    return s[0];
}

CPP
refused vector_push_back__span_braced "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::push_back'" <<'CPP'
verified unsigned vector_push_back__span_braced()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s{v};
    v.push_back(3u);
    return s[0];
}

CPP
refused vector_push_back__span_size "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::push_back'" <<'CPP'
verified unsigned vector_push_back__span_size()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    v.push_back(3u);
    return static_cast<unsigned>(s.size());
}

CPP
refused vector_push_back__span_empty "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::push_back'" <<'CPP'
verified unsigned vector_push_back__span_empty()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    v.push_back(3u);
    return s.empty() ? 1u : 0u;
}

CPP
refused vector_pop_back__reference "'r' refers to an element of 'v', which may have been reallocated or ended by 'std::vector::pop_back'" <<'CPP'
verified unsigned vector_pop_back__reference()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    v.pop_back();
    return r;
}

CPP
refused vector_pop_back__const_reference "'r' refers to an element of 'v', which may have been reallocated or ended by 'std::vector::pop_back'" <<'CPP'
verified unsigned vector_pop_back__const_reference()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    const unsigned& r = v[0];
    v.pop_back();
    return r;
}

CPP
refused vector_pop_back__reference_write "'r' refers to an element of 'v', which may have been reallocated or ended by 'std::vector::pop_back'" <<'CPP'
verified unsigned vector_pop_back__reference_write()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    v.pop_back();
    r = 5u;
    return v[0];
}

CPP
refused vector_pop_back__span "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::pop_back'" <<'CPP'
verified unsigned vector_pop_back__span()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    v.pop_back();
    return s[0];
}

CPP
refused vector_pop_back__span_write "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::pop_back'" <<'CPP'
verified unsigned vector_pop_back__span_write()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    v.pop_back();
    s[0] = 5u;
    return v[0];
}

CPP
refused vector_pop_back__const_span "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::pop_back'" <<'CPP'
verified unsigned vector_pop_back__const_span()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<const unsigned> s(v);
    v.pop_back();
    return s[0];
}

CPP
refused vector_pop_back__span_copy_initialized "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::pop_back'" <<'CPP'
verified unsigned vector_pop_back__span_copy_initialized()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s = v;
    v.pop_back();
    return s[0];
}

CPP
refused vector_pop_back__span_braced "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::pop_back'" <<'CPP'
verified unsigned vector_pop_back__span_braced()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s{v};
    v.pop_back();
    return s[0];
}

CPP
refused vector_pop_back__span_size "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::pop_back'" <<'CPP'
verified unsigned vector_pop_back__span_size()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    v.pop_back();
    return static_cast<unsigned>(s.size());
}

CPP
refused vector_pop_back__span_empty "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::pop_back'" <<'CPP'
verified unsigned vector_pop_back__span_empty()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    v.pop_back();
    return s.empty() ? 1u : 0u;
}

CPP
refused vector_clear__reference "'r' refers to an element of 'v', which may have been reallocated or ended by 'std::vector::clear'" <<'CPP'
verified unsigned vector_clear__reference()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    v.clear();
    return r;
}

CPP
refused vector_clear__const_reference "'r' refers to an element of 'v', which may have been reallocated or ended by 'std::vector::clear'" <<'CPP'
verified unsigned vector_clear__const_reference()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    const unsigned& r = v[0];
    v.clear();
    return r;
}

CPP
refused vector_clear__reference_write "'r' refers to an element of 'v', which may have been reallocated or ended by 'std::vector::clear'" <<'CPP'
verified unsigned vector_clear__reference_write()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    v.clear();
    r = 5u;
    return v[0];
}

CPP
refused vector_clear__span "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::clear'" <<'CPP'
verified unsigned vector_clear__span()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    v.clear();
    return s[0];
}

CPP
refused vector_clear__span_write "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::clear'" <<'CPP'
verified unsigned vector_clear__span_write()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    v.clear();
    s[0] = 5u;
    return v[0];
}

CPP
refused vector_clear__const_span "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::clear'" <<'CPP'
verified unsigned vector_clear__const_span()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<const unsigned> s(v);
    v.clear();
    return s[0];
}

CPP
refused vector_clear__span_copy_initialized "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::clear'" <<'CPP'
verified unsigned vector_clear__span_copy_initialized()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s = v;
    v.clear();
    return s[0];
}

CPP
refused vector_clear__span_braced "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::clear'" <<'CPP'
verified unsigned vector_clear__span_braced()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s{v};
    v.clear();
    return s[0];
}

CPP
refused vector_clear__span_size "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::clear'" <<'CPP'
verified unsigned vector_clear__span_size()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    v.clear();
    return static_cast<unsigned>(s.size());
}

CPP
refused vector_clear__span_empty "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::clear'" <<'CPP'
verified unsigned vector_clear__span_empty()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    v.clear();
    return s.empty() ? 1u : 0u;
}

CPP
refused vector_reserve__reference "'r' refers to an element of 'v', which may have been reallocated or ended by 'std::vector::reserve'" <<'CPP'
verified unsigned vector_reserve__reference()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    v.reserve(100ul);
    return r;
}

CPP
refused vector_reserve__const_reference "'r' refers to an element of 'v', which may have been reallocated or ended by 'std::vector::reserve'" <<'CPP'
verified unsigned vector_reserve__const_reference()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    const unsigned& r = v[0];
    v.reserve(100ul);
    return r;
}

CPP
refused vector_reserve__reference_write "'r' refers to an element of 'v', which may have been reallocated or ended by 'std::vector::reserve'" <<'CPP'
verified unsigned vector_reserve__reference_write()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    v.reserve(100ul);
    r = 5u;
    return v[0];
}

CPP
refused vector_reserve__span "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::reserve'" <<'CPP'
verified unsigned vector_reserve__span()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    v.reserve(100ul);
    return s[0];
}

CPP
refused vector_reserve__span_write "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::reserve'" <<'CPP'
verified unsigned vector_reserve__span_write()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    v.reserve(100ul);
    s[0] = 5u;
    return v[0];
}

CPP
refused vector_reserve__const_span "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::reserve'" <<'CPP'
verified unsigned vector_reserve__const_span()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<const unsigned> s(v);
    v.reserve(100ul);
    return s[0];
}

CPP
refused vector_reserve__span_copy_initialized "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::reserve'" <<'CPP'
verified unsigned vector_reserve__span_copy_initialized()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s = v;
    v.reserve(100ul);
    return s[0];
}

CPP
refused vector_reserve__span_braced "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::reserve'" <<'CPP'
verified unsigned vector_reserve__span_braced()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s{v};
    v.reserve(100ul);
    return s[0];
}

CPP
refused vector_reserve__span_size "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::reserve'" <<'CPP'
verified unsigned vector_reserve__span_size()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    v.reserve(100ul);
    return static_cast<unsigned>(s.size());
}

CPP
refused vector_reserve__span_empty "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::reserve'" <<'CPP'
verified unsigned vector_reserve__span_empty()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    v.reserve(100ul);
    return s.empty() ? 1u : 0u;
}

CPP
refused vector_copy_assign__reference "'r' refers to an element of 'v', which may have been reallocated or ended by 'std::vector::operator='" <<'CPP'
verified unsigned vector_copy_assign__reference()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    v = z;
    return r;
}

CPP
refused vector_copy_assign__const_reference "'r' refers to an element of 'v', which may have been reallocated or ended by 'std::vector::operator='" <<'CPP'
verified unsigned vector_copy_assign__const_reference()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    const unsigned& r = v[0];
    v = z;
    return r;
}

CPP
refused vector_copy_assign__reference_write "'r' refers to an element of 'v', which may have been reallocated or ended by 'std::vector::operator='" <<'CPP'
verified unsigned vector_copy_assign__reference_write()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    v = z;
    r = 5u;
    return v[0];
}

CPP
refused vector_copy_assign__span "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::operator='" <<'CPP'
verified unsigned vector_copy_assign__span()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    v = z;
    return s[0];
}

CPP
refused vector_copy_assign__span_write "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::operator='" <<'CPP'
verified unsigned vector_copy_assign__span_write()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    v = z;
    s[0] = 5u;
    return v[0];
}

CPP
refused vector_copy_assign__const_span "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::operator='" <<'CPP'
verified unsigned vector_copy_assign__const_span()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<const unsigned> s(v);
    v = z;
    return s[0];
}

CPP
refused vector_copy_assign__span_copy_initialized "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::operator='" <<'CPP'
verified unsigned vector_copy_assign__span_copy_initialized()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s = v;
    v = z;
    return s[0];
}

CPP
refused vector_copy_assign__span_braced "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::operator='" <<'CPP'
verified unsigned vector_copy_assign__span_braced()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s{v};
    v = z;
    return s[0];
}

CPP
refused vector_copy_assign__span_size "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::operator='" <<'CPP'
verified unsigned vector_copy_assign__span_size()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    v = z;
    return static_cast<unsigned>(s.size());
}

CPP
refused vector_copy_assign__span_empty "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::operator='" <<'CPP'
verified unsigned vector_copy_assign__span_empty()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    v = z;
    return s.empty() ? 1u : 0u;
}

CPP
refused vector_move_assign__reference "'r' refers to an element of 'v', which may have been reallocated or ended by 'std::vector::operator='" <<'CPP'
verified unsigned vector_move_assign__reference()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    v = std::move(z);
    return r;
}

CPP
refused vector_move_assign__const_reference "'r' refers to an element of 'v', which may have been reallocated or ended by 'std::vector::operator='" <<'CPP'
verified unsigned vector_move_assign__const_reference()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    const unsigned& r = v[0];
    v = std::move(z);
    return r;
}

CPP
refused vector_move_assign__reference_write "'r' refers to an element of 'v', which may have been reallocated or ended by 'std::vector::operator='" <<'CPP'
verified unsigned vector_move_assign__reference_write()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    v = std::move(z);
    r = 5u;
    return v[0];
}

CPP
refused vector_move_assign__span "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::operator='" <<'CPP'
verified unsigned vector_move_assign__span()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    v = std::move(z);
    return s[0];
}

CPP
refused vector_move_assign__span_write "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::operator='" <<'CPP'
verified unsigned vector_move_assign__span_write()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    v = std::move(z);
    s[0] = 5u;
    return v[0];
}

CPP
refused vector_move_assign__const_span "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::operator='" <<'CPP'
verified unsigned vector_move_assign__const_span()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<const unsigned> s(v);
    v = std::move(z);
    return s[0];
}

CPP
refused vector_move_assign__span_copy_initialized "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::operator='" <<'CPP'
verified unsigned vector_move_assign__span_copy_initialized()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s = v;
    v = std::move(z);
    return s[0];
}

CPP
refused vector_move_assign__span_braced "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::operator='" <<'CPP'
verified unsigned vector_move_assign__span_braced()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s{v};
    v = std::move(z);
    return s[0];
}

CPP
refused vector_move_assign__span_size "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::operator='" <<'CPP'
verified unsigned vector_move_assign__span_size()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    v = std::move(z);
    return static_cast<unsigned>(s.size());
}

CPP
refused vector_move_assign__span_empty "'s' views the storage of 'v', which may have been reallocated or ended by 'std::vector::operator='" <<'CPP'
verified unsigned vector_move_assign__span_empty()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    v = std::move(z);
    return s.empty() ? 1u : 0u;
}

CPP
refused vector_move_from__reference "'r' refers to an element of 'v', which may have been reallocated or ended by being moved from" <<'CPP'
verified unsigned vector_move_from__reference()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    std::vector<unsigned> m = std::move(v);
    return r;
}

CPP
refused vector_move_from__const_reference "'r' refers to an element of 'v', which may have been reallocated or ended by being moved from" <<'CPP'
verified unsigned vector_move_from__const_reference()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    const unsigned& r = v[0];
    std::vector<unsigned> m = std::move(v);
    return r;
}

CPP
refused vector_move_from__reference_write "'r' refers to an element of 'v', which may have been reallocated or ended by being moved from" <<'CPP'
verified unsigned vector_move_from__reference_write()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    std::vector<unsigned> m = std::move(v);
    r = 5u;
    return v[0];
}

CPP
refused vector_move_from__span "'s' views the storage of 'v', which may have been reallocated or ended by being moved from" <<'CPP'
verified unsigned vector_move_from__span()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    std::vector<unsigned> m = std::move(v);
    return s[0];
}

CPP
refused vector_move_from__span_write "'s' views the storage of 'v', which may have been reallocated or ended by being moved from" <<'CPP'
verified unsigned vector_move_from__span_write()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    std::vector<unsigned> m = std::move(v);
    s[0] = 5u;
    return v[0];
}

CPP
refused vector_move_from__const_span "'s' views the storage of 'v', which may have been reallocated or ended by being moved from" <<'CPP'
verified unsigned vector_move_from__const_span()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<const unsigned> s(v);
    std::vector<unsigned> m = std::move(v);
    return s[0];
}

CPP
refused vector_move_from__span_copy_initialized "'s' views the storage of 'v', which may have been reallocated or ended by being moved from" <<'CPP'
verified unsigned vector_move_from__span_copy_initialized()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s = v;
    std::vector<unsigned> m = std::move(v);
    return s[0];
}

CPP
refused vector_move_from__span_braced "'s' views the storage of 'v', which may have been reallocated or ended by being moved from" <<'CPP'
verified unsigned vector_move_from__span_braced()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s{v};
    std::vector<unsigned> m = std::move(v);
    return s[0];
}

CPP
refused vector_move_from__span_size "'s' views the storage of 'v', which may have been reallocated or ended by being moved from" <<'CPP'
verified unsigned vector_move_from__span_size()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    std::vector<unsigned> m = std::move(v);
    return static_cast<unsigned>(s.size());
}

CPP
refused vector_move_from__span_empty "'s' views the storage of 'v', which may have been reallocated or ended by being moved from" <<'CPP'
verified unsigned vector_move_from__span_empty()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    std::vector<unsigned> m = std::move(v);
    return s.empty() ? 1u : 0u;
}

CPP
refused vector_mutable_call__reference "'r' refers to an element of 'v', which may have been reallocated or ended by passing it by mutable reference to 'touch'" <<'CPP'
verified unsigned vector_mutable_call__reference()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    touch(v);
    return r;
}

CPP
refused vector_mutable_call__const_reference "'r' refers to an element of 'v', which may have been reallocated or ended by passing it by mutable reference to 'touch'" <<'CPP'
verified unsigned vector_mutable_call__const_reference()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    const unsigned& r = v[0];
    touch(v);
    return r;
}

CPP
refused vector_mutable_call__reference_write "'r' refers to an element of 'v', which may have been reallocated or ended by passing it by mutable reference to 'touch'" <<'CPP'
verified unsigned vector_mutable_call__reference_write()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    touch(v);
    r = 5u;
    return v[0];
}

CPP
refused vector_mutable_call__span "'s' views the storage of 'v', which may have been reallocated or ended by passing it by mutable reference to 'touch'" <<'CPP'
verified unsigned vector_mutable_call__span()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    touch(v);
    return s[0];
}

CPP
refused vector_mutable_call__span_write "'s' views the storage of 'v', which may have been reallocated or ended by passing it by mutable reference to 'touch'" <<'CPP'
verified unsigned vector_mutable_call__span_write()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    touch(v);
    s[0] = 5u;
    return v[0];
}

CPP
refused vector_mutable_call__const_span "'s' views the storage of 'v', which may have been reallocated or ended by passing it by mutable reference to 'touch'" <<'CPP'
verified unsigned vector_mutable_call__const_span()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<const unsigned> s(v);
    touch(v);
    return s[0];
}

CPP
refused vector_mutable_call__span_copy_initialized "'s' views the storage of 'v', which may have been reallocated or ended by passing it by mutable reference to 'touch'" <<'CPP'
verified unsigned vector_mutable_call__span_copy_initialized()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s = v;
    touch(v);
    return s[0];
}

CPP
refused vector_mutable_call__span_braced "'s' views the storage of 'v', which may have been reallocated or ended by passing it by mutable reference to 'touch'" <<'CPP'
verified unsigned vector_mutable_call__span_braced()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s{v};
    touch(v);
    return s[0];
}

CPP
refused vector_mutable_call__span_size "'s' views the storage of 'v', which may have been reallocated or ended by passing it by mutable reference to 'touch'" <<'CPP'
verified unsigned vector_mutable_call__span_size()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    touch(v);
    return static_cast<unsigned>(s.size());
}

CPP
refused vector_mutable_call__span_empty "'s' views the storage of 'v', which may have been reallocated or ended by passing it by mutable reference to 'touch'" <<'CPP'
verified unsigned vector_mutable_call__span_empty()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    touch(v);
    return s.empty() ? 1u : 0u;
}

CPP
refused vector_unsafe_block__reference "'r' refers to an element of 'v', which may have been reallocated or ended by the unsafe block" <<'CPP'
verified unsigned vector_unsafe_block__reference()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    unsafe {
        grow(v);
    }
    return r;
}

CPP
refused vector_unsafe_block__const_reference "'r' refers to an element of 'v', which may have been reallocated or ended by the unsafe block" <<'CPP'
verified unsigned vector_unsafe_block__const_reference()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    const unsigned& r = v[0];
    unsafe {
        grow(v);
    }
    return r;
}

CPP
refused vector_unsafe_block__reference_write "'r' refers to an element of 'v', which may have been reallocated or ended by the unsafe block" <<'CPP'
verified unsigned vector_unsafe_block__reference_write()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    unsigned& r = v[0];
    unsafe {
        grow(v);
    }
    r = 5u;
    return v[0];
}

CPP
refused vector_unsafe_block__span "'s' views the storage of 'v', which may have been reallocated or ended by the unsafe block" <<'CPP'
verified unsigned vector_unsafe_block__span()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    unsafe {
        grow(v);
    }
    return s[0];
}

CPP
refused vector_unsafe_block__span_write "'s' views the storage of 'v', which may have been reallocated or ended by the unsafe block" <<'CPP'
verified unsigned vector_unsafe_block__span_write()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    unsafe {
        grow(v);
    }
    s[0] = 5u;
    return v[0];
}

CPP
refused vector_unsafe_block__const_span "'s' views the storage of 'v', which may have been reallocated or ended by the unsafe block" <<'CPP'
verified unsigned vector_unsafe_block__const_span()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<const unsigned> s(v);
    unsafe {
        grow(v);
    }
    return s[0];
}

CPP
refused vector_unsafe_block__span_copy_initialized "'s' views the storage of 'v', which may have been reallocated or ended by the unsafe block" <<'CPP'
verified unsigned vector_unsafe_block__span_copy_initialized()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s = v;
    unsafe {
        grow(v);
    }
    return s[0];
}

CPP
refused vector_unsafe_block__span_braced "'s' views the storage of 'v', which may have been reallocated or ended by the unsafe block" <<'CPP'
verified unsigned vector_unsafe_block__span_braced()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s{v};
    unsafe {
        grow(v);
    }
    return s[0];
}

CPP
refused vector_unsafe_block__span_size "'s' views the storage of 'v', which may have been reallocated or ended by the unsafe block" <<'CPP'
verified unsigned vector_unsafe_block__span_size()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    unsafe {
        grow(v);
    }
    return static_cast<unsigned>(s.size());
}

CPP
refused vector_unsafe_block__span_empty "'s' views the storage of 'v', which may have been reallocated or ended by the unsafe block" <<'CPP'
verified unsigned vector_unsafe_block__span_empty()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::vector<unsigned> z{7u};
    std::span<unsigned> s(v);
    unsafe {
        grow(v);
    }
    return s.empty() ? 1u : 0u;
}

CPP
refused vector_loop__reference "'r' refers to an element of 'v', which may have been reallocated or ended by the loop" <<'CPP'
verified unsigned vector_loop__reference()
    ensures (result == result)
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
        v.push_back(0u);
        ++k;
    }
    return r;
}

CPP
refused vector_loop__const_reference "'r' refers to an element of 'v', which may have been reallocated or ended by the loop" <<'CPP'
verified unsigned vector_loop__const_reference()
    ensures (result == result)
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
        v.push_back(0u);
        ++k;
    }
    return r;
}

CPP
refused vector_loop__reference_write "'r' refers to an element of 'v', which may have been reallocated or ended by the loop" <<'CPP'
verified unsigned vector_loop__reference_write()
    ensures (result == result)
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
        v.push_back(0u);
        ++k;
    }
    r = 5u;
    return v[0];
}

CPP
refused vector_loop__span "'s' views the storage of 'v', which may have been reallocated or ended by the loop" <<'CPP'
verified unsigned vector_loop__span()
    ensures (result == result)
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
        v.push_back(0u);
        ++k;
    }
    return s[0];
}

CPP
refused vector_loop__span_write "'s' views the storage of 'v', which may have been reallocated or ended by the loop" <<'CPP'
verified unsigned vector_loop__span_write()
    ensures (result == result)
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
        v.push_back(0u);
        ++k;
    }
    s[0] = 5u;
    return v[0];
}

CPP
refused vector_loop__const_span "'s' views the storage of 'v', which may have been reallocated or ended by the loop" <<'CPP'
verified unsigned vector_loop__const_span()
    ensures (result == result)
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
        v.push_back(0u);
        ++k;
    }
    return s[0];
}

CPP
refused vector_loop__span_copy_initialized "'s' views the storage of 'v', which may have been reallocated or ended by the loop" <<'CPP'
verified unsigned vector_loop__span_copy_initialized()
    ensures (result == result)
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
        v.push_back(0u);
        ++k;
    }
    return s[0];
}

CPP
refused vector_loop__span_braced "'s' views the storage of 'v', which may have been reallocated or ended by the loop" <<'CPP'
verified unsigned vector_loop__span_braced()
    ensures (result == result)
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
        v.push_back(0u);
        ++k;
    }
    return s[0];
}

CPP
refused vector_loop__span_size "'s' views the storage of 'v', which may have been reallocated or ended by the loop" <<'CPP'
verified unsigned vector_loop__span_size()
    ensures (result == result)
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
        v.push_back(0u);
        ++k;
    }
    return static_cast<unsigned>(s.size());
}

CPP
refused vector_loop__span_empty "'s' views the storage of 'v', which may have been reallocated or ended by the loop" <<'CPP'
verified unsigned vector_loop__span_empty()
    ensures (result == result)
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
        v.push_back(0u);
        ++k;
    }
    return s.empty() ? 1u : 0u;
}

CPP
check

# --- The same for std::string (STDMODEL-015, STDMODEL-025)
begin string_generations
refused string_push_back__reference "'c' refers to an element of 's', which may have been reallocated or ended by 'std::basic_string<char>::push_back'" <<'CPP'
verified unsigned string_push_back__reference()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    s.push_back('x');
    return static_cast<unsigned>(c);
}

CPP
refused string_push_back__const_reference "'c' refers to an element of 's', which may have been reallocated or ended by 'std::basic_string<char>::push_back'" <<'CPP'
verified unsigned string_push_back__const_reference()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    const char& c = s[0];
    s.push_back('x');
    return static_cast<unsigned>(c);
}

CPP
refused string_push_back__reference_write "'c' refers to an element of 's', which may have been reallocated or ended by 'std::basic_string<char>::push_back'" <<'CPP'
verified unsigned string_push_back__reference_write()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    s.push_back('x');
    c = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
refused string_push_back__span "'c' views the storage of 's', which may have been reallocated or ended by 'std::basic_string<char>::push_back'" <<'CPP'
verified unsigned string_push_back__span()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    s.push_back('x');
    return static_cast<unsigned>(c[0]);
}

CPP
refused string_push_back__span_write "'c' views the storage of 's', which may have been reallocated or ended by 'std::basic_string<char>::push_back'" <<'CPP'
verified unsigned string_push_back__span_write()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    s.push_back('x');
    c[0] = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
refused string_push_back__const_span "'c' views the storage of 's', which may have been reallocated or ended by 'std::basic_string<char>::push_back'" <<'CPP'
verified unsigned string_push_back__const_span()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<const char> c(s);
    s.push_back('x');
    return static_cast<unsigned>(c[0]);
}

CPP
refused string_pop_back__reference "'c' refers to an element of 's', which may have been reallocated or ended by 'std::basic_string<char>::pop_back'" <<'CPP'
verified unsigned string_pop_back__reference()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    s.pop_back();
    return static_cast<unsigned>(c);
}

CPP
refused string_pop_back__const_reference "'c' refers to an element of 's', which may have been reallocated or ended by 'std::basic_string<char>::pop_back'" <<'CPP'
verified unsigned string_pop_back__const_reference()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    const char& c = s[0];
    s.pop_back();
    return static_cast<unsigned>(c);
}

CPP
refused string_pop_back__reference_write "'c' refers to an element of 's', which may have been reallocated or ended by 'std::basic_string<char>::pop_back'" <<'CPP'
verified unsigned string_pop_back__reference_write()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    s.pop_back();
    c = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
refused string_pop_back__span "'c' views the storage of 's', which may have been reallocated or ended by 'std::basic_string<char>::pop_back'" <<'CPP'
verified unsigned string_pop_back__span()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    s.pop_back();
    return static_cast<unsigned>(c[0]);
}

CPP
refused string_pop_back__span_write "'c' views the storage of 's', which may have been reallocated or ended by 'std::basic_string<char>::pop_back'" <<'CPP'
verified unsigned string_pop_back__span_write()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    s.pop_back();
    c[0] = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
refused string_pop_back__const_span "'c' views the storage of 's', which may have been reallocated or ended by 'std::basic_string<char>::pop_back'" <<'CPP'
verified unsigned string_pop_back__const_span()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<const char> c(s);
    s.pop_back();
    return static_cast<unsigned>(c[0]);
}

CPP
refused string_clear__reference "'c' refers to an element of 's', which may have been reallocated or ended by 'std::basic_string<char>::clear'" <<'CPP'
verified unsigned string_clear__reference()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    s.clear();
    return static_cast<unsigned>(c);
}

CPP
refused string_clear__const_reference "'c' refers to an element of 's', which may have been reallocated or ended by 'std::basic_string<char>::clear'" <<'CPP'
verified unsigned string_clear__const_reference()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    const char& c = s[0];
    s.clear();
    return static_cast<unsigned>(c);
}

CPP
refused string_clear__reference_write "'c' refers to an element of 's', which may have been reallocated or ended by 'std::basic_string<char>::clear'" <<'CPP'
verified unsigned string_clear__reference_write()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    s.clear();
    c = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
refused string_clear__span "'c' views the storage of 's', which may have been reallocated or ended by 'std::basic_string<char>::clear'" <<'CPP'
verified unsigned string_clear__span()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    s.clear();
    return static_cast<unsigned>(c[0]);
}

CPP
refused string_clear__span_write "'c' views the storage of 's', which may have been reallocated or ended by 'std::basic_string<char>::clear'" <<'CPP'
verified unsigned string_clear__span_write()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    s.clear();
    c[0] = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
refused string_clear__const_span "'c' views the storage of 's', which may have been reallocated or ended by 'std::basic_string<char>::clear'" <<'CPP'
verified unsigned string_clear__const_span()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<const char> c(s);
    s.clear();
    return static_cast<unsigned>(c[0]);
}

CPP
refused string_reserve__reference "'c' refers to an element of 's', which may have been reallocated or ended by 'std::basic_string<char>::reserve'" <<'CPP'
verified unsigned string_reserve__reference()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    s.reserve(100ul);
    return static_cast<unsigned>(c);
}

CPP
refused string_reserve__const_reference "'c' refers to an element of 's', which may have been reallocated or ended by 'std::basic_string<char>::reserve'" <<'CPP'
verified unsigned string_reserve__const_reference()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    const char& c = s[0];
    s.reserve(100ul);
    return static_cast<unsigned>(c);
}

CPP
refused string_reserve__reference_write "'c' refers to an element of 's', which may have been reallocated or ended by 'std::basic_string<char>::reserve'" <<'CPP'
verified unsigned string_reserve__reference_write()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    s.reserve(100ul);
    c = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
refused string_reserve__span "'c' views the storage of 's', which may have been reallocated or ended by 'std::basic_string<char>::reserve'" <<'CPP'
verified unsigned string_reserve__span()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    s.reserve(100ul);
    return static_cast<unsigned>(c[0]);
}

CPP
refused string_reserve__span_write "'c' views the storage of 's', which may have been reallocated or ended by 'std::basic_string<char>::reserve'" <<'CPP'
verified unsigned string_reserve__span_write()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    s.reserve(100ul);
    c[0] = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
refused string_reserve__const_span "'c' views the storage of 's', which may have been reallocated or ended by 'std::basic_string<char>::reserve'" <<'CPP'
verified unsigned string_reserve__const_span()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<const char> c(s);
    s.reserve(100ul);
    return static_cast<unsigned>(c[0]);
}

CPP
refused string_append_char__reference "'c' refers to an element of 's', which may have been reallocated or ended by 'std::basic_string<char>::operator+='" <<'CPP'
verified unsigned string_append_char__reference()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    s += 'x';
    return static_cast<unsigned>(c);
}

CPP
refused string_append_char__const_reference "'c' refers to an element of 's', which may have been reallocated or ended by 'std::basic_string<char>::operator+='" <<'CPP'
verified unsigned string_append_char__const_reference()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    const char& c = s[0];
    s += 'x';
    return static_cast<unsigned>(c);
}

CPP
refused string_append_char__reference_write "'c' refers to an element of 's', which may have been reallocated or ended by 'std::basic_string<char>::operator+='" <<'CPP'
verified unsigned string_append_char__reference_write()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    s += 'x';
    c = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
refused string_append_char__span "'c' views the storage of 's', which may have been reallocated or ended by 'std::basic_string<char>::operator+='" <<'CPP'
verified unsigned string_append_char__span()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    s += 'x';
    return static_cast<unsigned>(c[0]);
}

CPP
refused string_append_char__span_write "'c' views the storage of 's', which may have been reallocated or ended by 'std::basic_string<char>::operator+='" <<'CPP'
verified unsigned string_append_char__span_write()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    s += 'x';
    c[0] = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
refused string_append_char__const_span "'c' views the storage of 's', which may have been reallocated or ended by 'std::basic_string<char>::operator+='" <<'CPP'
verified unsigned string_append_char__const_span()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<const char> c(s);
    s += 'x';
    return static_cast<unsigned>(c[0]);
}

CPP
refused string_append_string__reference "'c' refers to an element of 's', which may have been reallocated or ended by 'std::basic_string<char>::operator+='" <<'CPP'
verified unsigned string_append_string__reference()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    s += u;
    return static_cast<unsigned>(c);
}

CPP
refused string_append_string__const_reference "'c' refers to an element of 's', which may have been reallocated or ended by 'std::basic_string<char>::operator+='" <<'CPP'
verified unsigned string_append_string__const_reference()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    const char& c = s[0];
    s += u;
    return static_cast<unsigned>(c);
}

CPP
refused string_append_string__reference_write "'c' refers to an element of 's', which may have been reallocated or ended by 'std::basic_string<char>::operator+='" <<'CPP'
verified unsigned string_append_string__reference_write()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    s += u;
    c = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
refused string_append_string__span "'c' views the storage of 's', which may have been reallocated or ended by 'std::basic_string<char>::operator+='" <<'CPP'
verified unsigned string_append_string__span()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    s += u;
    return static_cast<unsigned>(c[0]);
}

CPP
refused string_append_string__span_write "'c' views the storage of 's', which may have been reallocated or ended by 'std::basic_string<char>::operator+='" <<'CPP'
verified unsigned string_append_string__span_write()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    s += u;
    c[0] = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
refused string_append_string__const_span "'c' views the storage of 's', which may have been reallocated or ended by 'std::basic_string<char>::operator+='" <<'CPP'
verified unsigned string_append_string__const_span()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<const char> c(s);
    s += u;
    return static_cast<unsigned>(c[0]);
}

CPP
refused string_append__reference "'c' refers to an element of 's', which may have been reallocated or ended by 'std::basic_string<char>::append'" <<'CPP'
verified unsigned string_append__reference()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    s.append(u);
    return static_cast<unsigned>(c);
}

CPP
refused string_append__const_reference "'c' refers to an element of 's', which may have been reallocated or ended by 'std::basic_string<char>::append'" <<'CPP'
verified unsigned string_append__const_reference()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    const char& c = s[0];
    s.append(u);
    return static_cast<unsigned>(c);
}

CPP
refused string_append__reference_write "'c' refers to an element of 's', which may have been reallocated or ended by 'std::basic_string<char>::append'" <<'CPP'
verified unsigned string_append__reference_write()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    s.append(u);
    c = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
refused string_append__span "'c' views the storage of 's', which may have been reallocated or ended by 'std::basic_string<char>::append'" <<'CPP'
verified unsigned string_append__span()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    s.append(u);
    return static_cast<unsigned>(c[0]);
}

CPP
refused string_append__span_write "'c' views the storage of 's', which may have been reallocated or ended by 'std::basic_string<char>::append'" <<'CPP'
verified unsigned string_append__span_write()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    s.append(u);
    c[0] = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
refused string_append__const_span "'c' views the storage of 's', which may have been reallocated or ended by 'std::basic_string<char>::append'" <<'CPP'
verified unsigned string_append__const_span()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<const char> c(s);
    s.append(u);
    return static_cast<unsigned>(c[0]);
}

CPP
refused string_copy_assign__reference "'c' refers to an element of 's', which may have been reallocated or ended by 'std::basic_string<char>::operator='" <<'CPP'
verified unsigned string_copy_assign__reference()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    s = u;
    return static_cast<unsigned>(c);
}

CPP
refused string_copy_assign__const_reference "'c' refers to an element of 's', which may have been reallocated or ended by 'std::basic_string<char>::operator='" <<'CPP'
verified unsigned string_copy_assign__const_reference()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    const char& c = s[0];
    s = u;
    return static_cast<unsigned>(c);
}

CPP
refused string_copy_assign__reference_write "'c' refers to an element of 's', which may have been reallocated or ended by 'std::basic_string<char>::operator='" <<'CPP'
verified unsigned string_copy_assign__reference_write()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    s = u;
    c = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
refused string_copy_assign__span "'c' views the storage of 's', which may have been reallocated or ended by 'std::basic_string<char>::operator='" <<'CPP'
verified unsigned string_copy_assign__span()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    s = u;
    return static_cast<unsigned>(c[0]);
}

CPP
refused string_copy_assign__span_write "'c' views the storage of 's', which may have been reallocated or ended by 'std::basic_string<char>::operator='" <<'CPP'
verified unsigned string_copy_assign__span_write()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    s = u;
    c[0] = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
refused string_copy_assign__const_span "'c' views the storage of 's', which may have been reallocated or ended by 'std::basic_string<char>::operator='" <<'CPP'
verified unsigned string_copy_assign__const_span()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<const char> c(s);
    s = u;
    return static_cast<unsigned>(c[0]);
}

CPP
refused string_move_assign__reference "'c' refers to an element of 's', which may have been reallocated or ended by 'std::basic_string<char>::operator='" <<'CPP'
verified unsigned string_move_assign__reference()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    s = std::move(u);
    return static_cast<unsigned>(c);
}

CPP
refused string_move_assign__const_reference "'c' refers to an element of 's', which may have been reallocated or ended by 'std::basic_string<char>::operator='" <<'CPP'
verified unsigned string_move_assign__const_reference()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    const char& c = s[0];
    s = std::move(u);
    return static_cast<unsigned>(c);
}

CPP
refused string_move_assign__reference_write "'c' refers to an element of 's', which may have been reallocated or ended by 'std::basic_string<char>::operator='" <<'CPP'
verified unsigned string_move_assign__reference_write()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    s = std::move(u);
    c = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
refused string_move_assign__span "'c' views the storage of 's', which may have been reallocated or ended by 'std::basic_string<char>::operator='" <<'CPP'
verified unsigned string_move_assign__span()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    s = std::move(u);
    return static_cast<unsigned>(c[0]);
}

CPP
refused string_move_assign__span_write "'c' views the storage of 's', which may have been reallocated or ended by 'std::basic_string<char>::operator='" <<'CPP'
verified unsigned string_move_assign__span_write()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    s = std::move(u);
    c[0] = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
refused string_move_assign__const_span "'c' views the storage of 's', which may have been reallocated or ended by 'std::basic_string<char>::operator='" <<'CPP'
verified unsigned string_move_assign__const_span()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<const char> c(s);
    s = std::move(u);
    return static_cast<unsigned>(c[0]);
}

CPP
refused string_move_from__reference "'c' refers to an element of 's', which may have been reallocated or ended by being moved from" <<'CPP'
verified unsigned string_move_from__reference()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    std::string m = std::move(s);
    return static_cast<unsigned>(c);
}

CPP
refused string_move_from__const_reference "'c' refers to an element of 's', which may have been reallocated or ended by being moved from" <<'CPP'
verified unsigned string_move_from__const_reference()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    const char& c = s[0];
    std::string m = std::move(s);
    return static_cast<unsigned>(c);
}

CPP
refused string_move_from__reference_write "'c' refers to an element of 's', which may have been reallocated or ended by being moved from" <<'CPP'
verified unsigned string_move_from__reference_write()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    std::string m = std::move(s);
    c = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
refused string_move_from__span "'c' views the storage of 's', which may have been reallocated or ended by being moved from" <<'CPP'
verified unsigned string_move_from__span()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    std::string m = std::move(s);
    return static_cast<unsigned>(c[0]);
}

CPP
refused string_move_from__span_write "'c' views the storage of 's', which may have been reallocated or ended by being moved from" <<'CPP'
verified unsigned string_move_from__span_write()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    std::string m = std::move(s);
    c[0] = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
refused string_move_from__const_span "'c' views the storage of 's', which may have been reallocated or ended by being moved from" <<'CPP'
verified unsigned string_move_from__const_span()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<const char> c(s);
    std::string m = std::move(s);
    return static_cast<unsigned>(c[0]);
}

CPP
refused string_mutable_call__reference "'c' refers to an element of 's', which may have been reallocated or ended by passing it by mutable reference to 'touch_string'" <<'CPP'
verified unsigned string_mutable_call__reference()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    touch_string(s);
    return static_cast<unsigned>(c);
}

CPP
refused string_mutable_call__const_reference "'c' refers to an element of 's', which may have been reallocated or ended by passing it by mutable reference to 'touch_string'" <<'CPP'
verified unsigned string_mutable_call__const_reference()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    const char& c = s[0];
    touch_string(s);
    return static_cast<unsigned>(c);
}

CPP
refused string_mutable_call__reference_write "'c' refers to an element of 's', which may have been reallocated or ended by passing it by mutable reference to 'touch_string'" <<'CPP'
verified unsigned string_mutable_call__reference_write()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    touch_string(s);
    c = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
refused string_mutable_call__span "'c' views the storage of 's', which may have been reallocated or ended by passing it by mutable reference to 'touch_string'" <<'CPP'
verified unsigned string_mutable_call__span()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    touch_string(s);
    return static_cast<unsigned>(c[0]);
}

CPP
refused string_mutable_call__span_write "'c' views the storage of 's', which may have been reallocated or ended by passing it by mutable reference to 'touch_string'" <<'CPP'
verified unsigned string_mutable_call__span_write()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    touch_string(s);
    c[0] = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
refused string_mutable_call__const_span "'c' views the storage of 's', which may have been reallocated or ended by passing it by mutable reference to 'touch_string'" <<'CPP'
verified unsigned string_mutable_call__const_span()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<const char> c(s);
    touch_string(s);
    return static_cast<unsigned>(c[0]);
}

CPP
refused string_unsafe_block__reference "'c' refers to an element of 's', which may have been reallocated or ended by the unsafe block" <<'CPP'
verified unsigned string_unsafe_block__reference()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    unsafe {
        grow_string(s);
    }
    return static_cast<unsigned>(c);
}

CPP
refused string_unsafe_block__const_reference "'c' refers to an element of 's', which may have been reallocated or ended by the unsafe block" <<'CPP'
verified unsigned string_unsafe_block__const_reference()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    const char& c = s[0];
    unsafe {
        grow_string(s);
    }
    return static_cast<unsigned>(c);
}

CPP
refused string_unsafe_block__reference_write "'c' refers to an element of 's', which may have been reallocated or ended by the unsafe block" <<'CPP'
verified unsigned string_unsafe_block__reference_write()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    char& c = s[0];
    unsafe {
        grow_string(s);
    }
    c = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
refused string_unsafe_block__span "'c' views the storage of 's', which may have been reallocated or ended by the unsafe block" <<'CPP'
verified unsigned string_unsafe_block__span()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    unsafe {
        grow_string(s);
    }
    return static_cast<unsigned>(c[0]);
}

CPP
refused string_unsafe_block__span_write "'c' views the storage of 's', which may have been reallocated or ended by the unsafe block" <<'CPP'
verified unsigned string_unsafe_block__span_write()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<char> c(s);
    unsafe {
        grow_string(s);
    }
    c[0] = 'z';
    return static_cast<unsigned>(s[0]);
}

CPP
refused string_unsafe_block__const_span "'c' views the storage of 's', which may have been reallocated or ended by the unsafe block" <<'CPP'
verified unsigned string_unsafe_block__const_span()
    ensures (result == result)
{
    std::string s = "ab";
    std::string t = "cd";
    std::string u = "ef";
    s[0] = 'a';
    std::span<const char> c(s);
    unsafe {
        grow_string(s);
    }
    return static_cast<unsigned>(c[0]);
}

CPP
check

# --- Where the event stands: in one branch, after a return, in a loop, through another name of the container (STDMODEL-015, STDMODEL-025)
begin placement
refused placement_event_then_branch "'r' refers to an element of 'v', which may have been reallocated or ended by 'std::vector::push_back'" <<'CPP'
verified unsigned placement_event_then_branch(bool b)
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    unsigned& r = v[0];
    if (b) {
        v.push_back(1u);
    }
    return r;
}

CPP
refused placement_event_else_branch "'r' refers to an element of 'v', which may have been reallocated or ended by 'std::vector::clear'" <<'CPP'
verified unsigned placement_event_else_branch(bool b)
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    unsigned& r = v[0];
    if (b) {
        v[1] = 3u;
    } else {
        v.clear();
    }
    return r;
}

CPP
refused placement_event_nested_block "'r' refers to an element of 'v', which may have been reallocated or ended by 'std::vector::reserve'" <<'CPP'
verified unsigned placement_event_nested_block()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    unsigned& r = v[0];
    {
        {
            v.reserve(9ul);
        }
    }
    return r;
}

CPP
refused placement_event_in_loop_use_after "'r' refers to an element of 'v', which may have been reallocated or ended by the loop" <<'CPP'
verified unsigned placement_event_in_loop_use_after(std::size_t n)
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    unsigned& r = v[0];
    std::size_t k = 0ul;
    while (k < n)
        invariant (k <= n)
        decreases (n - k)
    {
        v.push_back(1u);
        ++k;
    }
    return r;
}

CPP
refused placement_use_in_loop_event_later "'r' refers to an element of 'v', which may have been reallocated or ended by the loop" <<'CPP'
verified unsigned placement_use_in_loop_event_later()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    unsigned& r = v[0];
    unsigned seen = 0u;
    std::size_t k = 0ul;
    while (k < 2ul)
        invariant (k <= 2ul)
        decreases (2ul - k)
    {
        seen = r;
        v.push_back(1u);
        ++k;
    }
    return seen;
}

CPP
refused placement_zero_iteration_loop "'r' refers to an element of 'v', which may have been reallocated or ended by the loop" <<'CPP'
verified unsigned placement_zero_iteration_loop()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    unsigned& r = v[0];
    std::size_t k = 0ul;
    while (k < 0ul)
        invariant (k <= 0ul)
        decreases (0ul - k)
    {
        v.push_back(1u);
        ++k;
    }
    return r;
}

CPP
refused placement_view_formed_in_branch "span 's' is modeled only as a view of a whole vector or string this body tracks" <<'CPP'
verified unsigned placement_view_formed_in_branch(bool b)
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    std::vector<unsigned> w{4u};
    std::span<unsigned> s(b ? v : w);
    v.push_back(1u);
    return s[0];
}

CPP
refused placement_local_reference_alias "local 'a' has type 'std::vector<unsigned int>', which is not modeled" <<'CPP'
verified unsigned placement_local_reference_alias()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    unsigned& r = v[0];
    std::vector<unsigned>& a = v;
    a.push_back(1u);
    return r;
}

CPP
refused placement_pointer_alias "local 'p' has type 'std::vector<unsigned int> *', which is not modeled" <<'CPP'
verified unsigned placement_pointer_alias()
    ensures (result == result)
{
    std::vector<unsigned> v{1u, 2u};
    unsigned& r = v[0];
    std::vector<unsigned>* p = &v;
    p->push_back(1u);
    return r;
}

CPP
refused placement_param_reference_event "'r' refers to an element of 'v', which may have been reallocated or ended by 'std::vector::push_back'" <<'CPP'
verified unsigned placement_param_reference_event(std::vector<unsigned>& v)
    expects (0ul < v.size())
    ensures (result == result)
{
    unsigned& r = v[0];
    v.push_back(1u);
    return r;
}

CPP
refused placement_param_reference_call "'r' refers to an element of 'v', which may have been reallocated or ended by passing it by mutable reference to 'touch'" <<'CPP'
verified unsigned placement_param_reference_call(std::vector<unsigned>& v)
    expects (0ul < v.size())
    ensures (result == result)
{
    unsigned& r = v[0];
    touch(v);
    return r;
}

CPP
refused placement_element_after_fill_call "return path 'placement_element_after_fill_call path" <<'CPP'
verified unsigned placement_element_after_fill_call()
    ensures (result == 2u)
{
    std::vector<unsigned> v{1u, 2u};
    fill(v);
    return v[1];
}

CPP
refused placement_ref_after_fill_call "return path 'placement_ref_after_fill_call path" <<'CPP'
verified unsigned placement_ref_after_fill_call()
    ensures (result == 2u)
{
    std::vector<unsigned> v{1u, 2u};
    unsigned& r = v[1];
    fill(v);
    return r;
}

CPP
# A call statement that copies a container into a by-value parameter stands
# inside the node destroying that temporary, whose destructor is the library's
# own code, and only a container mutator is modeled there (STDMODEL-023). The
# accepted twins bind the call's result.
refused placement_copy_call_statement "a call statement creates a temporary whose destruction at the statement's end runs a user-provided destructor" <<'CPP'
verified unsigned placement_copy_call_statement()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    unsigned& r = v[0];
    keep_copy(v);
    return r;
}

CPP
refused placement_copy_call_statement_span "a call statement creates a temporary whose destruction at the statement's end runs a user-provided destructor" <<'CPP'
verified unsigned placement_copy_call_statement_span()
    ensures (result == 1u)
{
    std::vector<unsigned> v{1u, 2u};
    std::span<unsigned> s(v);
    keep_copy(v);
    return s[0];
}

CPP
check

echo 'every use of a view or element reference after its storage may have changed is refused'
