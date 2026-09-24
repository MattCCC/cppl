#!/usr/bin/env bash
# What ghost state cannot do (SPEC.md 25, GHOST-001, GHOST-002, ERASE-011).
#
# Ghost state leaves the program before it runs. Nothing that runs may depend on
# it, its initializer may have no effect and call only what the formal core
# defines, and it exists only as a local of a verified body. Each program here
# breaks one of those, and each must be refused where the fault stands, for that
# reason, with no program produced. What ghost state may do is in
# `fixtures/ghost_state.cpp`, driven by `e2e/ghost_state.sh`.
set -euo pipefail

CPPL="$1"
FIXTURES="$2/negative"
WORK="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/ghost_state.XXXXXX")

refuse() {
    local name="$1"
    shift
    local status=0
    "$CPPL" -std=c++20 --cppl-trust-report "$FIXTURES/$name.cpp" -o "$run/$name" > "$run/$name.log" 2>&1 ||
        status=$?

    if [ "$status" -eq 0 ]; then
        echo "$name was accepted" >&2
        cat "$run/$name.log" >&2
        exit 1
    fi
    if [ -e "$run/$name" ]; then
        echo "a program was produced for $name" >&2
        exit 1
    fi
    if grep -qE "PROVEN|C\+\+L Trust Report" "$run/$name.log"; then
        echo "$name was described as proven" >&2
        cat "$run/$name.log" >&2
        exit 1
    fi
    local diagnostic
    for diagnostic in "$@"; do
        if ! grep -qF "$diagnostic" "$run/$name.log"; then
            echo "$name did not fail for the stated reason: $diagnostic" >&2
            cat "$run/$name.log" >&2
            exit 1
        fi
    done
}

leak() {
    local name="$1" at="$2" ghost="$3"
    refuse "$name" "$name.cpp:$at: error [cppl-syntax]: ghost '$ghost' is used by code that runs"
}

# SPEC: GHOST-001, GHOST-002
# Runtime influence: a returned value, a branch, an index, an argument, an
# object's initializer, a write, a loop bound, a lambda capture, and a runtime
# local a ghost shadows. Each use is refused where it stands.
leak ghost_returned 8:12 g
leak ghost_branch 7:9 g
leak ghost_index 9:19 g
leak ghost_argument 13:21 g
leak ghost_initializes_runtime 13:15 g
leak ghost_written 7:5 g
leak ghost_controls_mutation 9:31 g
leak ghost_captured 8:10 g
leak ghost_shadows_runtime 11:16 y

# SPEC: ERASE-011
# Ghost state has no runtime identity, so its address cannot be taken.
leak ghost_runtime_storage 9:30 seen

# SPEC: GHOST-002
# Nor can it reach code that runs through a name spelled like one C++L
# generates, which would be read as generated.
refuse ghost_through_reserved_name \
    "ghost_through_reserved_name.cpp:9:14: error [cppl-syntax]: '__cppl_copy' begins with '__cppl_', which names only what C++L generates"

# SPEC: GHOST-001
# An initializer never runs, so it may have no effect and may call only what the
# formal core defines: not an ordinary function, not a verified one.
refuse ghost_effectful_initializer \
    "ghost_effectful_initializer.cpp:9:24: error [cppl-syntax]: the initializer of ghost 'g' has an increment or decrement"
refuse ghost_impure_call \
    "ghost_impure_call.cpp:14:24: error [cppl-syntax]: the initializer of ghost 'g' calls 'read_sensor', which is not pure"
refuse ghost_verified_call \
    "ghost_verified_call.cpp:16:24: error [cppl-syntax]: the initializer of ghost 'g' calls 'next', which is not pure"

# SPEC: GHOST-001
# Ghost state is an integer or a Boolean local with a value: no class with a
# destructor, no reference, no static, no declaration without a value.
refuse ghost_class_type \
    "ghost_class_type.cpp:16:17: error [cppl-syntax]: ghost 'n' has type 'Noisy'; ghost state is an integer or a Boolean value"
refuse ghost_reference \
    "ghost_reference.cpp:7:21: error [cppl-syntax]: ghost 'r' has type 'unsigned int &'"
refuse ghost_static "ghost_static.cpp:7:27: error [cppl-syntax]: ghost 'g' is not a local with automatic storage"
refuse ghost_without_value "ghost_without_value.cpp:7:20: error [cppl-syntax]: ghost 'g' is declared without a value"

# SPEC: GHOST-001
# Where a ghost declaration may stand: in a verified body, directly in a block,
# outside every unsafe block, naming a type.
refuse ghost_global \
    "ghost_global.cpp:4:1: error [cppl-syntax]: ghost state is declared only as a local of a verified body"
refuse ghost_outside_verified \
    "ghost_outside_verified.cpp:5:5: error [unsupported-semantics]: ghost state outside a verified body would not be checked"
refuse ghost_in_unsafe_block \
    "ghost_in_unsafe_block.cpp:8:9: error [unsupported-semantics]: ghost state inside an unsafe block would not be checked"
refuse ghost_unbraced "ghost_unbraced.cpp:8:9: error [cppl-syntax]: a ghost declaration stands directly in a block"
refuse ghost_untyped "ghost_untyped.cpp:8:5: error [cppl-syntax]: a ghost declaration names a type and a variable"

# SPEC: GHOST-002, REFINE-008
# Ghost state proves nothing about what runs, and one of a refinement type owes
# its predicate like any other value.
refuse ghost_false_postcondition \
    "ghost_false_postcondition.cpp:8:12: error [proof-failure]: verified function 'claims' does not satisfy its contract"
refuse ghost_refinement_owed \
    "ghost_refinement_owed.cpp:9:21: error [kernel-rejection]: this value is not shown to satisfy refinement type 'Small'"

echo 'no ghost state reaches the program, and none is declared where the language does not allow it'
