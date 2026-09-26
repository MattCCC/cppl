#!/usr/bin/env bash
# Member functions that must be refused (SPEC.md CLASS-008 to CLASS-015).
#
# Every program here either claims something false of an object -- a fact that
# a write, an alias, a call or an unsafe block took away -- or asks for a
# member function this implementation does not verify. Each must fail for its
# own reason, stated where it was written, and none may be described as proven.
# The accepted half of each matched pair is in `fixtures/verified_methods.cpp`,
# driven by `e2e/verified_methods.sh`.
set -euo pipefail

CPPL="$1"
FIXTURES="$2/negative"
WORK="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/verified_methods.XXXXXX")

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

# SPEC: CLASS-008, CONTRACT-009
refuse methods_false_postcondition \
    "methods_false_postcondition.cpp:10:9: error [kernel-rejection]: return path 'Counter::get path 1' does not satisfy its contract"

# SPEC: CLASS-011
# A call on the object leaves only what the callee's contract states.
refuse methods_stale_member_after_call \
    "methods_stale_member_after_call.cpp:20:9: error [kernel-rejection]: return path 'Counter::after_reset path 1' does not satisfy its contract"
refuse methods_mutating_call_forgets \
    "methods_mutating_call_forgets.cpp:23:14: error [kernel-rejection]: return path 'mutating_call path 1' does not satisfy its contract"

# SPEC: CLASS-010, CONTRACT-010
# A reference parameter may designate the object or one of its members.
refuse methods_reference_alias_member \
    "methods_reference_alias_member.cpp:15:9: error [kernel-rejection]: return path 'Counter::through_alias path 1' does not satisfy its contract"
refuse methods_reference_object_stale \
    "methods_reference_object_stale.cpp:16:9: error [kernel-rejection]: return path 'Cursor::write_then_read path 1' does not satisfy its contract"
refuse methods_const_call_through_alias \
    "methods_const_call_through_alias.cpp:22:14: error [kernel-rejection]: return path 'const_call_through_alias path 1' does not satisfy its contract"

# SPEC: CONTRACT-010, CLASS-009, EDGECASE-058
# `const` does not stop a member function writing a `mutable` member.
refuse methods_mutable_member \
    "methods_mutable_member.cpp:24:18: error [kernel-rejection]: return path 'cached path 1' does not satisfy its contract"

# SPEC: CLASS-010, REFINEOBL-007
refuse methods_refined_member_write \
    "methods_refined_member_write.cpp:13:17: error [kernel-rejection]: this value is not shown to satisfy refinement type 'Small'"

# SPEC: TERMINATION-007
refuse methods_nondecreasing_recursion \
    "methods_nondecreasing_recursion.cpp:16:9: error [kernel-rejection]: recursive call 'Tank::drain -> Tank::drain' is not shown to be made at a smaller measure than its caller was entered with"

# SPEC: UNSAFE-003, UNSAFE-005
refuse methods_unsafe_block \
    "methods_unsafe_block.cpp:21:9: error [kernel-rejection]: return path 'Cursor::after_unsafe path 1' does not satisfy its contract" \
    "methods_unsafe_block.cpp:31:9: error [kernel-rejection]: return path 'Cursor::after_unnamed path 1' does not satisfy its contract"

# SPEC: CONTRACT-014, CLASS-006, CLASS-014
# Virtual dispatch stays closed: a verified virtual function, however it says it
# is one, and a call that dispatches or names a function with overrides.
refuse methods_virtual_function \
    "methods_virtual_function.cpp:10:5: error [unsupported-semantics]: a verified virtual function is not verified by this implementation" \
    "methods_virtual_function.cpp:20:5: error [unsupported-semantics]: a verified virtual function is not verified by this implementation" \
    "methods_virtual_function.cpp:28:5: error [unsupported-semantics]: a verified virtual function is not verified by this implementation"
refuse methods_implicit_override \
    "methods_implicit_override.cpp:16:23: error [unsupported-semantics]: verified member function 'Derived::get' is not verified by this implementation: it is virtual"
refuse methods_virtual_call \
    "methods_virtual_call.cpp:14:23: error [unsupported-semantics]: verified function 'Shape::dispatched' has a body this implementation cannot state as a value: a virtual call dispatches on the object's dynamic type" \
    "methods_virtual_call.cpp:20:23: error [unsupported-semantics]: verified function 'Shape::qualified' has a body this implementation cannot state as a value: a call to a virtual function names one whose overrides depend on the object's dynamic type"

# SPEC: CONTRACT-011, CONTRACT-012, CLASS-015
refuse methods_lifetime_members \
    "methods_lifetime_members.cpp:9:5: error [unsupported-semantics]: a verified constructor is not verified by this implementation" \
    "methods_lifetime_members.cpp:15:5: error [unsupported-semantics]: a verified destructor is not verified by this implementation"

# SPEC: TEMPLATE-001, CLASS-015
refuse methods_member_template \
    "methods_member_template.cpp:9:5: error [unsupported-semantics]: a verified member function template is not verified by this implementation"
refuse methods_class_template \
    "methods_class_template.cpp:9:23: error [unsupported-semantics]: verified member function 'Box::get' is not verified by this implementation: it is a member of a class template"

# SPEC: CONTRACT-005, CLASS-008
refuse methods_out_of_line_contract \
    "methods_out_of_line_contract.cpp:13:1: error [unsupported-semantics]: 'verified' is applied to a qualified declarator, which redeclares a function declared elsewhere"
refuse methods_local_class \
    "methods_local_class.cpp:9:9: error [unsupported-semantics]: 'verified' is applied outside namespace scope"

# SPEC: CLASS-015
refuse methods_unmodeled_receivers \
    "methods_unmodeled_receivers.cpp:9:23: error [unsupported-semantics]: verified member function 'Word::get' is not verified by this implementation: its implicit object is not one this implementation models: its class is a union" \
    "methods_unmodeled_receivers.cpp:23:23: error [unsupported-semantics]: verified member function 'Derived::get' is not verified by this implementation: its implicit object is not one this implementation models: its class has a base subobject"
refuse methods_unmodeled_uses \
    "methods_unmodeled_uses.cpp:20:23: error [unsupported-semantics]: verified function 'Cursor::same' has a body this implementation cannot state as a value: 'this' is not modeled" \
    "methods_unmodeled_uses.cpp:31:23: error [unsupported-semantics]: verified function 'Cursor::origin_value' has a body this implementation cannot state as a value: this member of the implicit object is not tracked storage" \
    "methods_unmodeled_uses.cpp:37:23: error [unsupported-semantics]: verified function 'Cursor::through' has a body this implementation cannot state as a value: a call through a pointer to member function is not modeled"

echo 'member functions fail closed: false claims about objects, aliased and stale members, virtual dispatch and unmodeled objects'
