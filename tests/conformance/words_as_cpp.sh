#!/usr/bin/env bash
# SPEC: WORD-008, WORD-014, WORD-015, WORD-016, WORD-017, WORD-018
# Ordinary C++ spelled with C++L words, where C++ gives each word a meaning of
# its own, is compiled as the ordinary C++ it is.
#
# Each fixture in `fixtures/words_as_cpp/` is valid C++ that a recognizer
# reading the words too eagerly would change or refuse. For each one, in every
# supported standard, cppl must treat the unit as holding no C++L (it writes no
# runtime projection), print nothing, and build a program that prints and exits
# exactly as the one Clang builds from the same source. The fixtures make a
# misreading visible at run time where they can: a word read as a specifier
# would select another type or another function.
set -euo pipefail

CPPL="$1"
FIXTURES="$2/words_as_cpp"
WORK="$3"
CLANG="$4"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/words-as-cpp.XXXXXX")

compared=0
for fixture in "$FIXTURES"/*.cpp; do
    name=$(basename "$fixture" .cpp)
    for standard in c++17 c++20 c++23; do
        base="$run/$name-$standard"
        if ! "$CPPL" "-std=$standard" "$fixture" -o "$base.cppl" "--cppl-emit-projection=$base.projection" \
            2> "$base.cppl.err"; then
            echo "cppl refused $name ($standard), which is ordinary C++:" >&2
            cat "$base.cppl.err" >&2
            exit 1
        fi
        if [ -s "$base.cppl.err" ]; then
            echo "cppl reported something on $name ($standard), which is ordinary C++:" >&2
            cat "$base.cppl.err" >&2
            exit 1
        fi
        if [ -e "$base.projection" ]; then
            echo "cppl read $name ($standard) as C++L; it holds none" >&2
            exit 1
        fi
        "$CLANG" "-std=$standard" "$fixture" -o "$base.clang"

        cppl_status=0
        "$base.cppl" > "$base.cppl.out" || cppl_status=$?
        clang_status=0
        "$base.clang" > "$base.clang.out" || clang_status=$?
        if [ "$cppl_status" != "$clang_status" ] || ! cmp -s "$base.cppl.out" "$base.clang.out"; then
            echo "$name ($standard) behaves differently built by cppl and by Clang:" >&2
            echo "cppl (exit $cppl_status):" >&2
            cat "$base.cppl.out" >&2
            echo "clang (exit $clang_status):" >&2
            cat "$base.clang.out" >&2
            exit 1
        fi
        compared=$((compared + 1))
    done
done

if [ "$compared" -eq 0 ]; then
    echo "no fixture was compared" >&2
    exit 1
fi

# SPEC: WORD-018
# `-w` silences a C++L warning as it silences Clang's, and changes nothing else:
# the unit that warns without it builds the same program with it. An error is
# never silenced.
named="$2/case_split_as_cpp_name.cpp"
"$CPPL" -std=c++20 "$named" -o "$run/named" 2> "$run/named.err"
if ! grep -q "warning \[cppl-syntax\]: 'cases' is also a name" "$run/named.err"; then
    echo "the unit naming a type 'cases' no longer warns, so -w is not tested" >&2
    exit 1
fi
"$CPPL" -std=c++20 -w "$named" -o "$run/quiet" 2> "$run/quiet.err"
if [ -s "$run/quiet.err" ]; then
    echo "-w left a C++L warning:" >&2
    cat "$run/quiet.err" >&2
    exit 1
fi
if [ "$("$run/quiet")" != "$("$run/named")" ]; then
    echo "-w changed the program" >&2
    exit 1
fi
cat > "$run/refused.cpp" <<'CPP'
int f(int n) {
    while (n > 0) invariant (n >= 0) { --n; }
    return n;
}
CPP
if "$CPPL" -std=c++20 -w -c "$run/refused.cpp" -o "$run/refused.o" 2> "$run/refused.err"; then
    echo "-w let a refused unit compile" >&2
    exit 1
fi
if ! grep -q "error \[unsupported-semantics\]: a loop invariant outside a verified function" "$run/refused.err"; then
    echo "-w silenced a C++L error:" >&2
    cat "$run/refused.err" >&2
    exit 1
fi

echo "$compared builds of ordinary C++ spelled with C++L words behave as Clang's, and -w silences only warnings"
