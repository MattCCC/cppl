#!/usr/bin/env bash
# SPEC: ARITH-014
# TRUST.md TCB-CLANG-006
#
# A unit is verified under exactly the options its program is compiled with.
# The Clang driver preprocesses the unit and compiles its runtime program, and
# libclang makes the analysis the proof is about, from the same command line
# with its inputs and outputs taken out. Each case below is one where the
# analysis was once made under other options than the object: an option the
# command line holds that the analysis was not given, so a contract about plain
# `char` was proven of one signedness and the program compiled with the other.
# Each refusal has an accepted twin, and each accepted program is run, so what
# was proven is what it prints.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"
CLANG="$4"
TARGET="$FIXTURES/target"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/analysis-options.XXXXXX")
cd "$run"

fail() {
    echo "$1" >&2
    exit 1
}

# accept <name> <fixture> <cppl arguments...>: the program verifies, its
# contract is reported proven, and it prints what its contract states.
accept() {
    local name="$1" fixture="$2"
    shift 2
    if ! "$CPPL" -std=c++17 "$TARGET/$fixture.cpp" "$@" -o "$name" --cppl-trust-report \
        > "$name.out" 2> "$name.err"; then
        tail -30 "$name.err" >&2
        fail "refused what should verify: $name"
    fi
    grep -Eq 'Function contracts proven: *1$' "$name.out" || fail "$name did not report its contract proven"
    local printed
    printed=$("./$name")
    [ "$printed" = "${proven[$fixture]}" ] ||
        fail "$name was proven to print ${proven[$fixture]}, and it prints $printed"
}

# refuse <name> <pattern> <fixture> <cppl arguments...>: the compile fails,
# produces no program, reports the reason matching <pattern>, and reports
# nothing proven.
refuse() {
    local name="$1" pattern="$2" fixture="$3"
    shift 3
    if "$CPPL" -std=c++17 "$TARGET/$fixture.cpp" "$@" -o "$name" --cppl-trust-report \
        > "$name.out" 2> "$name.err"; then
        fail "accepted what must be refused: $name"
    fi
    [ ! -e "$name" ] || fail "$name produced a program"
    if ! grep -Eq -- "$pattern" "$name.err"; then
        tail -30 "$name.err" >&2
        fail "$name was refused, but not because: $pattern"
    fi
    if grep -Eq 'Function contracts proven: *[1-9]' "$name.out"; then
        fail "$name reported a contract proven"
    fi
}

contract_false="verified function '[a-z_]+' does not satisfy its contract"

# What each program is proven to print.
declare -A proven=([signed_char_program]=1 [unsigned_char_program]=0)

# The host's predefined macros, read whole: a grep that stops at its first
# match would leave the driver writing to a closed pipe.
host_macros=$("$CLANG" -x c++ -E -dM - < /dev/null)
if grep -q '^#define __CHAR_UNSIGNED__ ' <<< "$host_macros"; then
    host=unsigned_char_program
    other=signed_char_program
    other_char=-fsigned-char
else
    host=signed_char_program
    other=unsigned_char_program
    other_char=-funsigned-char
fi

# The option itself, where nothing stands in its way.
accept host_plain "$host"
refuse host_with_flag "$contract_false" "$host" "$other_char"
accept other_with_flag "$other" "$other_char"

# ---- an option's value is its option's ------------------------------------
#
# Clang's driver reads the argument after each of these options as its value.
# Read as an input instead, it was taken out of the analysis's command line,
# and there the option took the next argument, the `char` option, as its value:
# the analysis read the host's `char` and the program was compiled with the
# other. Options of each kind the driver's table has: `Separate` ones in each
# spelling, a `JoinedOrSeparate` one written alone, one that names what it
# applies to joined to it and reads the next argument too (`JoinedAndSeparate`),
# and one that reads three values (`MultiArg`).
mkdir include
values=(
    "--include-directory include"
    "-Xanalyzer value"
    "-dependency-file deps.d"
    "-imultilib multilib"
    "--prefix include"
    "-dumpdir dump"
    "-iapinotes-modules include"
    "-Xopenmp-target=host value"
    "-sectcreate segment section include"
)
for value in "${values[@]}"; do
    read -ra option <<< "$value"
    name=$(printf '%s' "${option[0]}" | tr -c 'a-zA-Z0-9\n' '_')
    refuse "value$name" "$contract_false" "$host" "${option[@]}" "$other_char" -w
    accept "value_other$name" "$other" "${option[@]}" "$other_char" -w
