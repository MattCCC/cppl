#!/usr/bin/env bash
# A C++ compiler that dies from a signal is reported, not impersonated.
#
# `run()` returns the shell's `128 + signal` convention for a signalled child.
# Returning that as this process's own status made `cppl` exit 139 with no
# output at all: indistinguishable from `cppl` itself segfaulting, so the crash
# report never ran, and a build system saw a silent fault with no reason
# attached (`AGENTS.md` 23).
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/signalled.XXXXXX")

# Stands in for a compiler that crashes rather than diagnosing. Upstream Clang
# really does this on deeply nested expressions; the fake keeps the test fast
# and independent of which Clang is installed.
fake="$run/crashing-clang"
printf '#!/bin/sh\nkill -SEGV $$\n' > "$fake"
chmod +x "$fake"

source="$run/ordinary.cpp"
printf 'int main() { return 0; }\n' > "$source"

set +e
"$CPPL" --cppl-clang="$fake" "$source" -o "$run/out" > "$run/stdout" 2> "$run/stderr"
status=$?
set -e

# Not 139: `cppl` did not crash, so it must not claim to have.
if [ "$status" -eq 139 ]; then
    echo "cppl returned the subprocess's 128+signal status as its own" >&2
    exit 1
fi
[ "$status" -eq 1 ]

# The reason is stated, naming the signal, rather than left to be inferred.
grep -q "terminated by signal" "$run/stderr"

echo "a signalled C++ compiler is reported with a reason, not impersonated"
