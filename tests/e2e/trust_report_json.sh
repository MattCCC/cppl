#!/usr/bin/env bash
# SPEC: TUBOUND-006, TUBOUND-014, RUNTIMECHECK-013, RUNTIMECHECK-014, STDMODEL-018
# TRUST.md 36.2, Annex C, TCB-REPORT-006, TCB-XTU-010, TCB-TEST-006
#
# The trust report as a JSON document (`--cppl-emit-trust-report=<file>`) says
# what the text report says about the same compile, for trusted laws, unsafe
# code, library models, runtime validation sites, partial correctness and
# contracts imported from other units:
#
#   - every count of the text report is the same count in the document;
#   - the claims the text lists as assumption-free are exactly those the
#     document marks `"assumption_free": true`, and those it lists as resting
#     on trusted laws exactly those it marks `"trusted_closure_empty": false`;
#   - every trusted law, unsafe region and runtime validation site is there with
#     its status, and a claim proven through another unit's contract names the
#     interface it came from, with interface provenance unauthenticated;
#   - the document is ASCII, begins with the report's prefix and is the same
#     bytes when the same unit is compiled again;
#   - a compile that does not verify leaves no report, removes one an earlier
#     compile left at the path, and leaves alone a file there that is not one;
#     the option is refused without a file, twice, and where nothing compiles.
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/trust-json.XXXXXX")

fail() {
    echo "$1" >&2
    exit 1
}

# A count of the text report, by its label.
text_count() {
    sed -n "s/^$2 *\\([0-9][0-9]*\\)\$/\\1/p" "$1"
}

# A count of the document, by its key.
json_count() {
    sed -n "s/^    \"$2\": \\([0-9][0-9]*\\),\\{0,1\\}\$/\\1/p" "$1"
}

