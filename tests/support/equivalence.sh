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