done

# A value that reads as an option the analysis's command line leaves out: `-o`
# and `-c` here are what `-Xanalyzer` hands on, and the `char` option after
# them is the program's. Taken for the options they read as, they were left out
# with the argument after them, or left the `-Xanalyzer` before them to take it.
refuse value_reads_as_output "$contract_false" "$host" -Xanalyzer -o "$other_char" -w
accept value_reads_as_output_other "$other" -Xanalyzer -o "$other_char" -w
refuse value_reads_as_compile "$contract_false" "$host" -Xanalyzer -c "$other_char" -w
accept value_reads_as_compile_other "$other" -Xanalyzer -c "$other_char" -w

# A value that reads as an option only preprocessing reads: the runtime
# program, already preprocessed, is compiled without those, and `-I` taken for
# one was left out with the `char` option after it, so the program was compiled
# with the host's `char` and the analysis with the other.
refuse value_reads_as_include "$contract_false" "$host" -Xanalyzer -I "$other_char" -w
accept value_reads_as_include_other "$other" -Xanalyzer -I "$other_char" -w

# ---- a configuration file named in two arguments ----------------------------
#
# `--config <file>` is read as `--config=<file>` is. The name was once taken for
# an input, and `--config` took the next argument as the file to read.
printf -- '%s\n' "$other_char" > other.cfg
refuse config_separate "$contract_false" "$host" --config ./other.cfg
accept config_separate_other "$other" --config ./other.cfg
accept config_joined_other "$other" --config=./other.cfg

# A name with no directory is looked for where the driver looks, among them
# the directory the driver itself is in. libclang, which runs as a `clang` of
# its own, does not look there: the analysis reads the file the driver found,
# by the path it found it at, and not the name again.
mkdir driver
ln -s "$CLANG" driver/clang++
printf -- '%s\n' "$other_char" > driver/beside.cfg
beside=("--cppl-clang=$run/driver/clang++" -no-canonical-prefixes)
"$run/driver/clang++" "${beside[@]:1}" --config beside.cfg --version | grep -q "^Configuration file: .*driver/beside.cfg$" ||
    fail "the driver does not read beside.cfg from its own directory, so this case shows nothing"
# The driver's own directory holds no builtin header under -no-canonical-prefixes,
# so the unit is one that includes none, compiled to an object.
if [ "$host" = signed_char_program ]; then
    beside_host=plain_char
    beside_other=plain_char_nonnegative
else
    beside_host=plain_char_nonnegative
    beside_other=plain_char
fi
for spelling in separate joined; do
    if [ "$spelling" = separate ]; then
        named=(--config beside.cfg)
    else
        named=(--config=beside.cfg)
    fi
    if "$CPPL" "${beside[@]}" "${named[@]}" -std=c++17 -c "$TARGET/$beside_other.cpp" -o "beside_$spelling.o" \
        --cppl-trust-report > "beside_$spelling.out" 2> "beside_$spelling.err"; then
        grep -Eq 'Function contracts proven: *1$' "beside_$spelling.out" ||
            fail "beside_$spelling did not report its contract proven"
    else
        tail -30 "beside_$spelling.err" >&2
        fail "refused what should verify: beside_$spelling"
    fi
    if "$CPPL" "${beside[@]}" "${named[@]}" -std=c++17 -c "$TARGET/$beside_host.cpp" -o "beside_host_$spelling.o" \
        --cppl-trust-report > "beside_host_$spelling.out" 2> "beside_host_$spelling.err"; then
        fail "accepted what must be refused: beside_host_$spelling"
    fi
    grep -Eq -- "$contract_false" "beside_host_$spelling.err" || {
        tail -30 "beside_host_$spelling.err" >&2
        fail "beside_host_$spelling was refused, but not because its contract is false"
    }
done

# ---- arguments the driver's own program edits ---------------------------------
#
# Before it reads its arguments, the driver's own program replaces each one
# beginning with `@` with the arguments the response file it names holds,
# wherever it stands, an option's value too, and applies the edits
# CCC_OVERRIDE_OPTIONS names. libclang does neither, so a unit with C++L is
# refused under either, whatever the file or the variable says. The twin of each
# is the option written on the command line, `other_with_flag` above. A unit
# without C++L is compiled as clang++ compiles it.
sed -e 's/^verified //' -e '/^ *ensures /d' "$TARGET/$other.cpp" > ordinary.cpp
if grep -Eq '^verified|^ *ensures' ordinary.cpp; then
    fail "ordinary.cpp still holds C++L"
