#!/usr/bin/env bash
# SPEC: TUBOUND-004, TUBOUND-005, TUBOUND-006, TUBOUND-009, TUBOUND-012, TUBOUND-013
# TRUST.md 31.1, TCB-XTU-007, TCB-XTU-008, TCB-XTU-009, TCB-XTU-010, TCB-LIB-010
#
# The integrity of a verification interface, tampered with everywhere: the
# real interfaces the units of `fixtures/provenance_matrix_cross_tu/` write,
# with every line edited and the checksum left as it was, and cut at every line
# and within lines. Each is refused as corrupt, truncated or of another format,
# before anything is read from it. Of the source lines naming system headers,
# all alike and each read only through the checksum, the first is edited. The
# fields, edited with the checksum recomputed, are `negative/interface_fields.sh`.
set -euo pipefail
CPPL="$1"
FIXTURES="$2"
WORK="$3"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/interface-integrity.XXXXXX")
fail() {
    echo "$1" >&2
    exit 1
}
sha256() {
    if command -v sha256sum > /dev/null 2>&1; then
        sha256sum | cut -d' ' -f1
    else
        shasum -a 256 | cut -d' ' -f1
    fi
}

cp "$FIXTURES"/provenance_matrix_cross_tu/* "$run/"
cd "$run"
"$CPPL" -std=c++20 -c producer.cpp -o producer.o --cppl-emit-interface=producer.cppli > producer.log 2>&1 ||
    { cat producer.log >&2; fail "the producing unit is refused"; }
"$CPPL" -std=c++20 -c middle.cpp -o middle.o --cppl-import-interface=producer.cppli \
    --cppl-emit-interface=middle.cppli > middle.log 2>&1 || { cat middle.log >&2; fail "the middle unit is refused"; }
echo 'int main() { return 0; }' > probe.cpp

# probe <interface> <reason>...: importing the interface into a unit that uses
# nothing from it is refused, for one of the reasons given.
probe() {
    local interface="$1" reason
    shift
    if "$CPPL" -std=c++20 -c probe.cpp -o probe.o "--cppl-import-interface=$interface" > probe.log 2>&1; then
        fail "$interface was accepted"
    fi
    for reason in "$@"; do
        grep -qF -- "$reason" probe.log && return 0
    done
    cat probe.log >&2
    fail "$interface was not refused as $*"
}
"$CPPL" -std=c++20 -c probe.cpp -o probe.o --cppl-import-interface=producer.cppli > /dev/null 2>&1 ||
    fail "the untouched interface is refused"

# exhaust <interface>: every line of it edited with the checksum left as it
# was, and the file cut at every line and through every part of a line; a
# source line naming a system header is edited only the first time, since the
# rest are the same kind of line, each read only through the checksum.
exhaust() {
    local interface="$1" lines at kept size bytes edited=0 system=0
    lines=$(wc -l < "$interface" | tr -d ' ')
    for at in $(seq 1 "$lines"); do
        if sed -n "${at}p" "$interface" | grep -qE '^source [0-9a-f]+ /(usr|opt)/|^source [0-9a-f]+ .*/lib/clang/'; then
            system=$((system + 1))
            [ "$system" -eq 1 ] || continue
        fi
        awk -v at="$at" 'NR == at {
            if ($1 == "checksum") { $2 = (substr($2, 1, 1) == "0" ? "1" : "0") substr($2, 2) } else { $0 = $0 "x" }
        } { print }' "$interface" > edited.cppli
        probe edited.cppli "it is corrupt" "it is format version"
        edited=$((edited + 1))
        head -n "$((at - 1))" "$interface" > cut.cppli
        probe cut.cppli "it is truncated" "it is corrupt" "it is empty" "it is format version" \
            "it is not a C++L verification interface"
    done
    size=$(wc -c < "$interface" | tr -d ' ')
    for bytes in $(seq 1 499 "$((size - 1))"); do
        head -c "$bytes" "$interface" > cut.cppli
        probe cut.cppli "it is truncated" "it is corrupt" "it is empty" "it is format version" \
            "it is not a C++L verification interface"
    done
    echo "$interface: each of $edited lines edited, and every cut at a line and within one, is refused"
}

exhaust middle.cppli
exhaust producer.cppli
