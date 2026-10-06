#!/usr/bin/env bash
# SPEC: TUBOUND-005
# TRUST.md TCB-REPRO-001, TCB-REPRO-002, TCB-VERSION-004
#
# `cppl --cppl-version` is the record a release is traced by. It must be
# complete, it must be a function of the source, the toolchain and the
# arguments alone, and it must say so when it cannot be complete:
#
#   - every field is present once and says something;
#   - the source identity is the commit and tree state Git reports for the
#     tree the compiler was built from;
#   - two runs print the same record, and the record names no path or time,
#     so a clean checkout of one commit reproduces it;
#   - the target and language mode are what the Clang driver selects for the
#     arguments given, and the Clang that analyses and the driver that compiles
#     are one release;
#   - a driver that cannot be asked makes the record fail rather than print a
#     plausible guess.
set -euo pipefail

CPPL="$1"
SOURCE="$2"
WORK="$3"
CLANG="$4"
CMAKE="$5"

fail() {
    echo "$1" >&2
    exit 1
}

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/release-metadata.XXXXXX")

"$CPPL" --cppl-version > "$run/first" 2> "$run/first.err"
"$CPPL" --cppl-version > "$run/second" 2> "$run/second.err"
cmp -s "$run/first" "$run/second" || fail "two runs of --cppl-version printed different records"
[ ! -s "$run/first.err" ] || {
    cat "$run/first.err" >&2
    fail "--cppl-version warned about the toolchain it was built with"
}

value() {
    sed -n "s/^$1: *//p" "$run/first"
}

for field in 'C++L compiler' 'Source revision' 'Source tag' 'Source tree' 'Built with' 'Verification semantics' \
    'Verifier-semantics digest' 'Kernel version' 'Formal core version' 'Interface format version' 'Clang' \
    'Clang driver' 'Target' 'C++ mode' 'C++ standard library'; do
    [ "$(grep -c "^$field: " "$run/first")" -eq 1 ] || fail "the record does not state '$field' exactly once"
    [ -n "$(value "$field")" ] || fail "the record states nothing for '$field'"
done

# Nothing that differs between two builds of one commit, or between two hosts.
# A URL in a version string is not a path; a slash that opens a word is.
if grep -Eq '(^|[[:space:](=])/[^[:space:]]' "$run/first"; then
    grep -E '(^|[[:space:](=])/[^[:space:]]' "$run/first" >&2
    fail "the record names an absolute path"
fi
for private in "$SOURCE" "$WORK" "${HOME:-/nonexistent-home}"; do
    if [ -n "$private" ] && grep -Fq "$private" "$run/first"; then
        fail "the record names '$private', which is where or by whom it was built, not what"
    fi
done
if grep -Eq '[0-9]{4}-[0-9]{2}-[0-9]{2}|[0-9]{1,2}:[0-9]{2}:[0-9]{2}' "$run/first"; then
    fail "the record carries a date or a time"
fi

# The source identity is what Git says of the tree the compiler was built from.
revision=$(value 'Source revision')
tree=$(value 'Source tree')
if command -v git > /dev/null 2>&1 && git -C "$SOURCE" rev-parse --verify HEAD > /dev/null 2>&1; then
    [ "$revision" = "$(git -C "$SOURCE" rev-parse --verify HEAD)" ] ||
        fail "the record names revision $revision, and the tree is at $(git -C "$SOURCE" rev-parse HEAD); rebuild"
    if [ -z "$(git --no-optional-locks -C "$SOURCE" status --porcelain --untracked-files=no)" ]; then
        expected_tree=clean
    else
        expected_tree=modified
    fi
    [ "$tree" = "$expected_tree" ] || fail "the record says the tree is $tree, and Git says it is $expected_tree"
    tag=$(git -C "$SOURCE" describe --tags --exact-match HEAD 2> /dev/null || echo none)
    [ "$(value 'Source tag')" = "$tag" ] || fail "the record names tag '$(value 'Source tag')', and Git names '$tag'"
else
    [ "$revision" = unknown ] || fail "the record names revision $revision for a tree Git does not know"
    [ "$tree" = unknown ] || fail "the record names a tree state Git could not have reported"
fi
case "$revision" in
    unknown | [0-9a-f]*) ;;
    *) fail "the source revision '$revision' is neither a commit nor unknown" ;;
esac

# SPEC: TUBOUND-013 -- the compiler carries the verification semantics of the
# sources it was built from: the version they declare and the digest their
# semantic sources give (TRUST.md TCB-XTU-008).
"$CMAKE" "-DROOT=$SOURCE" "-DDIGEST_FILE=$run/digest" -P "$SOURCE/cmake/ComputeVerifierSemantics.cmake" > /dev/null
[ "$(value 'Verifier-semantics digest')" = "$(tr -d '[:space:]' < "$run/digest")" ] ||
    fail "the compiler reports verifier-semantics digest $(value 'Verifier-semantics digest'), and its sources give $(cat "$run/digest"); rebuild"
declared=$(sed -n 's/^inline constexpr std::string_view kVerificationSemanticsVersion = "\(.*\)";$/\1/p' \
    "$SOURCE/compiler/obligations/include/cppl/obligations/interface.hpp")
[ -n "$declared" ] || fail "the sources declare no verification semantics version"
[ "$(value 'Verification semantics')" = "$declared" ] ||
    fail "the compiler reports verification semantics '$(value 'Verification semantics')', and its sources declare '$declared'"

# The Clang that analyses and the driver that compiles are one release.
[ "$(value 'Clang')" = "$(value 'Clang driver')" ] ||
    fail "the build pairs libclang '$(value 'Clang')' with the driver '$(value 'Clang driver')'"
[ "$(value 'Target')" = "$("$CLANG" -print-target-triple)" ] ||
    fail "the record names target '$(value 'Target')', and the driver selects '$("$CLANG" -print-target-triple)'"
case "$(value 'C++ mode')" in
    *"(the Clang driver's default)") ;;
    *) fail "with no -std the record does not say the language mode is the driver's default" ;;
esac

# The language mode is the one the arguments select.
for standard in c++17 c++20 c++23 gnu++20; do
    mode=$("$CPPL" --cppl-version "-std=$standard" | sed -n 's/^C++ mode: *//p')
    [ "$mode" = "$standard" ] || fail "-std=$standard is reported as C++ mode '$mode'"
done

# A driver that cannot be asked is not reported as having answered.
if "$CPPL" --cppl-version "--cppl-clang=$run/no-such-clang" > "$run/missing" 2> "$run/missing.err"; then
    fail "--cppl-version succeeded without a Clang driver to ask"
fi
for field in 'Clang driver' 'Target' 'C++ mode'; do
    reported=$(grep "^$field: " "$run/missing") && grep -q ': *unavailable: ' <<< "$reported" ||
        fail "without a driver, '$field' is not reported unavailable"
done
grep -q 'could not be asked' "$run/missing.err" || fail "without a driver, --cppl-version gives no reason"

echo 'the release record is complete, reproducible, free of paths and times, and fails closed without a driver'
