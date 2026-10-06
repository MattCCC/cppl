#!/usr/bin/env bash
# `cppl` takes every input and option a Clang build takes, and takes it as
# Clang does, whether or not a unit holds C++L (AGENTS.md 2; ARCHITECTURE.md 80):
#
#   - an object, an archive or another source after a verified unit is read as
#     what it is, not as the verified unit's preprocessed text;
#   - a source of any extension Clang reads as C++ (`.CC`, `.cp`, `.CPP`, ...)
#     or C (`.c`) is compiled beside the others, as clang++ compiles it, and a
#     C++ one is verified when it holds C++L;
#   - the options only preprocessing reads (`-I`, `-isystem`, `-include`,
#     `-MD`, ...) are not reported unused for a verified unit, so `-Werror`
#     builds; an option Clang would report is still reported;
#   - `-MD` and `-MMD` write the dependency file where Clang would, naming the
#     target Clang would name, and `-c` without `-o` writes the object Clang
#     would name.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"
CLANG="$4"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/driver-inputs.XXXXXX")
cd "$run"

fail() {
    echo "$1" >&2
    exit 1
}

# succeeds <name> <command...>: the command succeeds; its output is kept.
succeeds() {
    local name="$1"
    shift
    if ! "$@" > "$name.out" 2> "$name.err"; then
        tail -20 "$name.err" >&2
        fail "$name failed"
    fi
}

# runs <name> <expected output>: the program built as <name> prints it.
runs() {
    local output
    output=$("./$1")
    [ "$output" = "$2" ] || fail "$1 printed '$output', not '$2'"
}

# ---- the sources --------------------------------------------------------------

mkdir include system
printf '#include <cstdio>\nint helper(int);\n\nverified unsigned id(unsigned x)\n    ensures (result == x)\n{\n    return x;\n}\n\nint main() { std::printf("%%u %%d\\n", id(3u), helper(4)); }\n' > verified.cpp
printf '#include <cstdio>\nint helper(int);\nint main() { std::printf("%%d\\n", helper(4)); }\n' > plain.cpp
printf 'int helper(int x) { return x + 1; }\n' > helper.cpp
# What `sizeof('"'"'a'"'"')` is tells C from C++: clang++ reads a `.c` input as C++.
printf 'int helper(int x) { return static_cast<int>(sizeof('"'"'a'"'"')) * 100 + x; }\n' > helper_c.c
printf 'struct Config { int level = 3; };\n' > include/config.h
printf '#define LIMIT 2u\n' > system/limit.h
printf '#include <cstdio>\n#include <limit.h>\n\nverified unsigned id(unsigned x)\n    ensures (result == x)\n{\n    return x;\n}\n\nint main() { Config c; std::printf("%%d %%u\\n", c.level, id(LIMIT)); }\n' > configured.cpp
# An ordinary source that reads only through the preprocessing options.
printf '#include <limit.h>\nint helper(int x) { Config c; return x + c.level + static_cast<int>(LIMIT) - 4; }\n' \
    > configured_helper.cpp

succeeds helper_object "$CLANG" -c helper.cpp -o helper.o
succeeds helper_archive ar rcs libhelper.a helper.o

# ---- link inputs after a verified unit ----------------------------------------

succeeds verified_object "$CPPL" verified.cpp helper.o -o verified_object
runs verified_object "3 5"
succeeds verified_archive "$CPPL" verified.cpp libhelper.a -o verified_archive
runs verified_archive "3 5"
succeeds verified_source "$CPPL" verified.cpp helper.cpp -o verified_source
runs verified_source "3 5"

# ---- every C++ extension, and C ----------------------------------------------

for extension in CC cp CPP CXX C++ cxx; do
    cp helper.cpp "helper_$extension.$extension"
    succeeds "plain_$extension" "$CPPL" plain.cpp "helper_$extension.$extension" -o "plain_$extension"
    runs "plain_$extension" "5"
    succeeds "verified_$extension" "$CPPL" verified.cpp "helper_$extension.$extension" -o "verified_$extension"
    runs "verified_$extension" "3 5"
done
succeeds plain_c_clang "$CLANG" plain.cpp helper_c.c -o plain_c_clang
succeeds plain_c "$CPPL" plain.cpp helper_c.c -o plain_c
[ "$(./plain_c)" = "$(./plain_c_clang)" ] || fail "a .c input is not compiled as clang++ compiles it"
succeeds verified_c "$CPPL" verified.cpp helper_c.c -o verified_c
[ "$(./verified_c)" = "3 $(./plain_c_clang)" ] || fail "a .c input beside a verified unit is not compiled as clang++ compiles it"

