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
# wrong shape, a proof name that was never declared, and a law that is named by
# a proof of one instance and by nothing that discharges the law itself.
refuse rejected_proofs
grep -q "does not prove what proof 'add_one_increments_by_exact' claims" \
    "$run/rejected_proofs.log"
grep -q "cannot be applied to what proof 'add_one_is_add_one_by_apply' claims" \
    "$run/rejected_proofs.log"
grep -q "no proof or assumed premise named 'no_such_proof' is in scope" \
    "$run/rejected_proofs.log"
grep -q "nothing establishes the law itself" "$run/rejected_proofs.log"

# Instantiation that does not hold up: not instantiated at all, the wrong type,
# more arguments than the evidence quantifies over, the wrong value, and one
# argument short.
refuse rejected_instantiations
grep -q "does not prove what proof 'at_41_uninstantiated_holds' claims" \
    "$run/rejected_instantiations.log"
grep -q "cannot be instantiated at a term of type 'i32'" "$run/rejected_instantiations.log"
grep -q "instantiated at more arguments than it quantifies over" \
    "$run/rejected_instantiations.log"
grep -q "does not prove what proof 'at_42_by_wrong_value_holds' claims" \
    "$run/rejected_instantiations.log"
grep -q "does not prove what proof 'at_seven_and_three_holds' claims" \
    "$run/rejected_instantiations.log"

# An instantiated conclusion of exactly the claim's shape that establishes a
# different equality. Only the kernel can tell, and it is the kernel that does.
grep -q "kernel-rejection" "$run/rejected_instantiations.log"
grep -q "instantiating that evidence establishes" "$run/rejected_instantiations.log"

# The argument a diagnostic blames is the one the author wrote: not the
# statement, not the declaration, but that term's own line and column.
source="$FIXTURES/rejected_instantiations.cpp"
line=$(grep -n "exact identity_general(41);" "$source" | cut -d: -f1)
column=$(awk -v n="$line" 'NR == n { print index($0, "(41)") + 1 }' "$source")
grep -q "rejected_instantiations.cpp:$line:$column:" "$run/rejected_instantiations.log"

# A law whose written proof was refused is never quietly closed by the
# compiler's own strategy instead.
if grep -q "identity_returns_input_again.*is not proven" "$run/rejected_proofs.log"; then
    echo "a law with a refused proof was re-attempted by the compiler" >&2
    exit 1
fi

# apply whose conclusion has exactly the goal's shape but does not close it:
# nothing before the kernel can tell, and the kernel says which terms differ.
refuse unsound_apply
grep -q "kernel-rejection" "$run/unsound_apply.log"
grep -q "definitionally equal" "$run/unsound_apply.log"

# Conditional laws that do not hold up. A premise is supposed, never granted:
# naming the wrong one, naming one where none is supposed, concluding something
# false underneath one, leaving one behind, and handing one evidence that does
# not establish it all fail, each where it was written.
refuse rejected_conditionals
grep -q "'h' does not name the premise this goal supposes" "$run/rejected_conditionals.log"
grep -q "'h' has no premise to stand for" "$run/rejected_conditionals.log"
grep -q "proof 'unguarded_holds' leaves a goal open" "$run/rejected_conditionals.log"
grep -q "has already closed every goal it states" "$run/rejected_conditionals.log"
grep -q "law 'two_preconditions' has 2 expects clauses" "$run/rejected_conditionals.log"

# Supposing a premise does not establish the conclusion, and a premise handed to
# an application is evidence like any other. Only the kernel can say so.
grep -q "kernel-rejection" "$run/rejected_conditionals.log"
grep -q "proof 'false_under_a_premise_holds' does not establish law" \
    "$run/rejected_conditionals.log"
grep -q "proof 'taken_unguarded_holds' does not establish law" "$run/rejected_conditionals.log"

# The premise a conditional law supposes is part of the goal the kernel is
# given, so a trust report can never describe one of these as proven.
if grep -q "Laws proven: *[1-9]" "$run/rejected_conditionals.log"; then
    echo "a refused conditional law was counted as proven" >&2
    exit 1
fi

# A tactic this implementation does not have is refused, not ignored.
refuse unsupported_proof_body
grep -q "does not begin a proof statement" "$run/unsupported_proof_body.log"

# Circular evidence is not evidence.
refuse cyclic_proofs
grep -q "depends on itself" "$run/cyclic_proofs.log"

echo "written proofs fail closed: false, mismatched, mis-instantiated, unknown," \
     "unsupported and circular"
