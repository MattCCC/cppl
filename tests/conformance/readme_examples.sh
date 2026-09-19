#!/usr/bin/env bash
# The C++L examples in README.md are checked by the compiler itself, so the
# documentation cannot drift away from the grammar it documents.
#
#   ```cpp cppl-example   must compile
#   ```cpp cppl-planned   must be refused, so the README never shows syntax as
#                         working that this implementation does not accept
set -euo pipefail

CPPL="$1"
README="$2"
WORK="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/readme.XXXXXX")

awk -v dir="$run" '
/^```/ {
    if (inblock) { inblock = 0; close(out); next }
    info = substr($0, 4)
    kind = ""
    if (info ~ /cppl-example/) kind = "example"
    else if (info ~ /cppl-planned/) kind = "planned"
    if (kind == "") next
    count[kind]++
    out = dir "/" kind "-" count[kind] ".cpp"
    inblock = 1
    next
}
inblock { print > out }
' "$README"

examples=0
planned=0

for source in "$run"/example-*.cpp; do
    [ -e "$source" ] || continue
    examples=$((examples + 1))
    if ! "$CPPL" -std=c++17 -c "$source" -o "$source.o" > "$source.log" 2>&1; then
        echo "a README example marked cppl-example was rejected:" >&2
        cat "$source" >&2
        cat "$source.log" >&2
        exit 1
    fi
done

for source in "$run"/planned-*.cpp; do
    [ -e "$source" ] || continue
    planned=$((planned + 1))
    if "$CPPL" -std=c++17 -c "$source" -o "$source.o" > "$source.log" 2>&1; then
        echo "a README example marked cppl-planned was accepted:" >&2
        cat "$source" >&2
        echo "the marker is stale: move this example to cppl-example" >&2
        exit 1
    fi
done

if [ "$examples" -eq 0 ]; then
    echo "no README example is marked cppl-example" >&2
    exit 1
fi

if [ "$planned" -eq 0 ]; then
    echo "no README example is marked cppl-planned" >&2
    exit 1
fi

echo "README examples: $examples compiled, $planned correctly refused"
