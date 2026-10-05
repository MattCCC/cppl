#!/bin/sh
#
# Builds the mechanized model of the kernel's checking judgment (formal/coq,
# docs/KERNEL.md 17) and requires every theorem Audit.v names to rest on no
# axiom and no admitted proof (TRUST.md TCB-META-003).
#
#   tools/formal/check.sh [build directory]
#
# The sources are compiled in a copy under the build directory, so the source
# tree gains no Coq output.

set -eu

root=$(cd "$(dirname "$0")/../.." && pwd)
out="${1:-${root}/build/formal}"
coqc="${COQC:-coqc}"

if ! command -v "${coqc}" >/dev/null 2>&1; then
    echo "coqc not found; install Coq 8.18 or set COQC" >&2
    exit 1
fi

mkdir -p "${out}"
cp "${root}"/formal/coq/*.v "${out}/"
cd "${out}"

"${coqc}" --version

for module in Syntax Semantics Typing Checker Consistency Certificate; do
    "${coqc}" -Q . CppL "${module}.v"
done

"${coqc}" -Q . CppL Audit.v > audit.txt
cat audit.txt

theorems=$(grep -c '^Print Assumptions' Audit.v)
closed=$(grep -c '^Closed under the global context$' audit.txt || true)
if [ "${closed}" != "${theorems}" ]; then
    echo "error: ${theorems} theorems are audited and ${closed} rest on nothing but Coq's kernel" >&2
    exit 1
fi

# No proof may be left unfinished.
if grep -nE '\b(Admitted|admit|Axiom|Parameter|Conjecture)\b' "${root}"/formal/coq/*.v; then
    echo "error: the model contains an axiom or an unfinished proof" >&2
    exit 1
fi

echo "formal model: ${theorems} theorems checked, none resting on an axiom"
