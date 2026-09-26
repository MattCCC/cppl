#!/usr/bin/env bash
# SPEC: TUBOUND-005
# TRUST.md TCB-XTU-008, TCB-VERSION-003
#
# The verifier semantics digest (cmake/ComputeVerifierSemantics.cmake) and the
# classification it rests on (cmake/VerifierSemanticsSources.cmake).
#
# The digest is what makes an interface of a verifier built from other semantic
# sources unusable even where the declared semantics version was not bumped. It
# is computed here over copies of the source tree, each changed in one thing:
# the same sources give the same digest wherever they are, however recently
# their files were touched and whatever line endings they have; a byte of a
# semantic source, or a file added to a semantic component, gives another; and a
# change to documentation, tests or a component classified as not semantic
# gives the same. Every source is classified, and an unclassified one is found.
#
# usage: verifier_semantics.sh <source root> <work directory> <cmake>
set -euo pipefail

ROOT="$1"
WORK="$2"
CMAKE="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/verifier-semantics.XXXXXX")

fail() {
    echo "$1" >&2
    exit 1
}

digest() {
    "$CMAKE" "-DROOT=$1" "-DDIGEST_FILE=$run/digest.txt" -P "$ROOT/cmake/ComputeVerifierSemantics.cmake"
    cat "$run/digest.txt"
}

classified() {
    "$CMAKE" "-DROOT=$1" -P "$ROOT/cmake/CheckVerifierSemantics.cmake" > "$run/classified.log" 2>&1
}

copies=0
# A fresh copy of every code root, with a documentation and a test file beside.
fresh() {
    copies=$((copies + 1))
    local copy="$run/copy-$copies"
    mkdir -p "$copy/docs" "$copy/tests/unit"
    cp -R "$ROOT/compiler" "$ROOT/kernel" "$ROOT/vir" "$ROOT/clang" "$ROOT/stdlib" "$ROOT/src" "$ROOT/tools" "$copy/"
    cp "$ROOT/docs/SPEC.md" "$copy/docs/"
    cp "$ROOT/tests/unit/interface_test.cpp" "$copy/tests/unit/"
    printf '%s\n' "$copy"
}

classified "$ROOT" || { cat "$run/classified.log" >&2; fail "the checkout's sources are not all classified"; }
original=$(digest "$ROOT")
[ "$(digest "$ROOT")" = "$original" ] || fail "two computations of one tree's digest differ"

# The same sources, anywhere, at any time, with any line endings.
copy=$(fresh)
[ "$(digest "$copy")" = "$original" ] || fail "a copy of the sources has another digest"
mv "$copy" "$run/moved"
[ "$(digest "$run/moved")" = "$original" ] || fail "the sources moved to another path have another digest"
find "$run/moved" -type f -exec touch -t 203001010000 {} +
[ "$(digest "$run/moved")" = "$original" ] || fail "the sources touched have another digest"
sed 's/$/\r/' "$run/moved/kernel/src/check.cpp" > "$run/crlf.cpp"
mv "$run/crlf.cpp" "$run/moved/kernel/src/check.cpp"
[ "$(digest "$run/moved")" = "$original" ] || fail "a source with CRLF line endings has another digest"

# What is not semantic changes nothing.
copy=$(fresh)
printf '\nAn edit.\n' >> "$copy/docs/SPEC.md"
printf '\n// An edit.\n' >> "$copy/tests/unit/interface_test.cpp"
printf '\n// An edit.\n' >> "$copy/compiler/erasure/src/erase.cpp"
printf '\n// An edit.\n' >> "$copy/compiler/formatter/src/format.cpp"
printf '\n// An edit.\n' >> "$copy/src/lsp/src/server.cpp"
printf '\n# An edit.\n' >> "$copy/tools/cppl-format/CMakeLists.txt"
[ "$(digest "$copy")" = "$original" ] || fail "an edit outside the semantic components changed the digest"

# What is semantic changes it: one byte of the kernel, a file added to a
# semantic component, a byte of the interface reader.
copy=$(fresh)
sed 's/return reject(/return  reject(/' "$copy/kernel/src/check.cpp" > "$run/edited.cpp"
cmp -s "$copy/kernel/src/check.cpp" "$run/edited.cpp" && fail "the kernel edit changed nothing"
mv "$run/edited.cpp" "$copy/kernel/src/check.cpp"
[ "$(digest "$copy")" != "$original" ] || fail "an edit of the kernel left the digest as it was"
copy=$(fresh)
printf '#pragma once\n' > "$copy/compiler/obligations/src/added.hpp"
[ "$(digest "$copy")" != "$original" ] || fail "a file added to a semantic component left the digest as it was"
copy=$(fresh)
printf '\n// An edit.\n' >> "$copy/compiler/artifact/src/interface.cpp"
[ "$(digest "$copy")" != "$original" ] || fail "an edit of the interface reader left the digest as it was"

# A source in no classified component is found, so a new component cannot fall
# outside the digest unnoticed.
copy=$(fresh)
mkdir -p "$copy/compiler/unclassified/src"
printf 'int unclassified() { return 0; }\n' > "$copy/compiler/unclassified/src/new.cpp"
if classified "$copy"; then
    fail "a source of an unclassified component was not found"
fi
grep -q "'compiler/unclassified/src/new.cpp' is in no classified component" "$run/classified.log" ||
    { cat "$run/classified.log" >&2; fail "the unclassified source was refused for another reason"; }

echo "the verifier semantics digest is $original; it follows every semantic source and nothing else"
