#!/usr/bin/env bash
# Invariants that must hold across whole classes of programs, not just the
# fixtures that happen to be written down (AGENTS.md 38, SPEC.md 42).
#
# Each check here is a property: it is run over a family of generated programs
# so that a regression shows up wherever it occurs, rather than only at the one
# argument a fixture pinned.
set -euo pipefail
CPPL="$1"
WORK="$3"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/template-identity.XXXXXX")

fail() {
    echo "$1" >&2
    exit 1
}

# Property: changing a proof-relevant template argument changes the obligation.
#
# `pick<N>` promises `result < 4` under `x < N`. That is true exactly when
# N <= 4. Every specialization at or below 4 must verify and every one above it
# must be refused, so the argument -- not the shared template source -- is what
# decides the outcome (SPEC.md TEMPLATE-001, TEMPLATE-003).
for n in 1 2 3 4 5 6 9 17; do
    name="pick_$n"
    cat > "$run/$name.cpp" <<CPP
template <unsigned N>
verified unsigned pick(unsigned x) expects (x < N) ensures (result < 4u) {
    return x;
}
int main() { return static_cast<int>(pick<${n}u>(0u)); }
CPP
    if "$CPPL" -std=c++17 "$run/$name.cpp" -o "$run/$name" > "$run/$name.log" 2>&1; then
        test "$n" -le 4 || fail "pick<$n> was accepted but 'result < 4' does not follow from 'x < $n'"
    else
        test "$n" -gt 4 || fail "pick<$n> was refused although 'x < $n' gives 'result < 4'"
    fi
done

# Property: a proven specialization never discharges an unproven one, whatever
# order they appear in. Both orders are compiled because a cache keyed on the
# template rather than the specialization would pass in one order only.
for order in 'good_first' 'bad_first'; do
    if [ "$order" = 'good_first' ]; then
        uses='pick<4u>(0u) + pick<9u>(0u)'
    else
        uses='pick<9u>(0u) + pick<4u>(0u)'
    fi
    cat > "$run/$order.cpp" <<CPP
template <unsigned N>
verified unsigned pick(unsigned x) expects (x < N) ensures (result < 4u) {
    return x;
}
int main() { return static_cast<int>($uses); }
CPP
    if "$CPPL" -std=c++17 "$run/$order.cpp" -o "$run/$order" > "$run/$order.log" 2>&1; then
        fail "a false specialization was accepted when compiled $order"
    fi
    test ! -e "$run/$order"
    grep -q 'does not satisfy its contract' "$run/$order.log" ||
        fail "wrong reason for the false specialization compiled $order"
done

# Property: an indexed refinement at one argument is not one at another, even
# though both erase to the same C++ type. `Index<4>` and `Index<8>` are
# different refinements, so a value of one does not enter the other by name.
cat > "$run/indexed_identity.cpp" <<'CPP'
type Index(unsigned n) = unsigned where (self < n);
verified Index<8u> widen(Index<4u> i) { return i; }
verified Index<4u> narrow(Index<8u> i) { return i; }
int main() { return 0; }
CPP
if "$CPPL" -std=c++17 "$run/indexed_identity.cpp" -o "$run/indexed_identity" \
    > "$run/indexed_identity.log" 2>&1; then
    fail "narrowing Index<8> to Index<4> was accepted"
fi
grep -Eq 'narrow' "$run/indexed_identity.log" ||
    fail "the refused narrowing was not reported against 'narrow'"

# Property: the bound a subscript owes is the extent that was stated, not any
# other. A capability of extent `k` admits an index proved below `k` and refuses
# one proved only below a larger bound, for every pair.
for extent in 2 4 8; do
    for bound in 2 4 8; do
        name="extent_${extent}_bound_${bound}"
        cat > "$run/$name.cpp" <<CPP
type Below = unsigned where (self < ${bound}u);
verified unsigned f(const unsigned* a, Below i)
    expects (readable(a, ${extent}u))
    ensures (result == result)
{
    return a[i];
}
int main() { return 0; }
CPP
        if "$CPPL" -std=c++17 -c "$run/$name.cpp" -o "$run/$name.o" > "$run/$name.log" 2>&1; then
            test "$bound" -le "$extent" ||
                fail "an index bounded by $bound was admitted into a region of extent $extent"
        else
            test "$bound" -gt "$extent" ||
                fail "an index bounded by $bound was refused in a region of extent $extent"
        fi
    done
done

# Property: a capability never supplies the bound by itself. For every extent,
# an index with no stated bound at all must be refused.
for extent in 1 4 64; do
    name="unbounded_$extent"
    cat > "$run/$name.cpp" <<CPP
verified unsigned f(const unsigned* a, unsigned i)
    expects (readable(a, ${extent}u))
    ensures (result == result)
{
    return a[i];
}
int main() { return 0; }
CPP
    if "$CPPL" -std=c++17 -c "$run/$name.cpp" -o "$run/$name.o" > "$run/$name.log" 2>&1; then
        fail "an unbounded index was admitted into a region of extent $extent"
    fi
    grep -q "element index' is not proven" "$run/$name.log" ||
        fail "the unbounded index at extent $extent was refused for the wrong reason"
done

# Property: erasure is unaffected by how many specializations were verified.
# The runtime program a template produces is the one Clang produces for the
# same C++, whatever C++L proved about it (SPEC.md 17.8, 18).
cat > "$run/erasure.cpp" <<'CPP'
template <unsigned N>
verified unsigned clamp_to(unsigned x) expects (x < N) ensures (result < N) {
    return x;
}
int main() { return static_cast<int>(clamp_to<4u>(3u) + clamp_to<8u>(7u)); }
CPP
"$CPPL" -std=c++17 -c "$run/erasure.cpp" -o "$run/erasure.o" \
    "--cppl-emit-projection=$run/erasure.runtime.cpp" > "$run/erasure.log" 2>&1 ||
    fail "the erasure program did not verify"
grep -q 'verified' "$run/erasure.runtime.cpp" && fail "'verified' survived into the runtime program"
grep -q 'expects\|ensures' "$run/erasure.runtime.cpp" && fail "a contract clause survived into the runtime program"
grep -q '__cppl_' "$run/erasure.runtime.cpp" && fail "a generated probe survived into the runtime program"

echo 'specialization identity, extents and erasure hold across argument families'
