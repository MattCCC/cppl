#!/usr/bin/env bash
# cppl-format --check exits 0 on canonical input and nonzero on misformatted
# input, and -i rewrites misformatted input in place to exactly the canonical
# form --check then accepts (the CLI surface of compiler/formatter, the same
# engine cppl-lsp's textDocument/formatting uses).
set -euo pipefail

CPPL_FORMAT="$1"
FIXTURES="$2"
WORK="$3"

mkdir -p "$WORK"
run=$(mktemp -d "$WORK/format_check.XXXXXX")

# A canonical fixture must already pass --check.
cp "$FIXTURES/refinement_types.cpp" "$run/canonical.cpp"
if ! "$CPPL_FORMAT" --check "$run/canonical.cpp"; then
    echo "expected --check to accept an already-canonical fixture" >&2
    exit 1
fi

# A deliberately misformatted copy must be rejected...
cat > "$run/misformatted.cpp" <<'EOF'
law bounded(unsigned x) expects (x < 10u) proves (x + 1u <= 10u);
verified int f(int x) ensures (result >= 0) {
    return x;
}
type NonNegative = int where (self >= 0);
EOF

if "$CPPL_FORMAT" --check "$run/misformatted.cpp"; then
    echo "expected --check to reject misformatted input" >&2
    exit 1
fi

# ...and -i must rewrite it to something --check then accepts.
"$CPPL_FORMAT" -i "$run/misformatted.cpp"
if ! "$CPPL_FORMAT" --check "$run/misformatted.cpp"; then
    echo "expected --check to accept the file after -i" >&2
    exit 1
fi

# The refinement's inline `where` clause must survive untouched.
if ! grep -Fq 'type NonNegative = int where (self >= 0);' "$run/misformatted.cpp"; then
    echo "expected the refinement 'where' clause to remain inline" >&2
    exit 1
fi

# Formatting an already-canonical file a second time must be a no-op
# (idempotency, enforced at the CLI surface).
cp "$run/misformatted.cpp" "$run/misformatted.cpp.bak"
"$CPPL_FORMAT" -i "$run/misformatted.cpp"
if ! diff -q "$run/misformatted.cpp.bak" "$run/misformatted.cpp" > /dev/null; then
    echo "expected a second -i pass to be a no-op" >&2
    exit 1
fi

echo "cppl-format --check/-i behave correctly and are idempotent"
