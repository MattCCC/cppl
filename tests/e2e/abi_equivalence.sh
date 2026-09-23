#!/usr/bin/env bash
# SPEC: ABI-001, ABI-002, ABI-003, ABI-005, ERASE-010, ERASEMATRIX-002
# TRUST.md TCB-ABI-001, TCB-ERASE-002, TCB-ERASE-007, TCB-ERASE-008
#
# C++L verification metadata changes no native ABI.
#
# `fixtures/equivalence/abi_library.cpp` states its interface with refinement
# types, contracts and a law. `abi_client.cpp` is ordinary C++, compiled by
# Clang alone, that declares the same interface with the base types the
# refinements lower to. In every supported standard and at both optimization
# levels:
#
#   - the library compiles to exactly the code of its erasure written by hand,
#     `abi_library.reference.cpp`: the same symbols under the same mangled
#     names, the same layouts and the same calling conventions;
#   - the client links against the C++L-compiled library, which it could not do
#     if a refinement had reached a mangled name, and calls it: records passed
#     in registers and through memory, by value and by reference, returned by
#     value, and a C-linkage function;
#   - both sides agree on the size, alignment and member offsets of the records
#     whose members are refined;
#   - the client sees the same results through the reference library.
set -euo pipefail

CPPL="$1"
FIXTURES="$2/equivalence"
WORK="$3"
CLANG="$4"
# shellcheck source=../support/equivalence.sh
source "$(dirname "$0")/../support/equivalence.sh"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/abi-equivalence.XXXXXX")

expected=$'42 42 42 0 100 64 5 9 9 3 6\n1'

for standard in c++17 c++20 c++23; do
    "$CPPL" "-std=$standard" -c "$FIXTURES/abi_library.cpp" -o "$run/verified-$standard.o" --cppl-trust-report \
        > "$run/verified-$standard.report"
    grep -Eq '^Laws proven: +1$' "$run/verified-$standard.report"
    grep -Eq '^Function contracts proven: +7$' "$run/verified-$standard.report"
    grep -Eq '^Unresolved obligations: +0$' "$run/verified-$standard.report"

    for level in -O0 -O2; do
        base="$run/$standard$level"

        assembly "$base.library" "$CPPL" "-std=$standard" "$level" "$FIXTURES/abi_library.cpp"
        assembly "$base.reference" "$CLANG" "-std=$standard" "$level" "$FIXTURES/abi_library.reference.cpp"
        same_code "abi_library ($standard, $level)" "$base.library" "$base.reference"

        # No refinement's name reaches a symbol, a section or a datum.
        if grep -Eq 'NonNegative|Percentage|Positive|Small|Index' "$base.library"; then
            echo "a refinement name reached the code of abi_library ($standard, $level)" >&2
            exit 1
        fi

        "$CPPL" "-std=$standard" "$level" -c "$FIXTURES/abi_library.cpp" -o "$base.library.o"
        "$CLANG" "-std=$standard" "$level" -c "$FIXTURES/abi_library.reference.cpp" -o "$base.reference.o"
        "$CLANG" "-std=$standard" "$level" -c "$FIXTURES/abi_client.cpp" -o "$base.client.o"
        "$CLANG" "$base.library.o" "$base.client.o" -o "$base.with-library"
        "$CLANG" "$base.reference.o" "$base.client.o" -o "$base.with-reference"

        output=$("$base.with-library")
        if [ "$output" != "$expected" ]; then
            printf 'the ordinary client saw\n%s\nthrough the C++L library (%s, %s), expected\n%s\n' \
                "$output" "$standard" "$level" "$expected" >&2
            exit 1
        fi
        if [ "$("$base.with-reference")" != "$output" ]; then
            echo "the client behaves differently through the reference library ($standard, $level)" >&2
            exit 1
        fi
    done
done

echo 'C++L verification metadata changes no mangled name, layout or calling convention in c++17, c++20 and c++23'
