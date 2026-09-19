#!/usr/bin/env bash
# A Law written in a header is verified where the header is used, and ordinary
# compiler arguments reach Clang untouched.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/header.XXXXXX")

binary="$run/header_law"
report="$run/header_law.report"

"$CPPL" -std=c++17 -I"$FIXTURES/include" -DVALUE=41 -O2 \
    "$FIXTURES/header_law_main.cpp" -o "$binary" --cppl-trust-report > "$report"

grep -Eq "^Laws proven: +1$" "$report"
grep -Eq "^Unresolved obligations: +0$" "$report"

# -DVALUE reached the compiler: the program returns scale(41) - 41.
"$binary"

echo "a Law declared in a header is proven, and -I/-D/-O2 pass through"
