#!/usr/bin/env bash
# SPEC: TUBOUND-004, TUBOUND-005, TUBOUND-006, TUBOUND-009, TUBOUND-012, TUBOUND-013
# TRUST.md 31.1, TCB-XTU-007, TCB-XTU-008, TCB-XTU-009, TCB-XTU-010, TCB-LIB-010
#
# Every field of a verification interface, edited with the checksum recomputed
# as an edit that knows the format leaves it. The interfaces are the real ones
# the units of `fixtures/provenance_matrix_cross_tu/` write: a producer whose
# records rest on a trusted law, an unsafe block, a library model and a runtime
# validation site, and a middle unit whose records rest on the producer's.
#
# Each field the identity of a result is made of (the callable, the statement,
# the totality, and by identity each trusted law, model, unsafe block and
# validation site it rests on) makes every record proven through the edited one
# unusable, and so does every field of the configuration and staleness checks.
# Each field that is provenance only (a name, a contract's description, where a
# premise is reported, a predicate's text, the build, the producing unit, the
# recorded flags) leaves the interface usable (TCB-XTU-009).
#
# A field edit to the record a claim is proven through directly is not detected
# by itself; that is the stated limit of unauthenticated provenance, pinned by
# `negative/cross_tu.sh`. The integrity of the interface is
# `negative/interface_integrity.sh`.
set -euo pipefail
CPPL="$1"
FIXTURES="$2"
WORK="$3"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/interface-fields.XXXXXX")
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
cp producer.cppli resealed.cppli
"$CPPL" -std=c++20 -c client.cpp -o client.o --cppl-import-interface=resealed.cppli \
    --cppl-import-interface=middle.cppli --cppl-trust-report > client.log 2>&1 ||
    { cat client.log >&2; fail "the client is refused with the untouched interfaces"; }

# --- Every field edited, the checksum recomputed ----------------------------
# reseal <entry or -> <field> <variant>: producer.cppli with the first <field>
# line (of that entry, or of the header) edited as <variant> says, and its
# checksum recomputed.
reseal() {
    sed '$d' producer.cppli | awk -v entry="$1" -v field="$2" -v variant="$3" '
        function flip(hex) { return (substr(hex, 1, 1) == "0" ? "1" : "0") substr(hex, 2) }
        $1 == "entry" { current = $2 }
        !done && $1 == field && (entry == "-" || current == entry) {
            done = 1
            if (variant == "removed") next
            if (variant == "identity" || variant == "hash" || (variant == "value" && (field == "build" || field == "verifier"))) $2 = flip($2)
            else if (variant == "symbol") $2 = $2 "x"
            else if (variant == "version") $2 = "4"
            else if (variant == "path") $3 = $3 ".moved"
            else if (variant == "value" && field == "name") $2 = "renamed"
            else if (variant == "value" && field == "contract") $2 = "reworded"
            else if (variant == "value" && field == "status") $2 = "trusted"
            else if (variant == "value" && field == "correctness") $2 = ($2 == "total" ? "partial" : "total")
            else if (variant == "value") $2 = "other"
            else if (variant == "line") { if (field == "premise") $3 = $3 + 1; else $2 = $2 + 1 }
            else if (variant == "column") $3 = $3 + 1
            else if (variant == "file") $4 = "other.cpp"
            else if (variant == "name" && field == "premise") $5 = "renamed"
            else if (variant == "name" && field == "model") $3 = "renamed%20model"
            else if (variant == "refinement") $5 = "Renamed"
            else if (variant == "predicate") $6 = "(self%20>=%201)"
            else { print "no edit " field " " variant > "/dev/stderr"; exit 1 }
        }
        { print }
        END { if (!done) { print "no " field " line in " entry > "/dev/stderr"; exit 1 } }
    ' > resealed.body
    sed '$d' producer.cppli | cmp -s - resealed.body && fail "the edit $1 $2 $3 changed nothing"
    { cat resealed.body; printf 'checksum %s\n' "$(sha256 < resealed.body)"; } > resealed.cppli
}

# client: the client unit, importing the edited producer interface and the
# middle unit's, whose records rest on the producer's.
client() {
    "$CPPL" -std=c++20 -c client.cpp -o client.o --cppl-import-interface=resealed.cppli \
        --cppl-import-interface=middle.cppli --cppl-trust-report > client.log 2>&1
}

