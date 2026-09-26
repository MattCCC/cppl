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

# SPEC: CLASS-010, REFINE-060, REFINE-061, REFINE-062
# Every route a value takes into a refined member is charged where it enters:
# a write through a reference that may be the member, a call's effect. A
# version no route charged -- what an unsafe block left -- is charged where the
# function hands it back, and nowhere else.
refuse methods_refined_alias_write \
    "methods_refined_alias_write.cpp:16:13: error [kernel-rejection]: this value is not shown to satisfy refinement type 'Small'"
refuse methods_refined_call_effect \
    "methods_refined_call_effect.cpp:21:9: error [kernel-rejection]: this value is not shown to satisfy refinement type 'Small'"
refuse methods_refined_unsafe_return \
    "methods_refined_unsafe_return.cpp:19:6: error [kernel-rejection]: this value is not shown to satisfy refinement type 'Small'"
refuse methods_refined_unsafe_alias \
    "methods_refined_unsafe_alias.cpp:21:6: error [kernel-rejection]: this value is not shown to satisfy refinement type 'Small'"

# SPEC: CLASS-010, CLASS-011, VERIFIED-032
# A call writing through a reference that may be a member leaves the member
# unknown, as a direct write through it does.
refuse methods_call_through_alias_member \
    "methods_call_through_alias_member.cpp:23:9: error [kernel-rejection]: return path 'Pair::forget path 1' does not satisfy its contract"
# A place the callee only reads takes a post-call version where a write of the
# call may reach it: a `const` call through a reference that may be a member.
refuse methods_const_call_alias_param \
    "methods_const_call_alias_param.cpp:24:9: error [kernel-rejection]: return path 'Pair::relay path 1' does not satisfy its contract"

# SPEC: CLASS-009, CLASS-011
# An `&&` member function called on `std::move(t)` is called on `t`; one called
# on a copy of `t` is called on a temporary.
refuse methods_rvalue_stale \
    "methods_rvalue_stale.cpp:27:14: error [kernel-rejection]: return path 'stale path 1' does not satisfy its contract" \
    "methods_rvalue_stale.cpp:33:19: error [unsupported-semantics]: verified function 'copied' has a body this implementation cannot state as a value: the object of this call is not storage this implementation can name"

# SPEC: CLASS-011, VERIFIED-038
# What a pointer designates is reached only under the capability stated for it,
# and may be what a reference designates.
refuse methods_pointer_receiver_capability \
    "methods_pointer_receiver_capability.cpp:25:19: error [unsupported-semantics]: verified function 'read_unstated' has a body this implementation cannot state as a value: reading 'p' requires 'readable(p)'" \
    "methods_pointer_receiver_capability.cpp:32:19: error [unsupported-semantics]: verified function 'write_read_only' has a body this implementation cannot state as a value: writing through 'p' requires 'writable(p)'"
refuse methods_pointer_receiver_alias \
    "methods_pointer_receiver_alias.cpp:27:5: error [kernel-rejection]: return path 'stale path 2' does not satisfy its contract"

# SPEC: CLASS-010, CLASS-015
# Members whose storage may overlap another place, or change unseen, have no
# place, and a body naming one is refused where it does.
refuse methods_overlapping_members \
    "methods_overlapping_members.cpp:15:23: error [unsupported-semantics]: verified function 'Holder::through_reference' has a body this implementation cannot state as a value: 'other' is a reference member" \
    "methods_overlapping_members.cpp:28:23: error [unsupported-semantics]: verified function 'Flags::read_low' has a body this implementation cannot state as a value: 'low' is a bit-field" \
    "methods_overlapping_members.cpp:41:23: error [unsupported-semantics]: verified function 'Word::through_union' has a body this implementation cannot state as a value: 'alias' is a member of a union" \
    "methods_overlapping_members.cpp:56:23: error [unsupported-semantics]: verified function 'Split::through_anonymous_struct' has a body this implementation cannot state as a value: 'hi' is a member of an anonymous struct" \
    "methods_overlapping_members.cpp:67:19: error [unsupported-semantics]: verified function 'Device::clear' has a body this implementation cannot state as a value: 'status' is volatile"

# SPEC: CLASS-009, CLASS-015
refuse methods_volatile_function \
    "methods_volatile_function.cpp:9:23: error [unsupported-semantics]: verified member function 'Port::zero' is not verified by this implementation: it is volatile-qualified" \
    "methods_volatile_function.cpp:15:23: error [unsupported-semantics]: verified member function 'Port::read' is not verified by this implementation: it is volatile-qualified"

