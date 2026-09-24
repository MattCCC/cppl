#!/usr/bin/env bash
# SPEC: ERASE-006, ERASE-008, ERASE-009, ERASE-011, ERASE-014
# TRUST.md TCB-ERASE-006; AGENTS.md 23
#
# A refused unit leaves no runtime program behind.
#
# Erasure produces a program only from a unit that verified. Whatever stage
# refuses a unit -- recognition, projection, Clang, elaboration, the kernel --
# must stop before code generation: no executable, and no runtime projection a
# build could pick up and compile instead. Specified constructs this
# implementation does not implement are refused one by one for the reason they
# fail, then every refused fixture is swept for the same property.
set -euo pipefail

CPPL="$1"
FIXTURES="$2/negative"
WORK="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/erasure-refusals.XXXXXX")

# refuse <name> [expected diagnostic]...
refuse() {
    local name="$1"
    shift
    local status=0
    "$CPPL" -std=c++17 "$FIXTURES/$name.cpp" -o "$run/$name" "--cppl-emit-projection=$run/$name.runtime.ii" \
        > "$run/$name.log" 2>&1 || status=$?

    if [ "$status" -eq 0 ]; then
        echo "$name was accepted" >&2
        cat "$run/$name.log" >&2
        exit 1
    fi
    if [ -e "$run/$name" ]; then
        echo "a program was produced for $name" >&2
        exit 1
    fi
    if [ -e "$run/$name.runtime.ii" ]; then
        echo "a runtime program was written for $name" >&2
        exit 1
    fi
    local message
    for message in "$@"; do
        if ! grep -Fq "$message" "$run/$name.log"; then
            echo "$name was not refused because: $message" >&2
            cat "$run/$name.log" >&2
            exit 1
        fi
    done
}

# Ghost state, `old` and induction are specified and not implemented, and loop
# clauses written outside their place are not recognized. Each keeps its C++
# reading or is refused by name; none is erased as though checked. A contract
# resting on what an unsafe block did is refused like any unproven one, and the
# block is not erased around it.
refuse unsupported_ghost_state "unsupported_ghost_state.cpp:9:5: error [cpp-semantic]: unknown type name 'ghost'"
refuse unsafe_false_postcondition \
    "unsafe_false_postcondition.cpp:12:12: error [kernel-rejection]: return path 'bumped path 1' does not satisfy its contract"
refuse unsupported_old_value "unsupported_old_value.cpp:8:19: error [cpp-semantic]: use of undeclared identifier 'old'"
refuse unsupported_induction "unsupported_induction.cpp:15:5: error [proof-failure]: proof" "uses induction over 'x'"
refuse misplaced_loop_clauses \
    "misplaced_loop_clauses.cpp:11:9: error [cpp-semantic]: use of undeclared identifier 'invariant'" \
    "misplaced_loop_clauses.cpp:22:20: error [cpp-semantic]: expected ';' after do/while statement"

# Every refused fixture, whichever stage refuses it.
swept=0
for fixture in "$FIXTURES"/*.cpp; do
    refuse "$(basename "$fixture" .cpp)"
    swept=$((swept + 1))
done
test "$swept" -ge 40

echo "$swept refused units left no program and no runtime projection behind"
