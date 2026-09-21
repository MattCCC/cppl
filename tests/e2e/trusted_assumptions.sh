#!/usr/bin/env bash
# The trusted boundary: an assumption the author states explicitly, recorded
# rather than proved (SPEC.md 27, TRUST.md 29).
#
# `trusted` is the escape hatch for a fact C++L cannot establish - an external
# API contract, an OS guarantee. It is not a weaker kind of proof. These cases
# pin that it stays an escape hatch: it is never reported as proven, it is
# always named in the trust report, and it never closes an obligation that
# something else was supposed to discharge.
set -euo pipefail
CPPL="$1"
WORK="$3"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/trusted.XXXXXX")

# A program this implementation must accept, with its trust report.
accept() {
    local name="$1"
    cat > "$run/$name.cpp"
    echo 'int main() { return 0; }' >> "$run/$name.cpp"
    if ! "$CPPL" -std=c++20 --cppl-trust-report "$run/$name.cpp" -o "$run/$name" \
        > "$run/$name.log" 2>&1; then
        echo "a valid trusted declaration was refused: $name" >&2
        cat "$run/$name.log" >&2
        exit 1
    fi
}

# A program this implementation must refuse, and the reason it must give.
refuse() {
    local name="$1" pattern="$2"
    cat > "$run/$name.cpp"
    echo 'int main() { return 0; }' >> "$run/$name.cpp"
    if "$CPPL" -std=c++20 --cppl-trust-report "$run/$name.cpp" -o "$run/$name" \
        > "$run/$name.log" 2>&1; then
        echo "an invalid trusted declaration was accepted: $name" >&2
        cat "$run/$name.log" >&2
        exit 1
    fi
    test ! -e "$run/$name"
    if ! grep -Eq "$pattern" "$run/$name.log"; then
        echo "$name was refused for an unexpected reason:" >&2
        cat "$run/$name.log" >&2
        exit 1
    fi
}

# What the trust report must say about the program just accepted.
reports() {
    local name="$1" pattern="$2"
    if ! grep -Eq "$pattern" "$run/$name.log"; then
        echo "the trust report for $name does not state: $pattern" >&2
        cat "$run/$name.log" >&2
        exit 1
    fi
}

# --- An assumption is recorded, not proved -----------------------------------

accept an_assumption_is_accepted <<'CPP'
trusted law external_guarantee(unsigned x) ensures(x + 0u == x);
CPP

reports an_assumption_is_accepted 'Laws trusted: +1'
reports an_assumption_is_accepted 'assumed: +external_guarantee'
reports an_assumption_is_accepted 'Trusted external axioms: +1'

# It is an assumption, so it is never counted among the proven laws.
reports an_assumption_is_accepted 'Laws proven: +0'

# The report names where the assumption was made, so it can be audited.
reports an_assumption_is_accepted 'an_assumption_is_accepted\.cpp:1'

# --- Trust is explicit, never inferred ---------------------------------------

# The same proposition without `trusted` owes a proof like any other law.
refuse an_ordinary_law_still_owes_a_proof 'does not establish|not proven|unresolved' <<'CPP'
law unprovable(unsigned x) ensures(x + 1u == x);
CPP

# An author may assume something false. That is the point of an escape hatch:
# C++L records the choice instead of silently refusing or silently proving it.
accept an_assumption_may_be_false <<'CPP'
trusted law wrong(unsigned x) ensures(x + 1u == x);
CPP

reports an_assumption_may_be_false 'Laws trusted: +1'
reports an_assumption_may_be_false 'Laws proven: +0'

# --- Assuming and proving the same law is a contradiction --------------------

refuse an_assumption_with_a_written_proof 'nothing to discharge' <<'CPP'
trusted law assumed(unsigned x) ensures(x + 0u == x);
proof assumed_proof(unsigned x) proves(assumed(x)) { refl; }
CPP

# --- `trusted` stays contextual ----------------------------------------------

accept trusted_is_an_ordinary_identifier <<'CPP'
int trusted = 3;
CPP

reports trusted_is_an_ordinary_identifier 'Laws trusted: +0'

# A unit with no assumption reports none, because that is true of it.
accept a_unit_with_no_assumption <<'CPP'
law provable(unsigned x) ensures(x + 0u == x);
CPP

reports a_unit_with_no_assumption 'Laws trusted: +0'
reports a_unit_with_no_assumption 'Trusted external axioms: +0'

# --- A trusted law must be a unit-level declaration --------------------------

refuse an_assumption_inside_a_function 'namespace scope' <<'CPP'
void f() { trusted law local(unsigned x) ensures(x + 0u == x); }
CPP

echo 'trusted assumptions: recorded, never proven'