edits=0
while IFS='|' read -r entry field variant outcome; do
    [ -n "$entry" ] || continue
    reseal "$entry" "$field" "$variant"
    edits=$((edits + 1))
    if [ "$outcome" = accepted ]; then
        client || { cat client.log >&2; fail "editing $field ($variant) of $entry, which is provenance, made the interface unusable"; }
        grep -Eq '^Function contracts imported: +8$' client.log ||
            { cat client.log >&2; fail "the client does not use every record after editing $field ($variant) of $entry"; }
    else
        client && { cat client.log >&2; fail "editing $field ($variant) of $entry was not detected"; }
        if [ "$outcome" = transitive ]; then
            grep -qF "it was proven through the contract of '$entry', and no imported interface records that contract as it was" client.log ||
                { cat client.log >&2; fail "editing $field ($variant) of $entry left a record proven through it usable"; }
        else
            grep -qF -- "$outcome" client.log ||
                { cat client.log >&2; fail "editing $field ($variant) of $entry was not refused as: $outcome"; }
        fi
    fi
done <<'EDITS'
-|cppl-verification-interface|version|it is format version '4', and this compiler reads only version 3
-|compiler|value|it was produced by another C++L compiler version
-|build|value|accepted
-|semantics|value|it was verified under other verification semantics
-|verifier|value|it was produced by a verifier built from other semantic sources
-|kernel|value|it was checked by another proof kernel
-|core|value|it was stated in another formal core
-|clang|value|its C++ semantics were resolved by another Clang
-|language|value|it was produced in another C++ language mode
-|target|value|it was produced for another target
-|flag|value|accepted
-|unit|value|accepted
-|source|hash|it is stale
-|source|path|it is stale
c:@F@imported_model#|entry|symbol|transitive
c:@F@imported_model#|name|value|accepted
c:@F@imported_model#|statement|identity|transitive
c:@F@imported_model#|contract|value|accepted
c:@F@imported_model#|status|value|records status 'trusted'; only a proven contract is record
c:@F@imported_model#|correctness|value|transitive
c:@F@imported_model#|model|identity|rests on a library model this compiler does not have
c:@F@imported_model#|model|name|rests on a library model this compiler does not have
c:@F@imported_model#|model|removed|transitive
c:@F@imported_plain#i#|entry|symbol|transitive
c:@F@imported_plain#i#|name|value|accepted
c:@F@imported_plain#i#|statement|identity|transitive
c:@F@imported_plain#i#|contract|value|accepted
c:@F@imported_plain#i#|status|value|records status 'trusted'; only a proven contract is record
c:@F@imported_plain#i#|correctness|value|transitive
c:@F@imported_trusted#i#|entry|symbol|transitive
c:@F@imported_trusted#i#|name|value|accepted
c:@F@imported_trusted#i#|statement|identity|transitive
c:@F@imported_trusted#i#|contract|value|accepted
c:@F@imported_trusted#i#|status|value|records status 'trusted'; only a proven contract is record
c:@F@imported_trusted#i#|correctness|value|transitive
c:@F@imported_trusted#i#|premise|identity|transitive
c:@F@imported_trusted#i#|premise|line|accepted
c:@F@imported_trusted#i#|premise|file|accepted
c:@F@imported_trusted#i#|premise|name|accepted
c:@F@imported_trusted#i#|premise|removed|transitive
c:@F@imported_unsafe#i#|entry|symbol|transitive
c:@F@imported_unsafe#i#|name|value|accepted
c:@F@imported_unsafe#i#|statement|identity|transitive
c:@F@imported_unsafe#i#|contract|value|accepted
c:@F@imported_unsafe#i#|status|value|records status 'trusted'; only a proven contract is record
c:@F@imported_unsafe#i#|correctness|value|transitive
c:@F@imported_unsafe#i#|unsafe|line|transitive
c:@F@imported_unsafe#i#|unsafe|column|transitive
c:@F@imported_unsafe#i#|unsafe|file|transitive
c:@F@imported_unsafe#i#|unsafe|removed|transitive
c:@F@imported_validation#I#|entry|symbol|transitive
c:@F@imported_validation#I#|name|value|accepted
c:@F@imported_validation#I#|statement|identity|transitive
c:@F@imported_validation#I#|contract|value|accepted
c:@F@imported_validation#I#|status|value|records status 'trusted'; only a proven contract is record
c:@F@imported_validation#I#|correctness|value|transitive
c:@F@imported_validation#I#|runtime|line|transitive
c:@F@imported_validation#I#|runtime|file|transitive
c:@F@imported_validation#I#|runtime|refinement|transitive
c:@F@imported_validation#I#|runtime|predicate|accepted
c:@F@imported_validation#I#|runtime|removed|transitive
EDITS
echo "every one of $edits field edits is refused where it changes what was verified, and only there"