# One line per claim of the document: identity, assumption_free,
# trusted_closure_empty. A claim's own members stand six spaces in.
claims() {
    awk '
        /^  "claims": \[$/ { inside = 1; next }
        inside && /^  \]/ { inside = 0 }
        inside && /^      "identity": / { gsub(/[",]/, "", $2); identity = $2 }
        inside && /^      "assumption_free": / { gsub(/,/, "", $2); free = $2 }
        inside && /^      "trusted_closure_empty": / { gsub(/,/, "", $2); print identity, free, $2 }
    ' "$1"
}

# The identities of the claims one section of the text report lists.
listed() {
    sed -n "/^$2/,/^$3/p" "$1" | sed -n 's/^  [^ ].*, identity \([0-9a-f]\{16\}\)$/\1/p' | sort
}

# The document and the text report of one compile agree.
agree() {
    local report="$1" json="$2" label key text value
    [ "$(head -n 2 "$json")" = $'{\n  "format": "cppl-trust-report",' ] ||
        fail "$json does not begin as a trust report"
    grep -q '^  "version": 1,$' "$json" || fail "$json is not version 1"
    if LC_ALL=C grep -n '[^ -~]' "$json" > "$run/unprintable"; then
        cat "$run/unprintable" >&2
        fail "$json holds a byte outside printable ASCII"
    fi
    while IFS='|' read -r label key; do
        text=$(text_count "$report" "$label")
        value=$(json_count "$json" "$key")
        [ -n "$text" ] || fail "the text report states no '$label'"
        [ "$text" = "$value" ] || fail "'$label' is $text in the text report and $key is '$value' in $json"
    done <<'PAIRS'
Laws proven:|laws_proven
  by a written proof:|laws_proven_by_written_proof
Proof declarations proven:|proof_declarations_proven
Laws trusted:|laws_trusted
Function contracts proven:|function_contracts_proven
  partial correctness only:|partial_correctness_contracts_proven
Function contracts imported:|function_contracts_imported
Call preconditions proven:|call_preconditions_proven
Defined operations proven:|defined_operations_proven
Loop invariants proven:|loop_invariants_proven
Loop measures proven:|loop_measures_proven
Recursive call measures proven:|recursive_call_measures_proven
Omitted cases proven:|omitted_cases_proven
Impossible paths proven:|impossible_paths_proven
Unresolved obligations:|unresolved_obligations
Trust-dependent claims:|trust_dependent_claims
Unsafe-dependent claims:|unsafe_dependent_claims
Assumption-free claims:|assumption_free_claims
Unused trusted laws:|unused_trusted_laws
Library-model-dependent claims:|library_model_dependent_claims
Partial-correctness contracts:|partial_correctness_contracts
Interface-dependent claims:|interface_dependent_claims
Runtime-check-dependent claims:|runtime_check_dependent_claims
Unsafe regions:|unsafe_regions
Runtime validation sites:|runtime_validation_sites
PAIRS

    claims "$json" > "$run/claims"
    [ -s "$run/claims" ] || fail "$json lists no claim"
    [ "$(listed "$report" 'Assumption-free claims:' 'Unused trusted laws:')" = \
        "$(awk '$2 == "true" { print substr($1, 1, 16) }' "$run/claims" | sort)" ] ||
        fail "the assumption-free claims of $json are not the ones the text report lists"
    [ "$(listed "$report" 'Trust-dependent claims:' 'Unsafe-dependent claims:')" = \
        "$(awk '$3 == "false" { print substr($1, 1, 16) }' "$run/claims" | sort)" ] ||
        fail "the claims of $json resting on trusted laws are not the ones the text report lists"
    [ "$(grep -c '^      "status": "PROVEN",$' "$json")" = "$(wc -l < "$run/claims" | tr -d ' ')" ] ||
        fail "a claim of $json is not PROVEN"
    [ "$(grep -c '^      "status": "TRUSTED",$' "$json")" = "$(text_count "$report" 'Laws trusted:')" ] ||
        fail "the trusted laws of $json are not all TRUSTED"
    [ "$(grep -c '^      "status": "UNSAFE",$' "$json")" = "$(text_count "$report" 'Unsafe regions:')" ] ||
        fail "the unsafe regions of $json are not all UNSAFE"
    [ "$(grep -c '^      "status": "RUNTIME-CHECKED",$' "$json")" = \
        "$(text_count "$report" 'Runtime validation sites:')" ] ||
        fail "the runtime validation sites of $json are not all RUNTIME-CHECKED"
}

# compile <name> <cppl arguments>...: the text report and the document of one
# compile, which must verify.
compile() {
    local name="$1"
    shift
    if ! "$CPPL" "$@" --cppl-trust-report "--cppl-emit-trust-report=$run/$name.json" > "$run/$name.report" \
        2> "$run/$name.log"; then
        cat "$run/$name.log" >&2
        fail "$name did not verify"
    fi
    agree "$run/$name.report" "$run/$name.json"
}

# --- Each kind of dependency, against the text report -------------------------

compile trusted -std=c++20 "$FIXTURES/trust_closure.cpp" -o "$run/trusted"
[ "$(json_count "$run/trusted.json" trust_dependent_claims)" -gt 0 ] || fail "trust_closure.cpp rests on no trusted law"
grep -q '^      "kind": "proposition",$' "$run/trusted.json" || fail "a trusted law does not say it states a proposition"
grep -q '^  "interface_provenance": "none_imported"$' "$run/trusted.json" ||
    fail "a compile that imports nothing does not say so"

compile unsafe -std=c++20 "$FIXTURES/unsafe_boundary.cpp" -o "$run/unsafe"
[ "$(json_count "$run/unsafe.json" unsafe_dependent_claims)" -gt 0 ] || fail "unsafe_boundary.cpp rests on no unsafe code"
grep -q '^      "kind": "block",$' "$run/unsafe.json" || fail "no unsafe block is reported as one"

compile containers -std=c++20 "$FIXTURES/containers.cpp" -o "$run/containers"
[ "$(json_count "$run/containers.json" library_model_dependent_claims)" -gt 0 ] ||
    fail "containers.cpp rests on no library model"
grep -q '^          "model": "std::vector",$' "$run/containers.json" || fail "no claim names the vector model"

compile runtime -std=c++20 "$FIXTURES/runtime_validation.cpp" -o "$run/runtime"
[ "$(json_count "$run/runtime.json" runtime_validation_sites)" -gt 0 ] ||
    fail "runtime_validation.cpp has no runtime validation site"

compile termination -std=c++20 "$FIXTURES/termination.cpp" -o "$run/termination"
grep -q '^      "correctness": "total",$' "$run/termination.json" || fail "no contract is reported total"

# --- Contracts of other units --------------------------------------------------

mkdir "$run/ledger"
cp "$FIXTURES"/integration/* "$run/ledger/"
(
    cd "$run/ledger"
    "$CPPL" -std=c++20 -c text.cpp -o text.o --cppl-emit-interface=text.cppli > /dev/null
    "$CPPL" -std=c++20 -c ledger.cpp -o ledger.o --cppl-emit-interface=ledger.cppli > /dev/null
)
compile statement -std=c++20 -c "$run/ledger/statement.cpp" -o "$run/statement.o" \
    "--cppl-import-interface=$run/ledger/text.cppli" "--cppl-import-interface=$run/ledger/ledger.cppli"
json="$run/statement.json"
[ "$(json_count "$json" function_contracts_imported)" -gt 0 ] || fail "the statement imports no contract"
grep -q '^  "interface_provenance": "unauthenticated"$' "$json" || fail "imported contracts are not said to be unauthenticated"
grep -q "^          \"interface\": \"$run/ledger/text.cppli\",\$" "$json" ||
    fail "a claim resting on the tokenizer does not name the interface it came from"
grep -q '^          "direct": true,$' "$json" || fail "no imported contract a claim rests on is marked direct"
# SPEC: TUBOUND-014 -- a claim resting on another unit's record is never
# assumption-free, however little that record rests on.
imported_free=$(awk '
    /^  "claims": \[$/ { inside = 1; next }
    inside && /^  \]/ { inside = 0 }
    inside && /^      "assumption_free": / { gsub(/,/, "", $2); free = $2 }
    inside && /^      "imported_contracts": \[$/ && free == "true" { print }
' "$json")
[ -z "$imported_free" ] || fail "a claim proven through another unit's contract is marked assumption-free"

# --- The same compile is the same document -------------------------------------

"$CPPL" -std=c++20 "$FIXTURES/trust_closure.cpp" -o "$run/again" "--cppl-emit-trust-report=$run/again.json" > /dev/null
cmp -s "$run/trusted.json" "$run/again.json" || fail "compiling the same unit again wrote another document"

# --- A compile that does not verify leaves no report ---------------------------

if "$CPPL" -std=c++17 "$FIXTURES/false_law.cpp" -o "$run/false" "--cppl-emit-trust-report=$run/false.json" \
    > "$run/false.log" 2>&1; then
    fail "a false law verified"
fi
[ ! -e "$run/false.json" ] || fail "a compile that did not verify wrote a trust report"

cp "$run/trusted.json" "$run/stale.json"
if "$CPPL" -std=c++17 "$FIXTURES/false_law.cpp" -o "$run/false" "--cppl-emit-trust-report=$run/stale.json" \
    > "$run/stale.log" 2>&1; then
    fail "a false law verified"
fi
[ ! -e "$run/stale.json" ] || fail "a compile that did not verify left an earlier trust report in place"

printf 'not a trust report\n' > "$run/kept.json"
if "$CPPL" -std=c++17 "$FIXTURES/false_law.cpp" -o "$run/false" "--cppl-emit-trust-report=$run/kept.json" \
    > "$run/kept.log" 2>&1; then
    fail "a false law verified"
fi
[ "$(cat "$run/kept.json")" = 'not a trust report' ] || fail "a failed compile removed a file that was not a trust report"

# --- The option itself -----------------------------------------------------------

refused() {
    local name="$1" pattern="$2"
    shift 2
    if "$CPPL" "$@" > "$run/$name.log" 2>&1; then
        fail "$name was accepted"
    fi
    grep -q -- "$pattern" "$run/$name.log" || {
        cat "$run/$name.log" >&2
        fail "$name was refused for another reason"
    }
}
refused empty "names no file" -std=c++20 "$FIXTURES/trust_closure.cpp" -o "$run/empty" --cppl-emit-trust-report=
refused twice "more than once" -std=c++20 "$FIXTURES/trust_closure.cpp" -o "$run/twice" \
    "--cppl-emit-trust-report=$run/one.json" "--cppl-emit-trust-report=$run/two.json"
refused preprocess "compiles nothing" -std=c++20 -E "$FIXTURES/trust_closure.cpp" \
    "--cppl-emit-trust-report=$run/preprocess.json"
[ ! -e "$run/preprocess.json" ] || fail "a command that compiles nothing wrote a trust report"

# A destination that cannot be replaced fails the compile and leaves no part of
# the report beside it.
mkdir "$run/occupied.json"
refused occupied "cannot write trust report" -std=c++20 "$FIXTURES/trust_closure.cpp" -o "$run/occupied" \
    "--cppl-emit-trust-report=$run/occupied.json"
ls "$run" | grep -q 'partial' && fail "a report that could not be written left a partial file"

echo "the JSON trust report states what the text report states, and only for a compile that verified"