# SPEC: CLASS-012
refuse methods_static_pure_false \
    "methods_static_pure_false.cpp:16:12: error [proof-failure]: verified function 'wrong' does not satisfy its contract"

# SPEC: CLASS-011, CLASS-015
# Receivers for which this implementation forms no sound place.
refuse methods_receiver_forms \
    "methods_receiver_forms.cpp:24:19: error [unsupported-semantics]: verified function 'through_reference' has a body this implementation cannot state as a value: 'Cell::set' may write the object it is called on, which a parameter designates by reference" \
    "methods_receiver_forms.cpp:31:19: error [unsupported-semantics]: verified function 'at_index' has a body this implementation cannot state as a value: this subscript's array is not tracked storage of this body" \
    "methods_receiver_forms.cpp:39:19: error [unsupported-semantics]: verified function 'temporary' has a body this implementation cannot state as a value: the object of this call is not storage this implementation can name"

# SPEC: CLASS-015
refuse methods_unmodeled_receivers \
    "methods_unmodeled_receivers.cpp:9:23: error [unsupported-semantics]: verified member function 'Word::get' is not verified by this implementation: its implicit object is not one this implementation models: its class is a union" \
    "methods_unmodeled_receivers.cpp:23:23: error [unsupported-semantics]: verified member function 'Derived::get' is not verified by this implementation: its implicit object is not one this implementation models: its class has a base subobject"
refuse methods_unmodeled_uses \
    "methods_unmodeled_uses.cpp:20:23: error [unsupported-semantics]: verified function 'Cursor::same' has a body this implementation cannot state as a value: 'this' is not modeled" \
    "methods_unmodeled_uses.cpp:31:23: error [unsupported-semantics]: verified function 'Cursor::origin_value' has a body this implementation cannot state as a value: this member of the implicit object is not tracked storage" \
    "methods_unmodeled_uses.cpp:37:23: error [unsupported-semantics]: verified function 'Cursor::through' has a body this implementation cannot state as a value: a call through a pointer to member function is not modeled"

# SPEC: CLASS-011, TUBOUND-003, TUBOUND-001
# Across translation units a member function's contract is only what the unit
# defining it recorded: nothing more after a mutating call, its precondition
# owed at the object's places, and without the interface nothing at all. Each
# refused unit is a written-out twin of a function in
# `fixtures/methods_cross_tu/client.cpp`, which `e2e/verified_methods.sh` shows
# verifying.
cross="$run/cross_tu"
mkdir -p "$cross"
cp "$2"/methods_cross_tu/* "$cross/"
"$CPPL" -std=c++20 -c "$cross/counter.cpp" -o "$cross/counter.o" "--cppl-emit-interface=$cross/counter.cppli" \
    > "$cross/counter.log" 2>&1

# refuse_unit <name> <diagnostic> <cppl arguments...>
refuse_unit() {
    local name="$1" diagnostic="$2"
    shift 2
    local status=0
    "$CPPL" -std=c++20 -I "$cross" -c "$@" -o "$cross/$name.o" --cppl-trust-report > "$cross/$name.log" 2>&1 ||
        status=$?
    if [ "$status" -eq 0 ] || [ -e "$cross/$name.o" ]; then
        echo "$name was accepted" >&2
        cat "$cross/$name.log" >&2
        exit 1
    fi
    if grep -qE 'Function contracts proven: *[1-9]' "$cross/$name.log"; then
        echo "$name reported a contract proven" >&2
        exit 1
    fi
    if ! grep -qF "$diagnostic" "$cross/$name.log"; then
        echo "$name did not fail for the stated reason: $diagnostic" >&2
        cat "$cross/$name.log" >&2
        exit 1
    fi
}

imported="--cppl-import-interface=$cross/counter.cppli"
refuse_unit methods_cross_tu_stale \
    "methods_cross_tu_stale.cpp:14:12: error [kernel-rejection]: return path 'stale path 1' does not satisfy its contract" \
    "$FIXTURES/methods_cross_tu_stale.cpp" "$imported"
refuse_unit methods_cross_tu_precondition \
    "methods_cross_tu_precondition.cpp:12:12: error [kernel-rejection]: call-site precondition for 'room -> Counter::headroom' is not proven" \
    "$FIXTURES/methods_cross_tu_precondition.cpp" "$imported"
refuse_unit methods_cross_tu_no_interface \
    "verified function 'Counter::reset' is declared but not defined in this translation unit, and no imported verification interface records its contract" \
    "$cross/client.cpp"

echo 'member functions fail closed: false claims about objects, aliased and stale members, virtual dispatch and unmodeled objects'
