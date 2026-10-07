#!/usr/bin/env bash
# SPEC: STORAGE-005, STORAGE-010, VERIFIED-038, STDMODEL-012
# TRUST.md 36.4: a signed index proven only below the extent.
#
# A subscript names an element only where its index lies in [0, N): `a[-1]` is
# outside the array whatever its extent (C++ [expr.sub]). Each program below
# subscripts with a signed index of which only `i < N` is known, in every form
# a verified body subscripts by -- a built-in array local, read and written, at
# a negative constant, a member of a local and of the implicit object, a
# std::array local, member and reference, a vector, a span, a pointer under a
# sized capability, and an element read in a condition, on an arm of a
# selection and as a call's argument -- and each is refused: the bound it owes
# is not proven. The accepted twins at the end state `0 <= i` as well.
set -euo pipefail
CPPL="$1"
WORK="$3"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/negative-indices.XXXXXX")

fail() {
    echo "$1" >&2
    exit 1
}

unproven="element index' is not proven"

# refused <name>: the program read from stdin is refused for an unproven
# element bound, and writes no executable.
refused() {
    local name="$1"
    cat > "$run/$name.cpp"
    echo 'int main() { return 0; }' >> "$run/$name.cpp"
    if "$CPPL" -std=c++20 "$run/$name.cpp" -o "$run/$name" > "$run/$name.log" 2>&1; then
        cat "$run/$name.log" >&2
        fail "a negative index was accepted: $name"
    fi
    [ ! -e "$run/$name" ] || fail "$name was refused, but wrote an executable"
    grep -qF -- "$unproven" "$run/$name.log" || {
        cat "$run/$name.log" >&2
        fail "$name was refused, but not for its unproven element bound"
    }
}

# accepted <name>: the program read from stdin verifies.
accepted() {
    local name="$1"
    cat > "$run/$name.cpp"
    echo 'int main() { return 0; }' >> "$run/$name.cpp"
    if ! "$CPPL" -std=c++20 "$run/$name.cpp" -o "$run/$name" > "$run/$name.log" 2>&1; then
        cat "$run/$name.log" >&2
        fail "a bounded index was refused: $name"
    fi
}

refused local_read <<'CPP'
verified int f(int i) expects (i < 4) ensures (true) {
    int a[4] = {1, 2, 3, 4};
    return a[i];
}
CPP

refused local_write <<'CPP'
verified int f(int i) expects (i < 4) ensures (true) {
    int a[4] = {1, 2, 3, 4};
    a[i] = 0;
    return a[0];
}
CPP

refused negative_constant <<'CPP'
verified int f() ensures (true) {
    int a[4] = {1, 2, 3, 4};
    return a[-1];
}
CPP

# The element before `items` is `first`: proven to keep 7, it held 0.
refused member_of_a_local <<'CPP'
struct Slots {
    int first;
    int items[4];
};
verified int f(int i) expects (i < 4) ensures (result == 7) {
    Slots s{7, {1, 2, 3, 4}};
    s.items[i] = 0;
    return s.first;
}
CPP

refused member_of_the_implicit_object <<'CPP'
struct Ring {
    int slots[8];
    int head;
    verified int at(int i) const expects (i < 8) ensures (true) {
        return slots[i];
    }
};
CPP

refused std_array_local <<'CPP'
#include <array>
verified int f(int i) expects (i < 4) ensures (true) {
    std::array<int, 4> a{1, 2, 3, 4};
    return a[i];
}
CPP

refused std_array_member <<'CPP'
#include <array>
struct Stack {
    std::array<int, 4> items;
    verified int at(int i) const expects (i < 4) ensures (true) {
        return items[i];
    }
};
CPP

refused std_array_reference <<'CPP'
#include <array>
verified int f(const std::array<int, 4>& a, int i) expects (i < 4) ensures (true) {
    return a[i];
}
CPP

refused vector_element <<'CPP'
#include <vector>
verified int f(const std::vector<int>& v, int i)
    expects (v.size() == 4u && i < 4)
    ensures (true)
{
    return v[i];
}
CPP

refused span_element <<'CPP'
#include <span>
verified int f(std::span<const int> s, int i)
    expects (readable(s) && s.size() == 4u && i < 4)
    ensures (true)
{
    return s[i];
}
CPP

refused pointer_under_a_sized_capability <<'CPP'
verified int f(const int* p, int i, int n)
    expects (readable(p, n) && i < n)
    ensures (true)
{
    return p[i];
}
CPP

refused element_in_a_condition <<'CPP'
verified int f(int i) expects (i < 4) ensures (true) {
    int a[4] = {1, 2, 3, 4};
    if (i < 4 && a[i] > 2) {
        return 1;
    }
    return 0;
}
CPP

refused element_on_a_selected_arm <<'CPP'
verified int f(int i) ensures (true) {
    int a[4] = {1, 2, 3, 4};
    return i < 4 ? a[i] : 0;
}
CPP

refused element_as_an_argument <<'CPP'
verified int kept(int x) ensures (result == x) { return x; }
verified int f(int i) ensures (true) {
    int a[4] = {1, 2, 3, 4};
    return kept(i < 4 ? a[i] : 0);
}
CPP

accepted local_read_bounded <<'CPP'
verified int f(int i) expects (0 <= i && i < 4) ensures (true) {
    int a[4] = {1, 2, 3, 4};
    return a[i];
}
CPP

accepted selected_arm_bounded <<'CPP'
verified int f(int i) ensures (true) {
    int a[4] = {1, 2, 3, 4};
    return i >= 0 && i < 4 ? a[i] : 0;
}
CPP

echo 'every subscript with an index proven only below its extent is refused'
