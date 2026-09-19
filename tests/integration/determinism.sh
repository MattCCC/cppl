#!/usr/bin/env bash
# The same input, compiler and configuration must produce the same verification
# status, the same obligation identity and the same result, every time.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/determinism.XXXXXX")

# A failing Law prints its obligation identity, so the identity itself is
# compared, not only the outcome.
first="$run/first.log"
second="$run/second.log"

"$CPPL" -std=c++17 "$FIXTURES/false_law.cpp" -o "$run/first" > "$first" 2>&1 || true
"$CPPL" -std=c++17 "$FIXTURES/false_law.cpp" -o "$run/second" > "$second" 2>&1 || true

if ! diff -u "$first" "$second" > "$run/diff"; then
    echo "verification of the same input differed between runs" >&2
    cat "$run/diff" >&2
    exit 1
fi

grep -qE "obligation [0-9a-f]{16}" "$first"

# And the successful path is equally stable.
first_report="$run/first.report"
second_report="$run/second.report"
"$CPPL" -std=c++17 "$FIXTURES/identity_law.cpp" -o "$run/law_a" --cppl-trust-report \
    > "$first_report"
"$CPPL" -std=c++17 "$FIXTURES/identity_law.cpp" -o "$run/law_b" --cppl-trust-report \
    > "$second_report"
diff -u "$first_report" "$second_report" > /dev/null

echo "verification results and obligation identities are reproducible"
