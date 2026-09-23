#!/usr/bin/env bash
# SPEC: WORD-002, WORD-008, WORD-010
# C++L words used as ordinary identifiers stay ordinary identifiers.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/contextual.XXXXXX")

for standard in c++17 c++20 c++23; do
    binary="$run/contextual-$standard"
    "$CPPL" "-std=$standard" "$FIXTURES/contextual_identifiers.cpp" -o "$binary"
    "$binary"

    # The words of `contradiction` and of a case omission, used as names inside
    # proofs as well as in ordinary C++. The proof still verifies, so every
    # keyword position was read as a keyword and every name as a name.
    words="$run/omission-words-$standard"
    "$CPPL" "-std=$standard" "$FIXTURES/contextual_omission_words.cpp" -o "$words" --cppl-trust-report \
        > "$words.report"
    grep -Eq "^Laws proven: +1$" "$words.report"
    grep -Eq "^Omitted cases proven: +2$" "$words.report"
    grep -Eq "^Unresolved obligations: +0$" "$words.report"
    if [ "$("$words")" != "4" ]; then
        echo "the program using the omission words as names computed the wrong value ($standard)" >&2
        exit 1
    fi
done

echo "contextual words remain ordinary identifiers in c++17, c++20 and c++23"
