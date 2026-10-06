#!/usr/bin/env bash
# Default arguments of verified functions (SPEC.md R.16).
#
# SPEC: CONTRACTCOMP-002, EDGECASE-038, TUBOUND-003, TUBOUND-004, ERASE-001
# TRUST.md TCB-CALL-003, TCB-CALL-005
#
# A call relying on a default argument is verified with the default evaluated
# where the call stands, as if written there: the callee's contract is
# instantiated at its value, and its precondition, its refined parameter, the
# precondition of a call the default makes and the descent of a recursive call
# are owed for it at the call. In c++17, c++20 and c++23:
#
#   - fixtures/equivalence/default_arguments.cpp verifies, calls relying on
#     defaults and calls writing the argument alike, and its program prints the
#     values its contracts state;
#   - the program keeps every default as written, and nothing generated for
#     the analysis (tests/e2e/erasure_equivalence.sh compares it with its
#     erasure by hand, code and text);
#   - across translation units a caller relies on its own declaration's
#     default, through the contract the defining unit proved without one, and
#     the two objects link and run.
#
# Every refusal is in tests/negative/default_arguments.sh.
set -euo pipefail
CPPL="$1"
FIXTURES="$2"
WORK="$3"
CLANG="$4"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/default-arguments.XXXXXX")

fail() {
    echo "$1" >&2
    exit 1
}

expected='take() == 50
narrow() == 7
doubled() == 8
sum(10) == 13
need() == 70
nested() == 7
bumped(3) == 5
walk() == 0
below(3) == 3
picked() == 7
caller() == 148
unsigned_caller() == 22
member_caller(6) == 10'

for standard in c++17 c++20 c++23; do
    base="$run/fixture-$standard"
    "$CPPL" "-std=$standard" "$FIXTURES/equivalence/default_arguments.cpp" -o "$base" --cppl-trust-report \
        "--cppl-emit-projection=$base.runtime.ii" > "$base.report"
    grep -Eq '^Function contracts proven: +16$' "$base.report" || fail "not every contract was proven ($standard)"
    grep -Eq '^Call preconditions proven: +7$' "$base.report" || fail "not every call precondition was proven ($standard)"
    grep -Eq '^Recursive call measures proven: +1$' "$base.report" || fail "the recursive call's descent was not proven ($standard)"
    grep -Eq '^Unresolved obligations: +0$' "$base.report" || fail "an obligation is unresolved ($standard)"
    for function in take narrow doubled sum need nested bumped walk Counter::plus below first picked caller \
        unsigned_caller member_caller; do
        grep -Eq "^  contract of $function \\(" "$base.report" || fail "the contract of $function is not listed ($standard)"
    done
    [ "$("$base")" = "$expected" ] || fail "the program printed other values than its contracts state ($standard)"

    # The program keeps each default where it was written.
    for written in 'int take(int p = 50)' 'int narrow(Small s = 7)' 'int doubled(int p = twice(4))' \
        'int sum(Small a, Small b = 3)' 'int need(int p = 70)' 'unsigned nested(unsigned p = add(5u))' \
        'unsigned walk(unsigned k = 0u)' 'int plus(int k = 4) const' 'T below(T v, T hi = T(10))' \
        'unsigned picked(unsigned p = (first<4u, 5u>()), unsigned q = 3u)' 'unsigned add(unsigned x, unsigned k = 2u)'; do
        grep -Fq "$written" "$base.runtime.ii" || fail "the program lost the default of '$written' ($standard)"
    done
    if grep -q '__cppl_' "$base.runtime.ii"; then
        fail "analysis scaffolding reached the program ($standard)"
    fi
    "$CLANG" "-std=$standard" -x c++-cpp-output "$base.runtime.ii" -o "$base.erased"
    [ "$("$base.erased")" = "$expected" ] || fail "the erased program behaves differently ($standard)"
done

# Across translation units: the defining unit states no default, and each
# caller relies on the one its own declaration states.
mkdir "$run/units"
cd "$run/units"
cat > library.cpp <<'CPP'
verified int need(int p) expects (p > 60) ensures (result == p) { return p; }
CPP
cat > client.cpp <<'CPP'
#include <cstdio>
verified int need(int p = 70) expects (p > 60) ensures (result == p);
verified int relies() ensures (result == 70) { return need(); }
int main() {
    std::printf("%d %d\n", relies(), need(61));
    return 0;
}
CPP
for standard in c++17 c++20 c++23; do
    "$CPPL" "-std=$standard" -c library.cpp -o "library-$standard.o" "--cppl-emit-interface=library-$standard.cppli"
    "$CPPL" "-std=$standard" -c client.cpp -o "client-$standard.o" "--cppl-import-interface=library-$standard.cppli" \
        --cppl-trust-report > "client-$standard.report"
    grep -Eq '^Function contracts proven: +1$' "client-$standard.report" || fail "relies was not proven ($standard)"
    grep -Eq '^Function contracts imported: +1$' "client-$standard.report" || fail "need was not imported ($standard)"
    grep -Eq '^Call preconditions proven: +1$' "client-$standard.report" || fail "the default's precondition was not proven ($standard)"
    grep -Eq '^Unresolved obligations: +0$' "client-$standard.report" || fail "an obligation is unresolved ($standard)"
    "$CLANG" "library-$standard.o" "client-$standard.o" -o "linked-$standard"
    [ "$("./linked-$standard")" = '70 61' ] || fail "the linked units printed other values ($standard)"
done

echo "a call relying on a default argument is verified at the default's value, and the program keeps it"
