#!/usr/bin/env bash
# SPEC: CLASS-008, CLASS-010, CLASS-011, STORAGE-005, STDMODEL-011
# TRUST.md TCB-AGGREGATE-001, TCB-AGGREGATE-003
#
# The storage a realistic class holds and a caller hands it
# (`fixtures/member_storage.cpp`): a member array of the implicit object,
# std::array or built in, subscripted at a term; an object a reference parameter
# designates, written member by member and through member calls that may write
# it; a std::array handed by reference; and aggregate initializers that leave
# members out, which C++ value-initializes. In every supported standard each
# contract is proven with nothing unresolved and no assumption, the program
# prints what was proven, and the erased program, compiled by Clang alone,
# prints the same. Refusals are in negative/member_storage.sh.
set -euo pipefail
CPPL="$1"
FIXTURES="$2"
WORK="$3"
CLANG="$4"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/member-storage.XXXXXX")

expected=$'9 9 2 11 0\n5 2\n2 3 0\n2 0\n0 4 0 3\n9'

for standard in c++17 c++20 c++23; do
    "$CPPL" "-std=$standard" "$FIXTURES/member_storage.cpp" -o "$run/program" \
        --cppl-trust-report "--cppl-emit-projection=$run/runtime.cpp" > "$run/report" 2> "$run/err"
    if [ -s "$run/err" ]; then
        cat "$run/err" >&2
        echo "verifying member_storage warned ($standard)" >&2
        exit 1
    fi
    for line in 'Function contracts proven: +20' 'Unresolved obligations: +0' 'Trusted external axioms: +0' \
        'Trust-dependent claims: +0' 'Unsafe-dependent claims: +0'; do
        if ! grep -Eq "^$line\$" "$run/report"; then
            echo "member_storage ($standard) does not report '$line'" >&2
            cat "$run/report" >&2
            exit 1
        fi
    done
    # TRUST.md TCB-LIB-007, TCB-LIB-010 -- `Stack::push` only writes its
    # std::array member through `operator[]`, and `push_one` calls it: both
    # rest on the std::array model, and neither is free of assumptions.
    sed -n '/^Library-model-dependent claims:/,/^$/p' "$run/report" > "$run/models"
    sed -n '/^Assumption-free claims:/,/^Unused trusted laws:/p' "$run/report" > "$run/free"
    for function in 'Stack::push' push_one; do
        if ! grep -q "^  contract of $function " "$run/models" || grep -q "^  contract of $function " "$run/free"; then
            cat "$run/report" >&2
            echo "member_storage ($standard): $function writes a std::array element and does not rest on its model" >&2
            exit 1
        fi
    done

    output=$("$run/program")
    if [ "$output" != "$expected" ]; then
        printf 'member_storage (%s) printed\n%s\nexpected\n%s\n' "$standard" "$output" "$expected" >&2
        exit 1
    fi

    # The runtime program is the source with its contracts blanked: every
    # subscript and call stands as written, and it compiles on its own.
    if grep -q '__cppl_' "$run/runtime.cpp"; then
        echo "analysis scaffolding reached the runtime program ($standard)" >&2
        exit 1
    fi
    for written in 'items[size] = value;' 'return items[size - 1u];' 'slots[head] = value;' 's.pop();' \
        'c.hits = 0u;' 'counts[bucket] = counts[bucket] + 1u;' 'std::array<unsigned, 10> counts{};' 'unsigned firsts[4] = {4u};'; do
        grep -qF "$written" "$run/runtime.cpp" || {
            echo "'$written' is not in the runtime program ($standard)" >&2
            exit 1
        }
    done
    "$CLANG" "-std=$standard" -x c++-cpp-output "$run/runtime.cpp" -o "$run/erased"
    if [ "$("$run/erased")" != "$output" ]; then
        echo "the erased program behaves differently ($standard)" >&2
        exit 1
    fi
done

# TRUST.md 36.4, TCB-LIB-007, TCB-LIB-010 -- that `operator[]` of a std::array
# designates element `i` is trusted of the library for a write as for a read.
# Each body below writes an element and reads none, in every form a write
# reaches one: a member of the implicit object, at a term and at a constant, a
# member of an object a reference designates and a member of a struct local.
# Each was once counted assumption-free.
cat > "$run/writes.cpp" <<'CPP'
#include <array>
#include <cstddef>

