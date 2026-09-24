#!/usr/bin/env bash
# Only an editor asks for a draft (ARCHITECTURE.md ARCH-LSP-007).
#
# A draft (RecognitionMode::Draft) keeps Laws and proofs not yet written whole
# and statements the recognizer could not read, so that an editor can ask where
# the author is. None of it has any authority: the compiler recognizes only
# whole text, so a draft never reaches elaboration, verification or a
# successful compile. The frontend defines drafts and the editor services
# consume them; nothing else may ask for one or read what C++L admits in one.
set -euo pipefail

cd "$1"

status=0
users=$(grep -rlE 'RecognitionMode::Draft|cppl/frontend/admissible\.hpp' \
    --include='*.cpp' --include='*.hpp' compiler clang kernel vir src tools) || status=$?
if [ "$status" -gt 1 ]; then
    echo "could not search the sources" >&2
    exit 1
fi

asked=""
for file in $users; do
    case "$file" in
        compiler/frontend/*|src/lsp/*) ;;
        *) asked="$asked$file"$'\n' ;;
    esac
done
if [ -n "$asked" ]; then
    echo "a draft is asked for outside the frontend and the editor services:" >&2
    printf '%s' "$asked" >&2
    exit 1
fi

echo "only the frontend and the editor services use drafts"