fi

# same_as_clang <name> <cppl and clang++ arguments...>: both build the program,
# and it prints the same, which is what `char` of the other signedness prints.
same_as_clang() {
    local name="$1"
    shift
    "$CPPL" ordinary.cpp "$@" -o "$name" > "$name.out" 2> "$name.err" || {
        tail -30 "$name.err" >&2
        fail "$name: a unit without C++L was not compiled as clang++ compiles it"
    }
    "$CLANG" ordinary.cpp "$@" -o "$name.clang" 2> "$name.clang.err" || fail "$name: clang++ did not build it"
    [ "$("./$name")" = "$("./$name.clang")" ] || fail "$name prints otherwise than clang++'s program"
    [ "$("./$name")" = "${proven[$other]}" ] || fail "$name was not compiled with $other_char"
}

printf -- '%s\n' "$other_char" > other.rsp
response="is not verified: its command line reads arguments from the response file '@other.rsp'"
refuse response_host "$response" "$host" @other.rsp
refuse response_other "$response" "$other" @other.rsp
refuse response_as_value "$response" "$other" -Xanalyzer @other.rsp -w
same_as_clang response_ordinary @other.rsp

override="is not verified: the environment variable CCC_OVERRIDE_OPTIONS is set"
export CCC_OVERRIDE_OPTIONS="+$other_char"
refuse override_host "$override" "$host"
refuse override_other "$override" "$other"
same_as_clang override_ordinary
export CCC_OVERRIDE_OPTIONS=""
refuse override_empty "$override" "$other" "$other_char"
unset CCC_OVERRIDE_OPTIONS

# CL and _CL_ edit the arguments too, in clang-cl mode alone. There the driver
# writes what it preprocesses to its standard output, not to the file it is
# asked to, so a unit is never read in that mode, let alone analysed: it is
# refused whatever the variables say. A clang-cl mode cppl could read would
# have to refuse them as it refuses CCC_OVERRIDE_OPTIONS.
# The unit is named relative to the run directory: clang-cl reads an absolute
# path such as /workspace/... or /Users/... as one of its own options.
ln -s "$CLANG" clang-cl
cp "$TARGET/$beside_host.cpp" cl_mode.cpp
if CL=/J _CL_=/J "$CPPL" "--cppl-clang=$run/clang-cl" -c cl_mode.cpp -o cl_mode.o \
    --cppl-trust-report > cl_mode.out 2> cl_mode.err; then
    fail "a unit was verified in clang-cl mode"
fi
[ ! -e cl_mode.o ] || fail "a unit compiled in clang-cl mode produced an object"
grep -q "could not read the preprocessed form" cl_mode.err || {
    tail -30 cl_mode.err >&2
    fail "a unit in clang-cl mode was refused, but not because it could not be read"
}

# Every other variable the driver reads is read by the driver library libclang
# runs in this process, under the same environment, or changes only what the
# driver preprocesses, which the analysis and the program are both made from:
# a header found through CPATH or CPLUS_INCLUDE_PATH is the program's and the
# analysis's alike.
mkdir -p search/one search/two
printf '#define CHOSEN 1\n' > search/one/chosen.h
printf '#define CHOSEN 2\n' > search/two/chosen.h
printf '#include <chosen.h>\n#include <cstdio>\n\nverified int chosen()\n    ensures (result == 1)\n{\n    return CHOSEN;\n}\n\nint main()\n{\n    std::printf("%%d\\n", chosen());\n    return 0;\n}\n' > searched.cpp
for variable in CPATH CPLUS_INCLUDE_PATH; do
    env "$variable=$run/search/one" "$CPPL" -std=c++17 searched.cpp -o "searched_$variable" --cppl-trust-report \
        > "searched_$variable.out" 2> "searched_$variable.err" || {
        tail -30 "searched_$variable.err" >&2
        fail "refused what should verify: searched_$variable"
    }
    [ "$("./searched_$variable")" = 1 ] || fail "searched_$variable does not print what was proven"
    if env "$variable=$run/search/two" "$CPPL" -std=c++17 searched.cpp -o "searched_other_$variable" \
        > "searched_other_$variable.out" 2> "searched_other_$variable.err"; then
        fail "accepted what must be refused: searched_other_$variable"
    fi
    grep -Eq -- "$contract_false" "searched_other_$variable.err" ||
        fail "searched_other_$variable was refused, but not because its contract is false"
done

echo "every unit is verified under the options its program is compiled with, or refused"
