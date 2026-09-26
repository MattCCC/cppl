#!/usr/bin/env bash
# SPEC: TUBOUND-005
# TRUST.md TCB-XTU-008, TCB-VERSION-004
#
# A verification interface is bound to a declared verification-semantics
# version and to the digest of the verifier's semantics-bearing sources
# (cmake/VerifierSemantics.cmake). The declared version can be left unchanged
# by mistake; the digest cannot. This shows:
#
#   - the compiler under test carries the digest of the sources it was built
#     from, not a stale one;
#   - an edit to a semantics-bearing source changes the digest while the
#     declared version stays what it was, which is what makes an interface of
#     the old verifier refused (negative/cross_tu.sh, `other_verifier_artifact`);
#   - documentation, the formatter, timestamps and where the tree lives do not.
set -euo pipefail

CMAKE="$1"
SOURCE="$2"
CPPL="$3"
WORK="$4"

SCRIPT="$SOURCE/cmake/VerifierSemantics.cmake"

fail() {
    echo "$1" >&2
    exit 1
}

digest_of() {
    "$CMAKE" "-DCPPL_SOURCE_ROOT=$1" -DCPPL_PRINT=ON -P "$SCRIPT" 2>&1
}

embedded=$("$CPPL" --cppl-version | sed -n 's/^Verifier-semantics digest: *//p')
[ -n "$embedded" ] || fail "cppl --cppl-version names no verifier-semantics digest"
[ "$(digest_of "$SOURCE")" = "$embedded" ] ||
    fail "the compiler carries digest $embedded, not that of the sources it was built from"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/verifier-semantics.XXXXXX")
tree="$run/tree"
for file in kernel/include/cppl/kernel/version.hpp compiler/driver/include/cppl/driver/semantics.hpp \
    compiler/obligations/src/contracts.cpp clang/src/bridge.cpp vir/CMakeLists.txt \
    compiler/formatter/src/format.cpp; do
    mkdir -p "$tree/$(dirname "$file")"
    cp "$SOURCE/$file" "$tree/$file"
done
mkdir -p "$tree/docs"
printf 'notes\n' > "$tree/docs/SPEC.md"
printf 'notes\n' > "$tree/kernel/README.md"

base=$(digest_of "$tree")
[ "${#base}" -eq 64 ] || fail "the digest of a tree is not a SHA-256: '$base'"

declared=$(grep -o 'cppl-verification-semantics-[0-9.]*' "$tree/compiler/driver/include/cppl/driver/semantics.hpp")

# What is not verifier semantics leaves the digest alone.
printf 'more notes\n' >> "$tree/docs/SPEC.md"
printf 'more notes\n' >> "$tree/kernel/README.md"
printf '// layout only\n' >> "$tree/compiler/formatter/src/format.cpp"
touch -d '2001-01-01' "$tree/compiler/obligations/src/contracts.cpp"
[ "$(digest_of "$tree")" = "$base" ] || fail "documentation, the formatter or a timestamp changed the digest"
mv "$tree" "$run/moved"
tree="$run/moved"
[ "$(digest_of "$tree")" = "$base" ] || fail "where the tree lives changed the digest"

# One byte of a semantics-bearing source, anywhere in the set, changes it, and
# the declared version does not.
for file in compiler/obligations/src/contracts.cpp clang/src/bridge.cpp kernel/include/cppl/kernel/version.hpp \
    vir/CMakeLists.txt; do
    cp "$tree/$file" "$run/saved"
    printf ' ' >> "$tree/$file"
    changed=$(digest_of "$tree")
    [ "$changed" != "$base" ] || fail "an edit to $file did not change the verifier-semantics digest"
    grep -q "$declared" "$tree/compiler/driver/include/cppl/driver/semantics.hpp" ||
        fail "the declared verification-semantics version moved"
    cp "$run/saved" "$tree/$file"
    [ "$(digest_of "$tree")" = "$base" ] || fail "restoring $file did not restore the digest"
done

# A new source joins the set.
printf '// new\n' > "$tree/compiler/obligations/src/added.cpp"
[ "$(digest_of "$tree")" != "$base" ] || fail "a new semantics-bearing source did not change the digest"

echo "the verifier-semantics digest follows the verifier's sources, and nothing else"
