#!/usr/bin/env bash
# The shipped executables carry the exploit mitigations cmake/Hardening.cmake
# asks for (docs/CI.md, "Hardening").
#
# A flag that is set is not a mitigation that is present. A target that stops
# linking cppl_project_options, a linker that drops an option, or a toolchain
# default that changes all lose one silently, so the linked files themselves
# are read.
#
#   hardening.sh <llvm-readobj> <llvm-nm> <executable>...
set -euo pipefail

readobj="$1"
nm="$2"
shift 2

if [ "$#" -eq 0 ]; then
    echo "hardening.sh: no executables to check" >&2
    exit 2
fi

failures=0

missing() {
    echo "$1: $2" >&2
    failures=$((failures + 1))
}

check_elf() {
    local binary="$1" headers stack_flags
    headers=$("$readobj" --elf-output-style=GNU --file-headers --program-headers --dynamic-table "$binary")

    grep -Eq '^[[:space:]]*Type:[[:space:]]+DYN[[:space:]]' <<<"$headers" ||
        missing "$binary" "not a position-independent executable"
    grep -Eq '^[[:space:]]*GNU_RELRO[[:space:]]' <<<"$headers" ||
        missing "$binary" "no RELRO segment"
    grep -Eq '\(FLAGS\).*BIND_NOW|\(FLAGS_1\).*[[:space:]]NOW([[:space:]]|$)' <<<"$headers" ||
        missing "$binary" "relocations are not bound at load (no BIND_NOW)"

    # An ELF file without PT_GNU_STACK gets an executable stack on some
    # architectures, so its absence fails as well.
    stack_flags=$(awk '$1 == "GNU_STACK" { print $7 }' <<<"$headers")
    if [ -z "$stack_flags" ] || [[ "$stack_flags" == *E* ]]; then
        missing "$binary" "stack is executable (GNU_STACK flags '${stack_flags}')"
    fi

    "$nm" --dynamic --undefined-only "$binary" | grep -Eq '(^|[[:space:]])__stack_chk_fail(@|$)' ||
        missing "$binary" "no stack protector (__stack_chk_fail is never called)"
}

check_macho() {
    local binary="$1"

    "$readobj" --file-headers "$binary" | grep -q 'MH_PIE' ||
        missing "$binary" "not a position-independent executable (no MH_PIE)"

    "$nm" --undefined-only "$binary" | grep -Eq '(^|[[:space:]])___stack_chk_fail$' ||
        missing "$binary" "no stack protector (___stack_chk_fail is never called)"
}

for binary in "$@"; do
    format=$("$readobj" --file-headers "$binary" | awk -F': ' '/^Format:/ { print $2; exit }')
    case "$format" in
        elf*) check_elf "$binary" ;;
        Mach-O*) check_macho "$binary" ;;
        *) missing "$binary" "unrecognized executable format '${format}'" ;;
    esac
done

if [ "$failures" -ne 0 ]; then
    echo "hardening: ${failures} mitigation(s) missing" >&2
    exit 1
fi

echo "hardening: every mitigation present in $# executable(s)"
