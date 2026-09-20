#!/usr/bin/env bash
set -euo pipefail
CPPL="$1"
WORK="$3"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/enum-cases-negative.XXXXXX")
reject() {
    local name="$1"
    local diagnostic="$2"
    cat > "$run/$name.cpp"
    echo 'int main() { return 0; }' >> "$run/$name.cpp"
    if "$CPPL" -std=c++20 "$run/$name.cpp" -o "$run/$name" > "$run/$name.log" 2>&1; then
        echo "invalid enum case accepted: $name" >&2
        exit 1
    fi
    test ! -e "$run/$name"
    ! grep -q PROVEN "$run/$name.log"
    if ! grep -q "$diagnostic" "$run/$name.log"; then
        cat "$run/$name.log" >&2
        exit 1
    fi
}
reject missing_residual "non-exhaustive cases: 'unnamed' has no arm" <<'CPP'
enum class E { a };
proof bad(E s) proves(s == E::a) { cases s { E::a => { assume h : s == E::a; exact h; } } }
CPP
reject missing_named "non-exhaustive cases: 'E::b' has no arm" <<'CPP'
enum class E { a, b };
proof bad(E s) proves(s == s) { cases s { E::a => { refl; } unnamed(v) => { refl; } } }
CPP
reject duplicate_alias "duplicate case 'E::a'" <<'CPP'
enum class E { a, alias = a };
proof bad(E s) proves(s == s) {
    cases s { E::a => { refl; } E::alias => { refl; } unnamed(v) => { refl; } }
}
CPP
reject wrong_enum "does not name a case of 'E'" <<'CPP'
enum class E { a };
enum class F { a };
proof bad(E s) proves(s == s) { cases s { F::a => { refl; } unnamed(v) => { refl; } } }
CPP
reject wildcard 'no wildcard' <<'CPP'
enum class E { a };
proof bad(E s) proves(s == s) { cases s { E::a => { refl; } _ => { refl; } } }
CPP
reject forged_case_fact 'no matching premise' <<'CPP'
enum class E { a, b };
proof bad(E s) proves(s == E::b) {
    cases s {
        E::a => { assume invented : s == E::b; exact invented; }
        E::b => { assume correct : s == E::b; exact correct; }
        unnamed(v) => { refl; }
    }
}
CPP
reject false_residual 'does not establish' <<'CPP'
enum class E { a };
proof bad(E s) proves(s == E::a) {
    cases s { E::a => { assume h : s == E::a; exact h; } unnamed(v) => { refl; } }
}
CPP
reject wrong_residual_fact 'no matching premise' <<'CPP'
enum class E : unsigned { a = 1u };
proof bad(E s) proves(s == s) {
    cases s { E::a => { refl; } unnamed(v) => { assume forged : v == 1u; refl; } }
}
CPP
reject sibling_evidence 'no proof or assumed premise' <<'CPP'
enum class E { a, b };
proof bad(E s) proves(s == s) {
    cases s { E::a => { assume h : s == E::a; refl; } E::b => { exact h; } unnamed(v) => { refl; } }
}
CPP
reject escaping_value 'undeclared identifier' <<'CPP'
enum class E { a };
proof other(int x) proves(x == x) { refl; }
proof bad(E s) proves(s == s) {
    cases s { unnamed(v) => { refl; } E::a => { exact other(v); } }
}
CPP
reject shadowed_subject 'redefinition of parameter' <<'CPP'
enum class E { a };
proof bad(E s) proves(s == s) {
    cases s { E::a => { refl; } unnamed(s) => { assume h : s == 0; refl; } }
}
CPP
reject extra_binder "binds 1 value(s), but this arm names 2" <<'CPP'
enum class E { a };
proof bad(E s) proves(s == s) {
    cases s { E::a => { refl; } unnamed(x, y) => { refl; } }
}
CPP
reject missing_binder "binds 1 value(s), but this arm names 0" <<'CPP'
enum class E { a };
proof bad(E s) proves(s == s) { cases s { E::a => { refl; } unnamed => { refl; } } }
CPP
reject named_binder "binds 0 value(s), but this arm names 1" <<'CPP'
enum class E { a };
proof bad(E s) proves(s == s) { cases s { E::a(x) => { refl; } unnamed(v) => { refl; } } }
CPP
reject self_reference 'uses itself' <<'CPP'
enum class E { a };
proof bad(E s) proves(s == s) { cases s { E::a => { exact bad(s); } unnamed(v) => { refl; } } }
CPP
reject cyclic_arms 'depends on itself' <<'CPP'
enum class E { a };
proof first(E s) proves(s == s) { cases s { E::a => { exact second(s); } unnamed(v) => { refl; } } }
proof second(E s) proves(s == s) { exact first(s); }
CPP
reject refused_dependency 'not admitted' <<'CPP'
enum class E { a };
proof invalid(E s) proves(s != s) { refl; }
proof bad(E s) proves(s == s) { cases s { E::a => { exact invalid(s); } unnamed(v) => { refl; } } }
CPP
reject empty_arm 'leaves a goal open' <<'CPP'
enum class E { a };
proof bad(E s) proves(s == s) { cases s { E::a => {} unnamed(v) => { refl; } } }
CPP
reject trailing_statement 'already closed' <<'CPP'
enum class E { a };
proof bad(E s) proves(s == s) { cases s { E::a => { refl; refl; } unnamed(v) => { refl; } } }
CPP
reject malformed_arrow 'cases requires' <<'CPP'
enum class E { a };
proof bad(E s) proves(s == s) { cases s { E::a -> { refl; } } }
CPP
reject unqualified 'must be qualified' <<'CPP'
enum class E { a };
using enum E;
proof bad(E s) proves(s == s) { cases s { a => { refl; } unnamed(v) => { refl; } } }
CPP
reject unknown_label 'no member named' <<'CPP'
enum class E { a };
proof bad(E s) proves(s == s) { cases s { E::unknown => { refl; } unnamed(v) => { refl; } } }
CPP
reject unscoped 'not modeled' <<'CPP'
enum E { a };
proof bad(E s) proves(true) { cases s { E::a => { refl; } unnamed(v) => { refl; } } }
CPP
reject opaque 'not modeled' <<'CPP'
enum class E : unsigned;
proof bad(E s) proves(true) { cases s { unnamed(v) => { refl; } } }
CPP
# A representation with no formal value model is refused by name, and is never
# reinterpreted as a sum because a proof used arm syntax on it.
reject variant "type 'std::variant<int, unsigned int>', which is not modeled" <<'CPP'
#include <variant>
proof bad(std::variant<int, unsigned> s) proves(true) { cases s { unnamed(v) => { refl; } } }
CPP
reject optional "type 'std::optional<int>', which is not modeled" <<'CPP'
#include <optional>
proof bad(std::optional<int> s) proves(true) { cases s { unnamed(v) => { refl; } } }
CPP
reject pointer "type 'int \*', which is not modeled" <<'CPP'
proof bad(int* p) proves(true) { cases p { unnamed(v) => { refl; } } }
CPP
reject product "type 'Point', which is not modeled" <<'CPP'
struct Point { int x; int y; };
proof bad(Point s) proves(true) { cases s { unnamed(v) => { refl; } } }
CPP
# A modeled value that no provider decomposes fails at the provider boundary,
# naming the resolved C++ type rather than the machine type it is carried in.
reject no_provider "proof decomposition is not defined for 'int'" <<'CPP'
proof bad(int s) proves(s == s) { cases s { unnamed(v) => { refl; } } }
CPP
reject narrowed_cast 'exact underlying type' <<'CPP'
enum class E : unsigned { a };
proof bad(E s) proves(Eq<unsigned char>(static_cast<unsigned char>(s), static_cast<unsigned char>(s))) { refl; }
CPP
reject wrong_named_fact_under_binder 'no matching premise' <<'CPP'
enum class E : unsigned { a = 1u };
proof bad(E s) proves(forall(unsigned x) { x == x }) {
    cases s { E::a => { assume h : static_cast<unsigned>(s) == 2u; refl; } unnamed(v) => { refl; } }
}
CPP
reject value_captured_by_quantifier 'does not establish' <<'CPP'
enum class E : unsigned { a = 1u };
proof bad(E s) proves(forall(unsigned x) { x == static_cast<unsigned>(s) }) {
    cases s { E::a => { refl; } unnamed(v) => { refl; } }
}
CPP
echo 'enum cases reject malformed, incomplete, forged, escaping, cyclic, and unsupported proofs'
reject wrong_proof_argument "proof argument has type 'F', but its quantified parameter has type 'E'" <<'CPP'
enum class E : unsigned { a };
enum class F : unsigned { a };
proof identity(E s) proves(s == s) { refl; }
proof bad(F s) proves(s == s) { exact identity(s); }
CPP
reject wrong_quantified_enum_argument "but its quantified parameter has type 'E'" <<'CPP'
enum class E : unsigned { a };
enum class F : unsigned { a };
proof identity() proves(forall(E s) { s == s }) { refl; }
proof bad(F s) proves(s == s) { exact identity(s); }
CPP
reject unused_shadowed_binder 'duplicates an enclosing value' <<'CPP'
enum class E { a };
proof bad(E s) proves(s == s) { cases s { E::a => { refl; } unnamed(s) => { refl; } } }
CPP
reject trailing_malformed_arm 'cases requires' <<'CPP'
enum class E { a };
proof bad(E s) proves(s == s) { cases s { E::a => { refl; } unnamed(v) => { refl; } E::bad } }
CPP
reject empty_binder_list 'cases requires' <<'CPP'
enum class E { a };
proof bad(E s) proves(s == s) { cases s { E::a() => { refl; } unnamed(v) => { refl; } } }
CPP
reject trailing_binder_comma 'identifier after' <<'CPP'
enum class E { a };
proof bad(E s) proves(s == s) { cases s { E::a => { refl; } unnamed(v,) => { refl; } } }
CPP
