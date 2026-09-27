#!/usr/bin/env bash
# Comparisons shared by the erasure-equivalence suites. Sourced, never run.
#
# Two compilations are the same code when their assembly is the same. Assembly
# carries every instruction, datum, symbol, section and alignment the object
# will, so an extra check, a changed layout, a different mangled name or an
# added parameter all show up in it, including on a path no test input takes.
# It is text, so no object-file tool is needed to read it on any platform.
#
# The one line removed is the `.file` directive ELF assembly opens with. It
# names the source file, and the two sides of a comparison are compiled from
# differently named files on purpose.

# assembly <output> <command> [args...]
#
# Runs a compiler command with `-S -o <output>.s`, then writes <output> as that
# assembly without its `.file` directive. Fails when the compiler fails or
# produces nothing, so an empty comparison can never pass for an equal one.
assembly() {
    local output="$1"
    shift
    "$@" -S -o "$output.s"
    grep -Ev '^[[:space:]]*\.file[[:space:]]' "$output.s" > "$output" || true
    if [ ! -s "$output" ]; then
        echo "no assembly was produced for: $*" >&2
        return 1
    fi
}

# same_code <what> <left> <right>
#
# Fails, showing where they part, when two assembly files differ.
same_code() {
    local what="$1" left="$2" right="$3"
    if ! cmp -s "$left" "$right"; then
        echo "$what: the two compilations differ" >&2
        diff "$left" "$right" | head -n 40 >&2 || true
        return 1
    fi
}

# tokens <output> <clang> <standard> <source>
#
# Writes <output> as the program text Clang reads from <source> once it is
# preprocessed: one token a line, its kind and its spelling, with no position,
# spacing, comment, line splice or line marker. Two programs with the same
# tokens are the same text as far as any C++ compiler is concerned; that is the
# runtime-text comparison, and it sees what code comparison cannot, such as a
# declaration nothing uses. A source that is not already preprocessed (`.ii`,
# as a runtime program is) is preprocessed first, so both sides of a comparison
# are read the same way. Fails when Clang fails or reads nothing.
tokens() {
    local output="$1" clang="$2" standard="$3" source="$4" read="$4"
    case "$source" in
        *.ii) ;;
        *)
            "$clang" "-std=$standard" -E "$source" -o "$output.ii"
            read="$output.ii"
            ;;
    esac
    "$clang" "-std=$standard" -fsyntax-only -Xclang -dump-tokens "$read" 2> "$output.dump"
    # Each line is the kind and the spelling, a tab, then flags and location.
    cut -f 1 "$output.dump" > "$output"
    if [ ! -s "$output" ]; then
        echo "no tokens were read from $source" >&2
        return 1
    fi
}

# same_text <what> <left> <right>
#
# Fails, showing where they part, when two token lists differ.
same_text() {
    local what="$1" left="$2" right="$3"
    if ! cmp -s "$left" "$right"; then
        echo "$what: the two programs are different text" >&2
        diff "$left" "$right" | head -n 40 >&2 || true
        return 1
    fi
}
