#!/usr/bin/env bash
# SPEC: STDMODEL-010, STDMODEL-011, STDMODEL-012, STDMODEL-013, STDMODEL-014, STDMODEL-015
# SPEC: STDMODEL-016, STDMODEL-017, STDMODEL-018, STDMODEL-020, STDMODEL-021, STDMODEL-022
# TRUST.md TCB-LIB-006, TCB-LIB-007, TCB-LIB-008, TCB-LIB-009, TCB-LIB-010
#
# The verified sequence subset (RFC 0020): the accepted twins of every refusal
# in `negative/containers.sh` verify, each claim rests on the library model it
# uses and says so, the program computes what its contracts state, and erased
# it is the same code as the same program written in ordinary C++.
#
# Every standard library the suite runs against compiles this: libc++ on
# macOS, libstdc++ in the Linux GCC job. The model names no library's layout,
# so the same claims verify against both.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"
CLANG="$4"
# shellcheck source=../support/equivalence.sh
source "$(dirname "$0")/../support/equivalence.sh"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/containers.XXXXXX")

"$CPPL" -std=c++20 "$FIXTURES/containers.cpp" -o "$run/program" --cppl-trust-report > "$run/report"

expect() {
    if ! grep -Eq "$1" "$run/report"; then
        echo "the trust report does not state: $1" >&2
        cat "$run/report" >&2
        exit 1
    fi
}

expect '^Function contracts proven: +26$'
expect '^Unresolved obligations: +0$'
# `sum_first`-style bounds passed on, the data pointer's count, and the
# preconditions of the capability callees.
expect '^Call preconditions proven: +4$'
# SPEC: STDMODEL-018
# Every contract but the pointer-only `zero_prefix` rests on a library model,
# and none of those is assumption-free.
expect '^Library-model-dependent claims: 25$'
expect '^Assumption-free claims: +1$'
expect '^  contract of zero_prefix \(.*containers\.cpp:[0-9]+\), identity [0-9a-f]+$'
expect '^    rests on the std::vector model, in its own contract or body$'
expect '^    rests on the std::basic_string<char> model, in its own contract or body$'
expect '^    rests on the std::span model, in its own contract or body$'
expect '^    rests on the std::array model, in its own contract or body$'
expect '^    rests on the std::vector model, through a verified call it makes$'

output=$("$run/program")
if [ "$output" != "5 4 7 3 7 5 4 1 2 0 3 6 9 16 y 6 2 1 6 3 5" ]; then
    echo "the verified program printed '$output'" >&2
    exit 1
fi

# SPEC: STDMODEL-022, ERASE-002
# Erased, the subset is the same code as the program without C++L, at -O0 and
# -O2, in every standard `std::span` exists in.
for standard in c++20 c++23; do
    base="$run/equivalence-$standard"
    "$CPPL" "-std=$standard" "$FIXTURES/equivalence/containers.cpp" -o "$base.cppl" --cppl-trust-report \
        "--cppl-emit-projection=$base.runtime.ii" > "$base.report"
    for line in 'Function contracts proven: +7' 'Unresolved obligations: +0' 'Library-model-dependent claims: 7'; do
        if ! grep -Eq "^$line\$" "$base.report"; then
            echo "the equivalence fixture ($standard) does not report '$line'" >&2
            cat "$base.report" >&2
            exit 1
        fi
    done
    if grep -q '__cppl_' "$base.runtime.ii"; then
        echo "analysis scaffolding reached the runtime program ($standard)" >&2
        exit 1
    fi
    "$CLANG" "-std=$standard" "$FIXTURES/equivalence/containers.reference.cpp" -o "$base.reference"
    erased=$("$base.cppl")
    if [ "$erased" != "5 18 2 6 9 3 5" ] || [ "$("$base.reference")" != "$erased" ]; then
        echo "the erased program ($standard) printed '$erased', unlike its reference" >&2
        exit 1
    fi
    for level in -O0 -O2; do
        assembly "$base$level.cppl" "$CPPL" "-std=$standard" "$level" "$FIXTURES/equivalence/containers.cpp"
        assembly "$base$level.reference" "$CLANG" "-std=$standard" "$level" \
            "$FIXTURES/equivalence/containers.reference.cpp"
        same_code "containers ($standard, $level)" "$base$level.cppl" "$base$level.reference"
    done
done

# SPEC: STDMODEL-018, TUBOUND-006
# Container contracts cross translation units through a verification interface.
# A claim proven through one rests on the imported contract and is never
# assumption-free; the models it names are those its own body uses and those
# the imported declaration uses, since an interface records no models of the
# other unit's body (TRUST.md TCB-LIB-010).
units="$run/units"
mkdir -p "$units"
cp "$FIXTURES/cross_tu/sequences.hpp" "$FIXTURES/cross_tu/sequences.cpp" "$FIXTURES/cross_tu/sequences_client.cpp" \
    "$units/"
(
    cd "$units"
    "$CPPL" -std=c++20 -c sequences.cpp -o sequences.o --cppl-emit-interface=sequences.cppli --cppl-trust-report \
        > sequences.report
    "$CPPL" -std=c++20 -c sequences_client.cpp -o client.o --cppl-import-interface=sequences.cppli \
        --cppl-trust-report > client.report
    "$CLANG" sequences.o client.o -o program
)
for line in 'Function contracts proven: +2' 'Library-model-dependent claims: 2'; do
    grep -Eq "^$line\$" "$units/sequences.report" || { echo "sequences.cpp does not report '$line'" >&2; exit 1; }
done
for line in 'Function contracts proven: +2' 'Function contracts imported: +2' 'Assumption-free claims: +0' \
    'Library-model-dependent claims: 1'; do
    grep -Eq "^$line\$" "$units/client.report" || {
        echo "the client does not report '$line'" >&2
        cat "$units/client.report" >&2
        exit 1
    }
done
sed -n '/^Library-model-dependent claims:/,$p' "$units/client.report" > "$units/models.listed"
grep -q '^  contract of through_copy ' "$units/models.listed" || { echo "through_copy is not listed" >&2; exit 1; }
if [ "$("$units/program")" != "3 3" ]; then
    echo "the program built from two units printed '$("$units/program")'" >&2
    exit 1
fi

echo 'every verified container operation rests on its model, runs as stated, and erases to the same code'
