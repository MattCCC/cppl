#!/usr/bin/env bash
# Specification declarations that must be refused rather than accepted unchecked.
#
# Every case here shares one property: accepting it would leave a claim in the
# program that nothing establishes. A clause on a function that is never
# verified, a termination measure this implementation cannot check, a
# declaration buried where the trust report could not name it -- each would read
# as a guarantee while being none. They fail, and each says where and why.
set -euo pipefail

CPPL="$1"
# These fixtures exist only to be refused, so they live apart from the ones that
# must compile: nothing here is ever expected to produce a program.
FIXTURES="$2/negative"
WORK="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/declarations.XXXXXX")

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

# A termination obligation this implementation does not verify is refused. This
# is the safety-critical direction: accepting an unchecked 'decreases' would let
# a report describe a program as terminating on no evidence at all.
refuse unverified_termination
grep -q "function termination is not verified by this implementation" \
    "$run/unverified_termination.log"
grep -q "the requested 'decreases' obligation must not be accepted unchecked" \
    "$run/unverified_termination.log"
grep -q "a lexicographic 'decreases' list is not verified by this implementation" \
    "$run/unverified_termination.log"
grep -q "state one measure; the requested obligation must not be accepted unchecked" \
    "$run/unverified_termination.log"

# A specification that would never become an obligation is refused, not ignored:
# a contract outside 'verified', and a loop invariant outside a verified body.
refuse unchecked_specifications
grep -q "a contract on a function that is not 'verified' would not be checked" \
    "$run/unchecked_specifications.log"
grep -q "mark the function 'verified' so its contract becomes an obligation" \
    "$run/unchecked_specifications.log"
grep -q "a loop invariant outside a verified function would not be checked" \
    "$run/unchecked_specifications.log"
grep -q "mark the enclosing function 'verified' so its loop invariants become obligations" \
    "$run/unchecked_specifications.log"

# Specification declarations are recognized at namespace scope only, so that the
# trust report can name everything it covers.
refuse misplaced_declarations
grep -q "law 'inner' is declared outside namespace scope" "$run/misplaced_declarations.log"
grep -q "proof 'outer_holds' is declared outside namespace scope" \
    "$run/misplaced_declarations.log"
grep -q "refinement type 'Small' is declared outside namespace scope" \
    "$run/misplaced_declarations.log"
grep -q "'pure' is applied outside namespace scope" "$run/misplaced_declarations.log"
grep -q "'verified' is applied outside namespace scope" "$run/misplaced_declarations.log"

# A method is refused by that same rule, and a virtual one deserves its own
# assertion: the dynamic type decides which body runs, so a contract proved from
# the base's body must never stand as evidence about a call that dispatches to
# an override. The fixture states one in a block, one on a virtual base method
# and one on its override, so all three are reported.
test "$(grep -c "'verified' is applied outside namespace scope" \
    "$run/misplaced_declarations.log")" -ge 3

# A trusted Law is an assumption, so the report must be able to name it.
grep -q "a trusted law must be declared at namespace scope" "$run/misplaced_declarations.log"
grep -q "a trusted assumption is a unit-level declaration" "$run/misplaced_declarations.log"

# A Law states a proposition; a verified function states a runtime contract.
# Confusing the two, or stating no claim at all, is refused.
refuse malformed_declarations
grep -q "a Law conclusion uses 'proves', never 'ensures'" "$run/malformed_declarations.log"
grep -q "replace 'ensures' with 'proves'; a Law has no runtime result" \
    "$run/malformed_declarations.log"
grep -q "a Law has only 'expects' and 'proves' clauses" "$run/malformed_declarations.log"
grep -q "law 'law_without_proposition' states no proposition" "$run/malformed_declarations.log"
grep -q "a law requires exactly one proves clause" "$run/malformed_declarations.log"
grep -q "law 'law_with_two_propositions' has 2 proves clauses" "$run/malformed_declarations.log"
grep -q "a runtime function postcondition uses 'ensures', never 'proves'" \
    "$run/malformed_declarations.log"

# A proof constructs evidence, so it has a body; a trusted Law is assumed, so it
# cannot also carry a proof of itself.
grep -q "a proof declaration has a body" "$run/malformed_declarations.log"
grep -q "a proof constructs evidence, so it ends with '{ ... }' rather than ';'" \
    "$run/malformed_declarations.log"
grep -q "a trusted Law ends with ';': an assumption cannot also have a proof body" \
    "$run/malformed_declarations.log"

# A specifier applies to a function declaration, and the contract's 'result' is
# a value of a return type that must be written rather than deduced.
refuse misapplied_specifiers
grep -q "the 'verified' specifier applies to a function declaration" \
    "$run/misapplied_specifiers.log"
grep -q "the 'pure' specifier applies to a function declaration" \
    "$run/misapplied_specifiers.log"
grep -q "no function declarator follows this specifier" "$run/misapplied_specifiers.log"
grep -q "a deduced return type is not supported on a verified function" \
    "$run/misapplied_specifiers.log"
grep -q "the contract's 'result' is a value of the declared return type" \
    "$run/misapplied_specifiers.log"

# A contract is discharged from the body, so a declaration alone states an
# obligation with nothing to discharge it.
refuse undischarged_contract
grep -q "declared but not defined in this translation unit" "$run/undischarged_contract.log"

# A construct whose delimiters never close fails where it opened, rather than
# reading on and swallowing the declarations that follow it.
refuse unterminated_constructs
grep -q "unterminated refinement predicate" "$run/unterminated_constructs.log"
grep -q "unterminated specification expression" "$run/unterminated_constructs.log"

# None of these reached the kernel, so nothing was reported as proven.
for name in unverified_termination unchecked_specifications misplaced_declarations \
            malformed_declarations misapplied_specifiers undischarged_contract \
            unterminated_constructs; do
    if grep -qE "(Laws|Function contracts|Loop measures) proven: *[1-9]" "$run/$name.log"; then
        echo "$name reported something as proven" >&2
        exit 1
    fi
done

echo "declarations fail closed: unverified, unchecked, misplaced, malformed," \
     "misapplied, undischarged and unterminated"
