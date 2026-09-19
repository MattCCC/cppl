#!/usr/bin/env bash
# Ordinary C++ compiles and runs through cppl with no source changes, in every
# supported target standard.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/ordinary.XXXXXX")

for standard in c++17 c++20 c++23; do
    binary="$run/hello-$standard"
    "$CPPL" "-std=$standard" "$FIXTURES/hello.cpp" -o "$binary"

    output=$("$binary")
    if [ "$output" != "hello" ]; then
        echo "$standard: expected 'hello', got '$output'" >&2
        exit 1
    fi
done

echo "ordinary C++ builds and runs in c++17, c++20 and c++23"
