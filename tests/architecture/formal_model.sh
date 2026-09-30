#!/usr/bin/env bash
# TRUST.md TCB-META-001, TCB-META-002
#
# The mechanized model (formal/coq, docs/KERNEL.md 17) states one version of the
# core. A change to the kernel's term formers, primitives or evidence formers,
# or to the core version, has to change the model too, or this fails: a model
# of another core proves nothing about this one.
#
# usage: formal_model.sh <source root>
set -euo pipefail

ROOT="$1"
KERNEL="$ROOT/kernel/include/cppl/kernel"
MODEL="$ROOT/formal/coq"

fail() {
    echo "$1" >&2
    exit 1
}

kernel_core=$(sed -n 's/.*kFormalCoreVersion = "\([^"]*\)".*/\1/p' "$KERNEL/version.hpp")
model_core=$(sed -n 's/^Definition core_version : string := "\([^"]*\)"\.$/\1/p' "$MODEL/Syntax.v")
[[ -n "$kernel_core" ]] || fail "no kFormalCoreVersion in version.hpp"
[[ "$kernel_core" == "$model_core" ]] ||
    fail "the model states core '$model_core' and the kernel implements '$kernel_core'"

# The variant sizes the kernel pins with static_assert.
kernel_terms=$(sed -n 's/.*variant_size_v<decltype(Term::node)> == \([0-9]*\).*/\1/p' "$KERNEL/term.hpp")
kernel_proofs=$(sed -n 's/.*variant_size_v<decltype(ProofTerm::node)> == \([0-9]*\).*/\1/p' "$KERNEL/proof.hpp")

# The primitive operations the kernel's enumeration lists.
kernel_ops=$(awk '/^enum class PrimOp/{inside=1; next} inside && /^};/{exit} inside' "$KERNEL/term.hpp" |
    sed 's,//.*,,' | tr ',' '\n' | grep -cE '^[[:space:]]*[A-Z][A-Za-z]*[[:space:]]*$')

# Constructors of an inductive type in the model: the '|' lines after its header,
# up to the terminating '.'.
constructors() {
    awk -v header="$2" '
        index($0, header) == 1 { inside = 1; next }
        inside && /^\|/ { count++ }
        inside && /\.[[:space:]]*(\(\*.*)?$/ { print count; exit }
    ' "$1"
}

model_terms=$(constructors "$MODEL/Syntax.v" "Inductive term : Type :=")
model_proofs=$(constructors "$MODEL/Checker.v" "Inductive evid : Type :=")
model_ops=$(awk '/^Inductive op : Type :=/{inside=1; next} inside {print} inside && /\.$/{exit}' "$MODEL/Syntax.v" |
    tr '|' '\n' | grep -cE '^[[:space:]]*[A-Z][A-Za-z]*[[:space:]]*\.?[[:space:]]*$')

[[ "$kernel_terms" == "$model_terms" ]] ||
    fail "the kernel has $kernel_terms term formers and the model $model_terms"
[[ "$kernel_proofs" == "$model_proofs" ]] ||
    fail "the kernel has $kernel_proofs evidence formers and the model $model_proofs"
[[ "$kernel_ops" == "$model_ops" ]] ||
    fail "the kernel has $kernel_ops primitives and the model $model_ops"

echo "formal model states $model_core: $model_terms term formers, $model_ops primitives, $model_proofs evidence formers"
