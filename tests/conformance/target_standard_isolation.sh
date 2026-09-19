#!/usr/bin/env bash
# The compiler is built as C++23; the program it hands to code generation must
# stay within the target standard the user selected.
#
# The runtime program is inspected directly: it must carry no formal syntax, and
# it must compile and run on its own under -std=c++17 with a standards-pedantic
# compiler.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"
CLANG="$4"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/isolation.XXXXXX")

# `apply` is left out of the pattern: libc++ declares std::apply, and the point
# here is that nothing of the proof survives, not that the word is unspellable.
formal='(^|[^[:alnum:]_])(law|ensures|expects|pure|proof|proves|ghost|refl|exact)([^[:alnum:]_]|$)'

isolated() {
    local name="$1"
    local projection="$run/$name.runtime.cpp"

    "$CPPL" -std=c++17 "$FIXTURES/$name.cpp" -o "$run/$name" \
        "--cppl-emit-projection=$projection"

    if grep -Eq "$formal" "$projection"; then
        echo "the runtime program for $name still contains formal syntax" >&2
        grep -nE "$formal" "$projection" >&2
        exit 1
    fi

    # The runtime program is ordinary C++17 on its own terms.
    "$CLANG" -std=c++17 -pedantic-errors -Wall -Werror -x c++-cpp-output "$projection" \
        -o "$run/${name}_direct"
    "$run/${name}_direct" > "$run/$name.output"

    if [ "$(cat "$run/$name.output")" != "41" ]; then
        echo "the erased program for $name did not behave like the original" >&2
        exit 1
    fi
}

isolated identity_law
isolated written_proof
isolated instantiated_proof

echo "the c++17 runtime projections are free of formal syntax and compile as c++17"
