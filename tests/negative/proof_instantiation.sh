#!/usr/bin/env bash
# SPEC: ERASE-019, ERASE-005, ERASE-006, LAW-008
# TRUST.md TCB-SOURCE-010; AGENTS.md 11, 16, 23
#
# Proof-only text instantiates nothing the program run does not.
#
# A class template's specialization is instantiated once, at its first use, and
# what it declares and computes then stays for the rest of the unit. Proof-only
# text is C++ in the program verified alone, so a use only it makes would change
# what ordinary C++ after it means there and nowhere in the program run: a friend
# the instantiation defines makes a detection idiom answer otherwise
# (fixtures/include/stateful_friend.hpp), and a contract is verified for a
# program that is not the one run. Each refused fixture below would verify
# `which()` returning 1 while the program prints 0, from proof-only text of a
# different kind; each is refused where that text uses the template, with that
# diagnostic alone, and leaves nothing behind. The accepted half uses standard
# templates, templates of data alone and the program's own classes in every
# proof-only position, and is verified and run with `E::A` 0.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"

# shellcheck source=../support/parallel.sh
source "$(dirname "$0")/../support/parallel.sh"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/proof-instantiation.XXXXXX")
cases_begin "$run/cases"

# refuse <name> <diagnostic>: refused with that diagnostic and no other error,
# with no executable and no runtime projection written.
refuse() {
    local name="$1" message="$2"
    local status=0
    "$CPPL" -std=c++20 "$FIXTURES/negative/$name.cpp" -o "$run/$name" \
        "--cppl-emit-projection=$run/$name.runtime.ii" > "$run/$name.log" 2>&1 || status=$?
    if [ "$status" -eq 0 ]; then
        echo "$name was accepted" >&2
        cat "$run/$name.log" >&2
        exit 1
    fi
    if [ -e "$run/$name" ] || [ -e "$run/$name.runtime.ii" ]; then
        echo "a program was written for $name" >&2
        exit 1
    fi
    if ! grep -Fq "$message" "$run/$name.log"; then
        echo "$name was not refused because: $message" >&2
        cat "$run/$name.log" >&2
        exit 1
    fi
    if [ "$(grep -c 'error \[' "$run/$name.log")" -ne 1 ]; then
        echo "$name was refused with more than its one diagnostic:" >&2
        cat "$run/$name.log" >&2
        exit 1
    fi
}

because="where the program run does not, so the program verified would not be the program run (SPEC.md ERASE-019)"
set_zero="error [cpp-semantic]: proof-only text may instantiate 'Set<0>', a specialization of 'Set', a template \
declared outside the standard library"

# The Law's parameter, the audit's case: projected as a definition taking Set<0>
# by value.
case_run refuse proof_instantiation_law "proof_instantiation_law.cpp:11:48: $set_zero, $because"
# A contract reading a parameter the function holds by reference.
case_run refuse proof_instantiation_contract "proof_instantiation_contract.cpp:11:16: $set_zero, $because"
# A contract on a declaration, which completes no parameter type: the clause's
# probe, a definition, would complete the one it takes by value.
case_run refuse proof_instantiation_declaration "proof_instantiation_declaration.cpp:11:67: $set_zero, $because"
# A ghost declaration, which the program run never has (SPEC.md ERASE-011).
case_run refuse proof_instantiation_ghost "proof_instantiation_ghost.cpp:12:65: $set_zero, $because"
# The argument a proof's step instantiates another proof at.
case_run refuse proof_instantiation_proof_step "proof_instantiation_proof_step.cpp:25:68: $set_zero, $because"
# A refinement predicate; the program run keeps the alias alone.
case_run refuse proof_instantiation_refinement "proof_instantiation_refinement.cpp:9:74: $set_zero, $because"
# The payload a case split on a runtime path binds.
case_run refuse proof_instantiation_case_split "proof_instantiation_case_split.cpp:15:33: $set_zero, $because"
# A loop invariant.
case_run refuse proof_instantiation_invariant "proof_instantiation_invariant.cpp:15:71: $set_zero, $because"
# A standard template is trusted, and what it is instantiated at is not, though
# only an alias of the program's names it.
case_run refuse proof_instantiation_standard_argument \
    "proof_instantiation_standard_argument.cpp:14:48: $set_zero, $because"
# A template of data alone is inert, and what it is instantiated at is not.
case_run refuse proof_instantiation_carried \
    "proof_instantiation_carried.cpp:17:79: $set_zero, through 'Crate<Set<0>>', $because"
# A template of data alone, named where its arguments are deduced, which
# deduces every guide written for it.
case_run refuse proof_instantiation_deduction_guide \
    "proof_instantiation_deduction_guide.cpp:30:13: error [cpp-semantic]: proof-only text may instantiate 'Crate', \
a template declared outside the standard library, $because"
# A verified template's clause, instantiated with each specialization of it.
case_run refuse proof_instantiation_template_clause "proof_instantiation_template_clause.cpp:10:16: $set_zero, $because"
# A function template overload resolution only considers, and deduces.
case_run refuse proof_instantiation_overload_candidate \
    "proof_instantiation_overload_candidate.cpp:29:13: error [cpp-semantic]: proof-only text may instantiate 'twice', \
a template declared outside the standard library that overload resolution for 'twice' considers, $because"
# A built-in operator on an enumeration, chosen over an operator template that
# overload resolution deduces too.
case_run refuse proof_instantiation_operator_candidate \
    "proof_instantiation_operator_candidate.cpp:28:14: error [cpp-semantic]: proof-only text may instantiate \
'operator==', a template declared outside the standard library that overload resolution for 'operator==' \
considers, $because"
# A member template of a class that is no template.
case_run refuse proof_instantiation_member_template \
    "proof_instantiation_member_template.cpp:18:42: error [cpp-semantic]: proof-only text may instantiate \
'Holder::plus', a member template of 'Holder', $because"
# A variable template's specialization, in the Law's parameter type.
case_run refuse proof_instantiation_variable_template \
    "proof_instantiation_variable_template.cpp:13:59: error [cpp-semantic]: proof-only text may instantiate 'small', \
a variable template or its specialization, declared outside the standard library, $because"
# An alias template whose substitution evaluates an expression, though the
# type it forms is a fundamental one.
case_run refuse proof_instantiation_alias_template \
    "proof_instantiation_alias_template.cpp:14:40: error [cpp-semantic]: proof-only text may instantiate 'Word', \
a template declared outside the standard library, $because"

# The accepted half: the same stateful templates, and proof-only text of every
# kind using only what cannot change them. Verified as run: E::A is 0.
accept() {
    if ! "$CPPL" -std=c++20 "$FIXTURES/proof_instantiation.cpp" -o "$run/accepted" --cppl-trust-report \
        > "$run/accepted.log" 2>&1; then
        echo "the accepted half was refused" >&2
        cat "$run/accepted.log" >&2
        exit 1
    fi
    local printed
    printed=$("$run/accepted")
    if [ "$printed" != "0 3 0 0" ]; then
        echo "the accepted half printed '$printed', not '0 3 0 0'" >&2
        exit 1
    fi
    grep -Eq '^Laws proven: +1$' "$run/accepted.log"
    grep -Eq '^Proof declarations proven: +1$' "$run/accepted.log"
    grep -Eq '^Function contracts proven: +3$' "$run/accepted.log"
    grep -Eq '^Unresolved obligations: +0$' "$run/accepted.log"
}
case_run accept

cases_end
echo "proof-only text instantiated nothing the program run does not"