# A C++ source of any extension is verified: a false contract in one is refused.
printf 'verified unsigned wrong(unsigned x)\n    ensures (result == x + 1u)\n{\n    return x;\n}\n' > wrong.CC
if "$CPPL" -c wrong.CC -o wrong.o > wrong.out 2> wrong.err; then
    fail "a false contract in a .CC source was not refused"
fi
grep -q "does not satisfy its contract" wrong.err || fail "a .CC source was refused, but not for its contract"

# ---- options only preprocessing reads, under -Werror -------------------------

succeeds werror_compile "$CPPL" -Werror -I include -iquote include -isystem system -idirafter system \
    -include config.h -imacros limit.h -MD -MP -c configured.cpp -o werror_compile.o
succeeds werror_link "$CPPL" -Werror -I include -isystem system -include config.h -MMD configured.cpp \
    -o werror_link
runs werror_link "3 2"
# Beside an object, nothing else reads them either.
succeeds werror_object "$CPPL" -Werror -I include -isystem system -include config.h verified.cpp helper.o \
    -o werror_object
runs werror_object "3 5"
# A search path a link reads too is left out only where nothing is linked.
mkdir frameworks
succeeds werror_frameworks "$CPPL" -Werror -F frameworks -c verified.cpp -o werror_frameworks.o
# Beside a source still preprocessed, they are read for it as Clang reads them.
succeeds werror_mixed "$CPPL" -Werror -I include -isystem system -include config.h -MD verified.cpp \
    configured_helper.cpp -o werror_mixed
runs werror_mixed "3 5"
# What Clang would report unused is still reported: nothing is linked by -c.
if "$CPPL" -Werror -c verified.cpp -o unused.o -lm > unused.out 2> unused.err; then
    fail "a linker input under -c was not reported unused"
fi
grep -q "'linker' input unused" unused.err || fail "a linker input under -c was refused for another reason"

# ---- dependency files and default outputs ------------------------------------

mkdir out deps
succeeds depfile "$CPPL" -I include -isystem system -include config.h -MD -c configured.cpp -o out/configured.o
[ -f out/configured.d ] || fail "-MD without -MF wrote no dependency file beside the object"
rule=$(head -1 out/configured.d) && grep -q '^out/configured.o: configured.cpp ' <<< "$rule" ||
    fail "-MD names another target than the object: $(head -1 out/configured.d)"
grep -q 'include/config.h' out/configured.d || fail "-MD does not list the header -include reads"
grep -q 'system/limit.h' out/configured.d || fail "-MD does not list a system header"

succeeds depfile_named "$CPPL" -I include -isystem system -include config.h -MMD -MF deps/named.d -c configured.cpp \
    -o out/named.o
rule=$(head -1 deps/named.d) && grep -q '^out/named.o: configured.cpp ' <<< "$rule" ||
    fail "-MF without -MT names another target than the object: $(head -1 deps/named.d)"
if grep -q 'system/limit.h' deps/named.d; then
    fail "-MMD lists a system header"
fi

succeeds depfile_target "$CPPL" -I include -isystem system -include config.h -MD -MF deps/target.d -MT custom \
    -c configured.cpp -o out/target.o
rule=$(head -1 deps/target.d) && grep -q '^custom: configured.cpp ' <<< "$rule" || fail "-MT is not the target named"

# `-c` with neither `-o` nor `-MF`: each output is named after the source.
mkdir bare
(cd bare && "$CPPL" -I ../include -isystem ../system -include config.h -MD -c ../configured.cpp > ../bare.out 2> ../bare.err) ||
    fail "a compile with no -o failed"
[ -f bare/configured.o ] || fail "-c without -o wrote no configured.o: $(ls bare)"
[ -f bare/configured.d ] || fail "-MD without -o or -MF wrote no configured.d: $(ls bare)"
rule=$(head -1 bare/configured.d) && grep -q '^configured.o: ' <<< "$rule" || fail "-MD without -o names another target"
[ "$(ls bare | wc -l)" -eq 2 ] || fail "a compile with no -o left more than its object and dependency file: $(ls bare)"

echo "cppl takes objects, archives, every source extension and the preprocessing and dependency options as Clang does"
