#!/usr/bin/env bash
# SPEC: ARITH-014, ARITH-002
# TRUST.md TCB-CLANG-006
#
# A unit is verified for the target its program is compiled for, and for no
# other. The Clang driver compiles the object and decides its target, and it
# may decide from more than the arguments say: from the target prefix of its
# own name, or from a configuration file it reads. libclang, which resolves the
# C++ the proof is about, sees only the arguments. Each case below is one where
# an analysis left to the arguments would be made for another machine than the
# one the object is compiled for, and would prove a contract false of the
# program that runs.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"
CLANG="$4"
TARGET="$FIXTURES/target"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/analysis-target.XXXXXX")
cd "$run"

fail() {
    echo "$1" >&2
    exit 1
}

# accept <name> <driver> <cppl arguments...>: the unit verifies, its contract
# is reported proven, and its object is produced.
accept() {
    local name="$1" driver="$2"
    shift 2
    if ! "$CPPL" --cppl-clang="$driver" -std=c++17 -c "$@" -o "$name.o" --cppl-trust-report \
        > "$name.out" 2> "$name.err"; then
        tail -30 "$name.err" >&2
        fail "refused what should verify: $name"
    fi
    [ -s "$name.o" ] || fail "$name produced no object"
    grep -Eq 'Function contracts proven: *1$' "$name.out" || fail "$name did not report its contract proven"
}

# refuse <name> <driver> <pattern> <cppl arguments...>: the compile fails,
# produces no object, reports the reason matching <pattern>, and reports
# nothing proven.
refuse() {
    local name="$1" driver="$2" pattern="$3"
    shift 3
    if "$CPPL" --cppl-clang="$driver" -std=c++17 -c "$@" -o "$name.o" --cppl-trust-report \
        > "$name.out" 2> "$name.err"; then
        fail "accepted what must be refused: $name"
    fi
    [ ! -e "$name.o" ] || fail "$name produced an object"
    if ! grep -Eq "$pattern" "$name.err"; then
        tail -30 "$name.err" >&2
        fail "$name was refused, but not because: $pattern"
    fi
    if grep -Eq 'Function contracts proven: *[1-9]' "$name.out"; then
        fail "$name reported a contract proven"
    fi
}

contract_false="verified function '[a-z_]+' does not satisfy its contract"

# Which fixture holds on the host, where `unsigned long` is 64 bits wide on
# LP64 systems and 32 bits on LLP64 ones.
if "$CLANG" -x c++ -E -dM - < /dev/null | grep -q '^#define __SIZEOF_LONG__ 8$'; then
    host_true=widened
    host_false=wrapped
else
    host_true=wrapped
    host_false=widened
fi

# The host itself: the analysis and the object are both the host's.
accept host_true "$CLANG" "$TARGET/$host_true.cpp"
refuse host_false "$CLANG" "$contract_false" "$TARGET/$host_false.cpp"

# ---- the target prefix of the driver's name ---------------------------------
#
# `i686-linux-gnu-clang++` compiles for i686 because of its name; no argument
# says so. Its `unsigned long` is 32 bits wide, so `widened` is false of the
# object it compiles and `wrapped` true, whatever the host.
ln -s "$CLANG" "$run/i686-linux-gnu-clang++"
prefixed="$run/i686-linux-gnu-clang++"
[ "$("$prefixed" -print-target-triple)" = "i686-unknown-linux-gnu" ] ||
    fail "the prefixed driver does not select i686, so this test shows nothing"

refuse prefix_widened "$prefixed" "$contract_false" "$TARGET/widened.cpp"
accept prefix_wrapped "$prefixed" "$TARGET/wrapped.cpp" --cppl-emit-interface=prefix_wrapped.cppli
# The interface records the target the unit was verified for.
grep -q '^target i686-unknown-linux-gnu$' prefix_wrapped.cppli ||
    fail "the interface of the prefixed unit does not record target i686"

# ---- a configuration file the driver reads ----------------------------------
#
# A `clang++` driver reads `clang++.cfg` from its user configuration directory;
# libclang, which runs as a `clang` of its own, would not. The file gives plain
# `char` the signedness the host does not give it, so the fixture that holds of
# the host's `char` is false of the object compiled with the file.
if "$CLANG" -x c++ -E -dM - < /dev/null | grep -q '^#define __CHAR_UNSIGNED__ '; then
    host_char=plain_char_nonnegative
    other_char_fixture=plain_char
    other_char=-fsigned-char
else
    host_char=plain_char
    other_char_fixture=plain_char_nonnegative
    other_char=-funsigned-char
fi
refuse char_other_host "$CLANG" "$contract_false" "$TARGET/$other_char_fixture.cpp"
accept char_other_flag "$CLANG" "$TARGET/$other_char_fixture.cpp" "$other_char"
mkdir "$run/config"
printf -- '%s\n' "$other_char" > "$run/config/clang++.cfg"
configured=(--config-user-dir="$run/config")
"$CLANG" "${configured[@]}" --version | grep -q "^Configuration file: .*clang++.cfg" ||
    fail "the driver does not read clang++.cfg from its user configuration directory, so this test shows nothing"

accept char_host "$CLANG" "$TARGET/$host_char.cpp"
refuse char_configured "$CLANG" "$contract_false" "$TARGET/$host_char.cpp" "${configured[@]}"

# The other way round: `clang.cfg` is what libclang would read by default, and
# the `clang++` driver does not read it. The object's `char` is the host's, so
# the contract holds of it, and the analysis must not read `char` as that file
# says.
mkdir "$run/libclang-config"
printf -- '%s\n' "$other_char" > "$run/libclang-config/clang.cfg"
if "$CLANG" --config-user-dir="$run/libclang-config" --version | grep -q "^Configuration file:"; then
    fail "the driver reads clang.cfg as well, so this case shows nothing"
fi
accept char_unconfigured "$CLANG" "$TARGET/$host_char.cpp" --config-user-dir="$run/libclang-config"

# ---- a driver whose compile job runs for another triple ---------------------
#
# Stands in for a driver whose compile job is for another triple than the one
# the analysis was given, however that comes about: it names its effective
# triple as i686's and otherwise is the real driver. What the analysis was made
# for is compared with that, so the unit is refused rather than proven for the
# host and compiled as though for i686.
lying="$run/lying-clang++"
printf '#!/bin/sh\nfor argument in "$@"; do\n    if [ "$argument" = -print-effective-triple ]; then\n        echo i686-unknown-linux-gnu\n        exit 0\n    fi\ndone\nexec "%s" "$@"\n' \
    "$CLANG" > "$lying"
chmod +x "$lying"
refuse lying_effective "$lying" \
    "its C\+\+ semantics were resolved for target '[^']+', and the Clang driver '[^']+' compiles the program for 'i686-unknown-linux-gnu'" \
    "$TARGET/$host_true.cpp"

# ---- more than one target ---------------------------------------------------
#
# Each `-arch` is a slice the driver compiles the program for, and one analysis
# is made for one target. Nothing in C++L refuses several: the driver refuses to
# preprocess under them, and a unit is read only once it is preprocessed. That
# is what this case keeps true (compiler/driver/src/target.hpp).
refuse two_architectures "$CLANG" "with multiple -arch options" \
    "$TARGET/$host_true.cpp" --target=x86_64-apple-macosx11 -arch x86_64 -arch arm64

echo "every unit is verified for the target its program is compiled for, or refused"
