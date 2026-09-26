#!/usr/bin/env bash
set -euo pipefail
CPPL="$1"
WORK="$3"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/formal-equality-negative.XXXXXX")
reject() {
    local name="$1"
    cat > "$run/$name.cpp"
    echo 'int main() { return 0; }' >> "$run/$name.cpp"
    if "$CPPL" -std=c++17 "$run/$name.cpp" -o "$run/$name" > "$run/$name.log" 2>&1; then
        echo "invalid formal equality accepted: $name" >&2
        exit 1
    fi
    test ! -e "$run/$name"
    ! grep -q 'PROVEN' "$run/$name.log"
    grep -q 'error' "$run/$name.log"
}
reject false <<'CPP'
proof false_equality() proves (Eq<int>(1, 2)) { refl; }
CPP
reject wrong_evidence <<'CPP'
proof first(int x) proves (Eq<int>(x, x)) { refl; }
proof wrong(int x, int y) proves (Eq<int>(x, y)) { exact first(x); }
CPP
reject wrong_type <<'CPP'
proof wrong(bool x) proves (Eq<int>(x, x)) { refl; }
CPP
reject missing_operand <<'CPP'
proof wrong(int x) proves (Eq<int>(x)) { refl; }
CPP
reject extra_angle <<'CPP'
proof wrong(int x) proves (Eq<int>>(x, x)) { refl; }
CPP
reject extra_operand <<'CPP'
proof wrong(int x) proves (Eq<int>(x, x, x)) { refl; }
CPP
reject unmodeled_type <<'CPP'
struct S {};
bool operator==(S, S) { return true; }
proof wrong(S x, S y) proves (Eq<S>(x, y)) { refl; }
CPP
reject self_reference <<'CPP'
proof wrong(int x) proves (Eq<int>(x, 0)) { exact wrong(x); }
CPP
reject mutual_reference <<'CPP'
proof first(int x) proves (Eq<int>(x, 0)) { exact second(x); }
proof second(int x) proves (Eq<int>(x, 0)) { exact first(x); }
CPP
reject written_failure <<'CPP'
proof wrong(int x) proves (Eq<int>(x, x)) { exact absent; }
CPP
reject outside_scope <<'CPP'
proof first(int x) proves (Eq<int>(x, x)) { refl; }
proof second() proves (Eq<int>(x, x)) { refl; }
CPP
reject same_presumed_location <<'CPP'
#line 10 "same.cpp"
law overloaded(unsigned x) proves (Eq<unsigned>(x, x));
#line 10 "same.cpp"
law overloaded(int x) proves (Eq<int>(x, 0));
CPP
reject unproven_helper <<'CPP'
unsigned helper(unsigned x) { return x; }
proof wrong(unsigned x) proves (Eq<unsigned>(helper(x), x)) { refl; }
CPP
reject impossible_assumption <<'CPP'
proof wrong(int x) proves (Eq<int>(x, 0)) { assume h : Eq<int>(x, 0); exact h; }
CPP
reject false_contract <<'CPP'
verified unsigned wrong(unsigned x) ensures (Eq<unsigned>(result, x)) { return x + 1u; }
CPP
# An explicit equality may compose through a connective (SPEC.md 7.6), but it is
# not a value another explicit equality can compare.
reject formal_operand_of_formal <<'CPP'
proof wrong(int x) proves (Eq<bool>(Eq<int>(x, x), Eq<int>(x, x))) { refl; }
CPP
reject failed_dependency <<'CPP'
proof first(int x) proves (Eq<int>(x, 0)) { refl; }
proof second(int x) proves (Eq<int>(x, 0)) { exact first(x); }
CPP
# Written failures must retain their own failure, even for a true proposition.
grep -q 'no proof or assumed premise' "$run/written_failure.log"
grep -q 'uses itself as its own evidence' "$run/self_reference.log"
grep -q 'depends on itself' "$run/mutual_reference.log"
grep -q 'kernel-rejection' "$run/false.log"
grep -q 'was not admitted' "$run/failed_dependency.log"
grep -q 'unsupported-semantics' "$run/formal_operand_of_formal.log"
column=$(awk 'NR == 1 { print index($0, "(x, x)") + 1 }' "$run/wrong_type.cpp")
grep -q "wrong_type.cpp:1:$column:" "$run/wrong_type.log"
