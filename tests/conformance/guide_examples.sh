#!/usr/bin/env bash
# The developer guide marks the examples it promises verify:
#
#   <!-- cppl-example: verify -->
#
#   ```cpp
#   ...
#   ```
#
# Each marked example is compiled by the compiler it documents, so a guide
# example cannot show code this implementation refuses. The marker must be
# followed, after blank lines only, by a C++ fence; one that is not is itself an
# error, so a marker cannot silently stop covering its example.
set -euo pipefail

CPPL="$1"
GUIDE="$2"
WORK="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/guide.XXXXXX")

awk -v dir="$run" '
/<!-- cppl-example: verify -->/ {
    if (pending) { print "marker at line " pending " has no C++ example" > "/dev/stderr"; failed = 1 }
    pending = NR
    next
}
/^```/ {
    if (inblock) { inblock = 0; close(out); next }
    if (pending) {
        if (substr($0, 4) !~ /^cpp/) {
            print "marker at line " pending " is followed by a non-C++ fence" > "/dev/stderr"
            failed = 1
        } else {
            count++
            out = dir "/example-" count ".cpp"
            printf "#line %d \"DEVELOPER_GUIDE.md\"\n", NR + 1 > out
            inblock = 1
        }
    }
    pending = 0
    next
}
inblock { print > out; next }
/^[[:space:]]*$/ { next }
{
    if (pending) { print "marker at line " pending " is not followed by an example" > "/dev/stderr"; failed = 1 }
    pending = 0
}
END { exit failed }
' "$GUIDE"

examples=0
for source in "$run"/example-*.cpp; do
    [ -e "$source" ] || continue
    examples=$((examples + 1))
    if ! "$CPPL" -std=c++17 -c "$source" -o "$source.o" > "$source.log" 2>&1; then
        echo "a developer guide example marked to verify was refused:" >&2
        cat "$source" >&2
        cat "$source.log" >&2
        exit 1
    fi
done

if [ "$examples" -eq 0 ]; then
    echo "no developer guide example is marked to verify" >&2
    exit 1
fi

echo "developer guide examples: $examples verified"