struct Stack {
    std::array<unsigned, 4> items;
    std::size_t size;

    verified void push(unsigned value)
        expects (size < 4u)
        ensures (size > 0u)
    {
        items[size] = value;
        size = size + 1u;
    }

    verified void clear_first()
        ensures (true)
    {
        items[0] = 0u;
    }
};

verified void push_onto(Stack& s, unsigned value)
    expects (s.size < 4u)
    ensures (s.size > 0u)
{
    s.items[s.size] = value;
    s.size = s.size + 1u;
}

verified std::size_t local_write()
    ensures (result == 0u)
{
    Stack s{};
    s.items[1] = 5u;
    return s.size;
}

int main() {
    return 0;
}
CPP
"$CPPL" -std=c++20 "$run/writes.cpp" -o "$run/writes" --cppl-trust-report > "$run/writes.report"
sed -n '/^Library-model-dependent claims:/,/^$/p' "$run/writes.report" > "$run/writes.models"
for function in Stack::push Stack::clear_first push_onto local_write; do
    grep -q "^  contract of $function " "$run/writes.models" || {
        cat "$run/writes.report" >&2
        echo "$function writes a std::array element and was not listed as resting on its model" >&2
        exit 1
    }
done
grep -Eq '^Assumption-free claims: +0$' "$run/writes.report" || {
    cat "$run/writes.report" >&2
    echo "a write of a std::array element was counted free of assumptions" >&2
    exit 1
}

# SPEC: ARITH-013 -- a compound assignment, an increment and a decrement of an
# element of an array, built in or std::array, a local's, a member's of the
# implicit object or what a reference designates, are the assignments they
# abbreviate at the element's type, which C++ guarantees where that type is not
# promoted. Each was once refused as if the array's type were promoted.
# Refused twins: `*_update_*` in negative/member_storage.sh.
cat > "$run/updates.cpp" <<'CPP'
#include <array>
#include <cstddef>
#include <cstdio>

struct Counts {
    std::array<unsigned, 4> hits;
    unsigned misses[2];

    verified void hit(std::size_t i)
        expects (i < 4u && misses[0] < 100u)
        ensures (misses[0] <= 100u)
    {
        hits[i] += 1u;
        ++misses[0];
    }
};

verified unsigned added_twice()
    ensures (result == 3u)
{
    std::array<unsigned, 4> a{};
    a[1] += 2u;
    ++a[1];
    return a[1];
}

verified void bump_third(std::array<unsigned, 4>& a)
    expects (a[2] < 100u)
    ensures (a[2] <= 100u)
{
    ++a[2];
}

verified int lowered()
    ensures (result == -5)
{
    int s[2] = {0, 0};
    s[1] -= 5;
    return s[1];
}

verified unsigned at_a_term(std::size_t i)
    expects (i < 4u)
    ensures (true)
{
    unsigned a[4] = {0u, 0u, 0u, 0u};
    a[i] *= 3u;
    a[i]--;
    return a[0];
}

int main() {
    Counts c{{}, {0u, 0u}};
    c.hit(1u);
    std::array<unsigned, 4> a{};
    bump_third(a);
    std::printf("%u %u %u %u %d %u\n", c.hits[1], c.misses[0], added_twice(), a[2], lowered(), at_a_term(0u));
    return 0;
}
CPP
"$CPPL" -std=c++20 "$run/updates.cpp" -o "$run/updates" --cppl-trust-report > "$run/updates.report"
grep -Eq '^Function contracts proven: +5$' "$run/updates.report" && grep -Eq '^Unresolved obligations: +0$' "$run/updates.report" || {
    cat "$run/updates.report" >&2
    echo "the updates of array elements were not all proven" >&2
    exit 1
}
[ "$("$run/updates")" = '1 1 3 1 -5 4294967295' ] || {
    echo "the updates of array elements printed '$("$run/updates")'" >&2
    exit 1
}

echo 'member arrays and objects a reference designates are followed member by member, and the erased program agrees'
