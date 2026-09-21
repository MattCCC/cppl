#!/usr/bin/env bash
set -euo pipefail
CPPL="$1"
WORK="$3"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/source-identity.XXXXXX")

reject() {
    local name="$1"
    local status=0
    "$CPPL" -std=c++17 --cppl-trust-report "$run/$name.cpp" -o "$run/$name" \
        > "$run/$name.log" 2>&1 || status=$?
    if [ "$status" -ne 1 ] || [ -e "$run/$name" ]; then
        echo "invalid declaration identity was not rejected: $name (exit $status)" >&2
        cat "$run/$name.log" >&2
        exit 1
    fi
    grep -q 'error' "$run/$name.log"
}

cat > "$run/namespaces.cpp" <<'CPP'
namespace A { law same() proves (0u == 0u); } namespace B { law same() proves (0u == 1u); }
int main() { return 0; }
CPP
reject namespaces

cat > "$run/overloads.cpp" <<'CPP'
law same(unsigned x) proves (x == x); law same(int x) proves (0u == 1u);
int main() { return 0; }
CPP
reject overloads

cat > "$run/ordinary.cpp" <<'CPP'
bool same(unsigned) { return 0u == 0u; } law same(int x) proves (0u == 1u);
int main() { return 0; }
CPP
reject ordinary

cat > "$run/macros.cpp" <<'CPP'
#define FIRST namespace A { law same() proves (0u == 0u); }
#define SECOND namespace B { law same() proves (0u == 1u); }
FIRST SECOND
int main() { return 0; }
CPP
reject macros

cat > "$run/remapped.cpp" <<'CPP'
#line 1 "same.cpp"
verified unsigned a() ensures(result == 0u) { return 0u; }
#line 1 "same.cpp"
verified unsigned b() ensures(result == 0u) { return 1u; }
int main() { return static_cast<int>(b()); }
CPP
reject remapped

# Valid declarations with the same displayed positions stay distinct too.
cat > "$run/valid.cpp" <<'CPP'
namespace A { law same() proves (0u == 0u); } namespace B { law same() proves (1u == 1u); }
law same(unsigned x) proves (x == x); law same(int x) proves (x == x);
#line 20 "same.cpp"
verified unsigned a() ensures(result == 7u) { return 7u; }
#line 20 "same.cpp"
verified unsigned b() ensures(result == 9u) { return 9u; }
#line 30 "same.cpp"
pure unsigned c() { return 11u; }
#line 30 "same.cpp"
pure unsigned d() { return 13u; }
law distinct() proves (c() + d() == 24u);
int main() { return a() == 7u && b() == 9u && c() == 11u && d() == 13u ? 0 : 1; }
CPP
for standard in c++17 c++20 c++23; do
    "$CPPL" "-std=$standard" --cppl-trust-report "$run/valid.cpp" -o "$run/valid" > "$run/valid.log" 2>&1
    grep -Eq 'Laws proven: +5$' "$run/valid.log"
    grep -Eq 'Function contracts proven: +2$' "$run/valid.log"
    grep -Eq 'Unresolved obligations: +0$' "$run/valid.log"
    "$run/valid"
done
echo 'physical declaration identity survives overloads, namespaces, macros, and line remapping'
