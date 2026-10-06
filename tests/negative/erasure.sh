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

# shellcheck source=../support/parallel.sh
source "$(dirname "$0")/../support/parallel.sh"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/erasure-refusals.XXXXXX")

# Each refusal is a case of its own, in files of its own, so they run side by
# side (support/parallel.sh). The sweep below refuses the fixtures named here a
# second time, so it writes into a directory of its own.
cases_begin "$run/cases"
mkdir -p "$run/sweep"

# refuse <name> [expected diagnostic]...
refuse() {
    refuse_in "$run" "$@"
}

# refuse_in <directory> <name> [expected diagnostic]...: refuse, writing what
# the compiler produces into <directory>.
refuse_in() {
    local run="$1" name="$2"
    shift 2
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

# `old` is specified and not implemented, induction over a signed integer has
# no principle, and loop clauses written outside their place are not
# recognized. Each keeps its C++ reading or is refused by name; none is erased
# as though checked. Ghost state that code
# would give runtime storage, and a contract resting on what an unsafe block
# did, are refused, and nothing is erased around them.
case_run refuse ghost_runtime_storage \
    "ghost_runtime_storage.cpp:9:30: error [cppl-syntax]: ghost 'seen' is used by code that runs"
case_run refuse unsafe_false_postcondition \
    "unsafe_false_postcondition.cpp:12:12: error [kernel-rejection]: return path 'bumped path 1' does not satisfy its contract"
old_refused="error [unsupported-semantics]: 'old(...)' in a postcondition denotes the entry value of its operand"
case_run refuse unsupported_old_value "unsupported_old_value.cpp:8:19: $old_refused"
# A visible function named `old` does not make the form a call to it, wherever
# the postcondition writes it (SPEC.md 3.1, 11.4).
case_run refuse old_shadowed_by_function "old_shadowed_by_function.cpp:15:19: $old_refused"
case_run refuse old_forms_in_postconditions \
    "old_forms_in_postconditions.cpp:16:34: $old_refused" \
    "old_forms_in_postconditions.cpp:22:29: $old_refused" \
    "old_forms_in_postconditions.cpp:28:56: $old_refused" \
    "old_forms_in_postconditions.cpp:34:39: $old_refused" \
    "old_forms_in_postconditions.cpp:40:24: $old_refused"
case_run refuse induction_signed_subject \
    "induction_signed_subject.cpp:16:5: error [proof-failure]: induction over 'x' has no principle"
case_run refuse misplaced_loop_clauses \
    "misplaced_loop_clauses.cpp:11:9: error [cpp-semantic]: use of undeclared identifier 'invariant'" \
    "misplaced_loop_clauses.cpp:22:20: error [cpp-semantic]: expected ';' after do/while statement"

# SPEC: LAW-008, ERASE-005, ERASE-006
# A Law has no runtime callable identity, and the analysed program gives it
# none either: ordinary lookup, overload resolution and a detection idiom
# resolve as the program does, so the specialization verified is the one the
# program runs. Each claim below holds only of the specialization a visible Law
# would have selected, and each goal names the one the program instantiates.
case_run refuse law_answers_ordinary_lookup \
    "law_answers_ordinary_lookup.cpp:27:12: error [proof-failure]: verified function 'resolved' does not satisfy its contract" \
    "law_answers_ordinary_lookup.cpp:27:12: note: goal: forall u32. Eq<u32>(7:u32, 1:u32)" \
    "law_answers_ordinary_lookup.cpp:43:12: error [proof-failure]: verified function 'geo::hidden' does not satisfy its contract" \
    "law_answers_ordinary_lookup.cpp:43:12: note: goal: forall u32. Eq<u32>(7:u32, 1:u32)" \
    "law_answers_ordinary_lookup.cpp:64:12: error [proof-failure]: verified function 'detected' does not satisfy its contract" \
    "law_answers_ordinary_lookup.cpp:64:12: note: goal: forall u32. Eq<u32>(2:u32, 1:u32)"
case_run refuse law_answers_explicit_instantiation \
    "law_answers_explicit_instantiation.cpp:24:12: error [proof-failure]: verified function 'g' does not satisfy its contract" \
    "law_answers_explicit_instantiation.cpp:24:12: note: goal: forall u32. Eq<u32>(7:u32, 1:u32)"

# SPEC: ERASE-017
# A directive inside an expression or a statement C++L states is refused at its
# own line, and one kept between contract clauses stands where C++ admits none,
# which Clang refuses as it refuses the program erased by hand.
case_run refuse directive_inside_expression \
    "directive_inside_expression.cpp:10:1: error [unsupported-semantics]: the directive '#pragma pack(push, 1)' stands inside an expression or a statement C++L states, where it cannot be kept" \
    "directive_inside_expression.cpp:17:1: error [unsupported-semantics]: the directive '#pragma pack(push, 1)' stands inside an expression or a statement C++L states, where it cannot be kept" \
    "directive_inside_expression.cpp:26:1: error [unsupported-semantics]: the directive '#pragma pack(push, 1)' stands inside an expression or a statement C++L states, where it cannot be kept"
case_run refuse directive_between_contract_clauses \
    "directive_between_contract_clauses.cpp:11:9: error [cpp-semantic]: expected function body after function declarator"

# refuse_alone <name> <diagnostic>: refused with that diagnostic and no other
# error, none internal, and no name only the compiler generates.
refuse_alone() {
    local name="$1"
    refuse "$@"
    if [ "$(grep -c 'error \[' "$run/$name.log")" -ne 1 ] || grep -q 'error \[internal\]' "$run/$name.log" ||
        grep -q '__cppl_' "$run/$name.log"; then
        echo "$name was refused with more than its one diagnostic:" >&2
        cat "$run/$name.log" >&2
        exit 1
    fi
}

# SPEC: RUNTIMECHECK-019
# A validation in a ghost declaration would never run; it is refused by name,
# once, and erasure is never asked to keep runtime code in a span it blanks.
case_run refuse_alone validation_in_ghost \
    "validation_in_ghost.cpp:13:21: error [unsupported-semantics]: a validation expression is runtime code, and a ghost declaration never runs"

# SPEC: ERASE-018
# A lowering that cannot keep the code after it on its line in its column is
# refused by name, rather than compiled with that code moved.
case_run refuse_alone refinement_lowering_moves_columns \
    "refinement_lowering_moves_columns.cpp:8:1: error [unsupported-semantics]: refinement type 'Positive' lowers to more C++ than its declaration takes on its line, so the code after it there would move"

# Every refused fixture, whichever stage refuses it.
swept=0
for fixture in "$FIXTURES"/*.cpp; do
    case_run refuse_in "$run/sweep" "$(basename "$fixture" .cpp)"
    swept=$((swept + 1))
done
cases_end
test "$swept" -ge 40

echo "$swept refused units left no program and no runtime projection behind"
