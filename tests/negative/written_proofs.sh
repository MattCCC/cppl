#!/usr/bin/env bash
# Written proofs that must be refused.
#
# A written proof is not a way to be believed. Every one of these fails, none
# produces a program, and each says precisely what was wrong and where.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/proofs.XXXXXX")

# Compiles a fixture that must fail, and leaves its output in "$run/<name>.log".
refuse() {
    local name="$1"
    local status=0
    "$CPPL" -std=c++17 "$FIXTURES/$name.cpp" -o "$run/$name" > "$run/$name.log" 2>&1 || status=$?

    if [ "$status" -eq 0 ]; then
        echo "$name was accepted" >&2
        cat "$run/$name.log" >&2
        exit 1
    fi
    if [ -e "$run/$name" ]; then
        echo "a program was produced for $name" >&2
        exit 1
    fi
    if grep -q "PROVEN" "$run/$name.log"; then
        echo "$name was described as proven" >&2
        exit 1
    fi
    if ! grep -q "error" "$run/$name.log"; then
        echo "$name did not fail as an error" >&2
        exit 1
    fi
    # The failure points back at the developer's own source.
    grep -q "$name.cpp:" "$run/$name.log"
}

# refl where the two sides are not definitionally equal. Only the kernel can
# say so, and it is the kernel that says so.
refuse false_proof
grep -q "kernel-rejection" "$run/false_proof.log"
grep -q "add_one_changes_nothing_holds" "$run/false_proof.log"
grep -q "definitionally equal" "$run/false_proof.log"

# exact with evidence for another proposition, apply whose conclusion has the
# wrong shape, and a proof name that was never declared.
refuse rejected_proofs
grep -q "'exact' requires evidence for the goal itself" "$run/rejected_proofs.log"
grep -q "cannot be applied to the goal" "$run/rejected_proofs.log"
grep -q "no proof named 'no_such_proof' is declared" "$run/rejected_proofs.log"
grep -q "quantifies over 1" "$run/rejected_proofs.log"

# A law whose written proof was refused is never quietly closed by the
# compiler's own strategy instead.
if grep -q "identity_returns_input_again.*is not proven" "$run/rejected_proofs.log"; then
    echo "a law with a refused proof was re-attempted by the compiler" >&2
    exit 1
fi

# apply whose conclusion has exactly the goal's shape but does not close it:
# nothing before the kernel can tell, and the subgoal it left is named.
refuse unsound_apply
grep -q "kernel-rejection" "$run/unsound_apply.log"
grep -q "apply left the subgoal" "$run/unsound_apply.log"

# A tactic this implementation does not have is refused, not ignored.
refuse unsupported_proof_body
grep -q "does not begin a proof statement" "$run/unsupported_proof_body.log"

# Circular evidence is not evidence.
refuse cyclic_proofs
grep -q "depends on itself" "$run/cyclic_proofs.log"

echo "written proofs fail closed: false, mismatched, unknown, unsupported and circular"
