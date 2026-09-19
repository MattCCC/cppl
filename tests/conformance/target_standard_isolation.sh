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

projection="$run/identity_law.runtime.cpp"
"$CPPL" -std=c++17 "$FIXTURES/identity_law.cpp" -o "$run/identity_law" \
    "--cppl-emit-projection=$projection"

if grep -Eq '(^|[^[:alnum:]_])(law|ensures|expects|pure|proof|proves|ghost)([^[:alnum:]_]|$)' \
        "$projection"; then
    echo "the runtime program still contains formal syntax" >&2
    grep -nE '(^|[^[:alnum:]_])(law|ensures|expects|pure|proof|proves|ghost)([^[:alnum:]_]|$)' \
        "$projection" >&2
    exit 1
fi

# The runtime program is ordinary C++17 on its own terms.
"$CLANG" -std=c++17 -pedantic-errors -Wall -Werror -x c++-cpp-output "$projection" \
    -o "$run/identity_law_direct"
"$run/identity_law_direct" > "$run/output"

if [ "$(cat "$run/output")" != "41" ]; then
    echo "the erased program did not behave like the original" >&2
    exit 1
fi

echo "the c++17 runtime projection is free of formal syntax and compiles as c++17"
