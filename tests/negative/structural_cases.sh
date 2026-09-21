#!/usr/bin/env bash
set -euo pipefail
CPPL="$1"
WORK="$3"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/structural-cases-negative.XXXXXX")
reject() {
    local name="$1"
    local diagnostic="$2"
    cat > "$run/$name.cpp"
    echo 'int main() { return 0; }' >> "$run/$name.cpp"
    if "$CPPL" -std=c++20 "$run/$name.cpp" -o "$run/$name" > "$run/$name.log" 2>&1; then
        echo "invalid structural decomposition accepted: $name" >&2
        exit 1
    fi
    test ! -e "$run/$name"
    ! grep -q PROVEN "$run/$name.log"
    if ! grep -q "$diagnostic" "$run/$name.log"; then
        cat "$run/$name.log" >&2
        exit 1
    fi
}

# 13. Exhaustiveness is provider-driven: an omitted alternative is named, and no
# arm is treated as an implicit catch-all for the rest.
reject variant_missing_alternative "non-exhaustive cases: 'alternative<1>' has no arm" <<'CPP'
#include <variant>
proof bad(std::variant<int, bool> v) proves(true) {
    cases v { alternative<0>(a) => { refl; } valueless => { refl; } }
}
CPP
# 3. `valueless` is a real state of every variant and may never be omitted.
reject variant_missing_valueless "non-exhaustive cases: 'valueless' has no arm" <<'CPP'
#include <variant>
proof bad(std::variant<int, bool> v) proves(true) {
    cases v { alternative<0>(a) => { refl; } alternative<1>(b) => { refl; } }
}
CPP
reject variant_out_of_range "'alternative<2>' is not a case of" <<'CPP'
#include <variant>
proof bad(std::variant<int, bool> v) proves(true) {
    cases v {
        alternative<0>(a) => { refl; }
        alternative<1>(b) => { refl; }
        alternative<2>(c) => { refl; }
        valueless => { refl; }
    }
}
CPP
reject variant_duplicate_alternative "duplicate case 'alternative<0>'" <<'CPP'
#include <variant>
proof bad(std::variant<int, bool> v) proves(true) {
    cases v {
        alternative<0>(a) => { refl; }
        alternative<0>(b) => { refl; }
        alternative<1>(c) => { refl; }
        valueless => { refl; }
    }
}
CPP
# 4. An optional has exactly two states and binds its payload only in `some`.
reject optional_missing_none "non-exhaustive cases: 'none' has no arm" <<'CPP'
#include <optional>
proof bad(std::optional<int> o) proves(true) { cases o { some(v) => { refl; } } }
CPP
reject optional_missing_some "non-exhaustive cases: 'some' has no arm" <<'CPP'
#include <optional>
proof bad(std::optional<int> o) proves(true) { cases o { none => { refl; } } }
CPP
reject optional_payload_in_none "binds 0 value(s), but this arm names 1" <<'CPP'
#include <optional>
proof bad(std::optional<int> o) proves(true) {
    cases o { some(v) => { refl; } none(v) => { refl; } }
}
CPP
reject optional_missing_payload "binds 1 value(s), but this arm names 0" <<'CPP'
#include <optional>
proof bad(std::optional<int> o) proves(true) {
    cases o { some => { refl; } none => { refl; } }
}
CPP
# 6. A pointer decomposes into exactly two states and says nothing more.
reject pointer_missing_non_null "non-exhaustive cases: 'non_null' has no arm" <<'CPP'
proof bad(int* p) proves(true) { cases p { null => { refl; } } }
CPP
reject pointer_binds_pointee "binds 0 value(s), but this arm names 1" <<'CPP'
proof bad(int* p) proves(true) {
    cases p { null => { refl; } non_null(pointee) => { refl; } }
}
CPP
# 14. There is no wildcard and no internal escape hatch for unmodeled states.
reject variant_wildcard 'no wildcard' <<'CPP'
#include <variant>
proof bad(std::variant<int, bool> v) proves(true) {
    cases v { alternative<0>(a) => { refl; } _ => { refl; } }
}
CPP
# 7. A product is not a sum: it has one arm and no alternatives to choose.
reject product_as_sum "product decomposition requires" <<'CPP'
struct Point { int x; int y; };
proof bad(Point p) proves(true) { decompose p { some(v) => { refl; } none => { refl; } } }
CPP
reject product_wrong_arity "product binds 2 value(s), but this arm names 3" <<'CPP'
struct Point { int x; int y; };
proof bad(Point p) proves(true) { decompose p { components(x, y, z) => { refl; } } }
CPP
reject sum_as_product "cases requires\|is not a case" <<'CPP'
#include <optional>
proof bad(std::optional<int> o) proves(true) { cases o { components(v) => { refl; } } }
CPP
# 7. Access control is respected: a private member is not projectable.
reject inaccessible_member "cannot access member" <<'CPP'
class Hidden { int secret; public: int shown; };
proof bad(Hidden h) proves(true) { decompose h { components(secret, shown) => { refl; } } }
CPP
# 7. Representations whose states need an independent justification are refused
# by name rather than decomposed on a guess.
reject union_member "a union requires an independently justified active-member model" <<'CPP'
union U { int a; bool b; };
proof bad(U v) proves(true) { decompose v { components(a, b) => { refl; } } }
CPP
reject base_subobject "base subobject decomposition requires an explicit accessible projection" <<'CPP'
struct Base { int x; };
struct Derived : Base { int y; };
proof bad(Derived v) proves(true) { decompose v { components(x, y) => { refl; } } }
CPP
# 7/11. A reference member's referent can be written elsewhere, so it is not a
# component this framework can project.
reject reference_member "has an unmodeled type 'int &'" <<'CPP'
struct WithRef { int& r; bool b; };
proof bad(WithRef w) proves(true) { decompose w { components(r, b) => { refl; } } }
CPP
reject incomplete_type "proof decomposition unavailable for incomplete type" <<'CPP'
struct Opaque;
proof bad(Opaque& o) proves(true) { decompose o { components(x) => { refl; } } }
CPP
# 19. Standard types are recognized by semantic identity, never by spelling: a
# user type spelled like one is an ordinary record and has no sum arms.
reject lookalike_optional "product decomposition requires" <<'CPP'
namespace mine { template <typename T> struct optional { bool engaged; T payload; }; }
proof bad(mine::optional<int> o) proves(true) {
    cases o { some(v) => { refl; } none => { refl; } }
}
CPP
reject lookalike_variant "product decomposition requires" <<'CPP'
namespace other { namespace std { template <typename... T> struct variant { int tag; }; } }
proof bad(other::std::variant<int, bool> v) proves(true) {
    cases v { alternative<0>(a) => { refl; } valueless => { refl; } }
}
CPP
# 15/24. A branch fact must come from the branch; none may be manufactured.
reject forged_variant_fact 'no matching premise' <<'CPP'
#include <variant>
proof bad(std::variant<unsigned, bool> v) proves(true) {
    cases v {
        alternative<0>(a) => { assume invented : a != a; exact invented; }
        alternative<1>(b) => { refl; }
        valueless => { refl; }
    }
}
CPP
# 23. Binders stay local to their arm and may not escape or shadow the subject.
reject binder_escapes_arm 'undeclared identifier' <<'CPP'
#include <optional>
proof other(int x) proves(x == x) { refl; }
proof bad(std::optional<int> o) proves(true) {
    cases o { none => { exact other(v); } some(v) => { refl; } }
}
CPP
reject binder_shadows_subject 'duplicates an enclosing value' <<'CPP'
#include <optional>
proof bad(std::optional<int> o) proves(true) {
    cases o { some(o) => { refl; } none => { refl; } }
}
CPP
# 8. A nested arm closes its own goal; an unclosed inner arm is not absorbed.
reject nested_open_goal 'leaves a goal open' <<'CPP'
#include <optional>
#include <variant>
proof bad(std::variant<std::optional<int>, bool> v) proves(true) {
    cases v {
        alternative<0>(inner) => { cases inner { some(p) => {} none => { refl; } } }
        alternative<1>(f) => { refl; }
        valueless => { refl; }
    }
}
CPP
echo 'structural decomposition rejects missing, duplicated, forged, escaping and inaccessible cases'
