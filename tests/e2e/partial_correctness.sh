#!/usr/bin/env bash
# SPEC: CORRECT-001, CORRECT-002, CORRECT-003, CORRECT-005, CORRECT-006
# A contract proven for partial correctness only is said so where it is
# compiled, not only in the trust report (SPEC.md 23).
#
# Such a contract holds if the function returns, and nothing shows that it
# does. The compiler warns at each one, naming the function and every reason
# its termination is not established: each loop that states no `decreases`,
# with where it stands, each unsafe block, and each partial callee by name. The
# contracts warned about are exactly those the trust report lists as partial,
# and no total contract is warned about. The warning is a warning: the verdict,
# the report and the exit status are what they are without it, `-Werror` does
# not make it an error, as it makes none of C++L's warnings one, and `-w`
# silences it, as it silences them (SPEC.md WORD-018). The editor shows the
# same warning (unit/lsp_server_test.cpp).
set -euo pipefail

CPPL="$1"
FIXTURES="$2"
WORK="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/partial-correctness.XXXXXX")
# Compiled from a copy beside its outputs, so every location names the file as
# it is given.
cp "$FIXTURES/partial_correctness.cpp" "$run/"
cd "$run"

fail() {
    echo "$1" >&2
    exit 1
}

# compiled <name> [arguments...]: the unit verifies and builds, whatever it warns.
compiled() {
    local name="$1"
    shift
    if ! "$CPPL" -std=c++20 partial_correctness.cpp -o "$name" --cppl-trust-report "$@" > "$name.report" \
        2> "$name.err"; then
        cat "$name.err" >&2
        fail "the unit was refused ($name): a partial-correctness contract is proven, and warned about, not refused"
    fi
    ./"$name" || fail "the program built ($name) did not compute what its contracts state"
}

compiled plain

expected="partial_correctness.cpp:10:19: warning [partial-correctness]: the contract of 'loop_nodec' is proven \
for partial correctness only: the loop at partial_correctness.cpp:14:5 states no 'decreases', so it is not shown to \
terminate (SPEC.md CORRECT-002)
  partial_correctness.cpp:14:5: note: the loop that states no 'decreases'
partial_correctness.cpp:37:19: warning [partial-correctness]: the contract of 'calls_partial' is proven for partial \
correctness only: it calls 'loop_nodec', whose termination is not established, so the call is not shown to return \
(SPEC.md CORRECT-005)
partial_correctness.cpp:51:19: warning [partial-correctness]: the contract of 'both_reasons' is proven for partial \
correctness only: the loop at partial_correctness.cpp:55:5 states no 'decreases', so it is not shown to terminate \
(SPEC.md CORRECT-002); it calls 'calls_partial', whose termination is not established, so the call is not shown to \
return (SPEC.md CORRECT-005)
  partial_correctness.cpp:55:5: note: the loop that states no 'decreases'
partial_correctness.cpp:64:19: warning [partial-correctness]: the contract of 'through_unsafe' is proven for partial \
correctness only: it passes through the unsafe block at partial_correctness.cpp:68:5, which need not return (SPEC.md \
CORRECT-003)
  partial_correctness.cpp:68:5: note: the unsafe block, which need not return"
# Exactly these, and nothing else: no warning at a total contract, none
# repeated, and every reason of each.
if [ "$(cat plain.err)" != "$expected" ]; then
    echo "the partial-correctness warnings are not exactly the expected ones:" >&2
    diff -u <(printf '%s\n' "$expected") plain.err >&2 || true
    exit 1
fi
for total in loop_dec calls_total straight count_down; do
    if grep -q "'$total'" plain.err; then
        fail "the total contract of '$total' was warned about"
    fi
done

# The verdict is the report's, and the report's partial contracts are the ones
# warned about: one decision, read in both places.
for line in 'Function contracts proven: +8' '  partial correctness only: +4' 'Loop measures proven: +1' \
    'Recursive call measures proven: +1' 'Unresolved obligations: +0' 'Partial-correctness contracts: 4'; do
    grep -Eq "^$line\$" plain.report || { cat plain.report >&2; fail "the trust report does not state '$line'"; }
done
reported=$(awk '/^Partial-correctness contracts:/ { listing = 1; next }
                listing && /^  contract of / { print $3; next }
                { listing = 0 }' plain.report | sort)
warned=$(sed -n "s/^.*warning \[partial-correctness\]: the contract of '\([^']*\)' is proven.*$/\1/p" plain.err | sort)
if [ "$reported" != "$warned" ] || [ -z "$warned" ]; then
    fail "the contracts warned about ($(echo $warned)) are not those the report lists as partial ($(echo $reported))"
fi

# -Werror turns none of C++L's warnings into an error: the same unit, verdict
# and warnings, and the exit status of a build that succeeded.
compiled werror -Werror
[ "$(cat werror.err)" = "$expected" ] || { cat werror.err >&2; fail "-Werror changed what the unit warns"; }
cmp -s plain.report werror.report || fail "-Werror changed the trust report"

# -w silences it, and nothing else changes.
compiled silenced -w
if [ -s silenced.err ]; then
    cat silenced.err >&2
    fail "-w did not silence the partial-correctness warnings"
fi
cmp -s plain.report silenced.report || fail "-w changed the trust report"

# A function whose `decreases` asks that it terminate, and whose loop states no
# measure, is refused for it (SPEC.md TERMINATION-006): the refusal is the
# diagnostic, and it is not also warned about as a proven partial contract.
cat > asks.cpp <<'CPP'
verified unsigned asks(unsigned n)
    ensures (result >= n)
    decreases (n)
{
    unsigned i = 0u;
    while (i < n)
        invariant (i <= n)
    {
        i = i + 1u;
    }
    return i;
}

int main() {
    return static_cast<int>(asks(0u));
}
CPP
if "$CPPL" -std=c++20 asks.cpp -o asks > asks.out 2> asks.err; then
    fail "a function asking to terminate, with a loop that states no measure, was accepted"
fi
grep -Fq "asks.cpp:6:5: error [proof-failure]: the termination of verified function 'asks' is not established" asks.err ||
    { cat asks.err >&2; fail "the function asking to terminate was not refused for its unmeasured loop"; }
if grep -q 'partial-correctness' asks.err; then
    cat asks.err >&2
    fail "a function refused for not terminating was also warned about as a proven partial contract"
fi

echo 'each partial-correctness contract is warned about with its reasons, no total one is, and nothing else changes'
