#!/usr/bin/env bash
# Targeted clang-tidy: analyze only files that differ from a base ref.
#
# `make lint`/`make tidy` run clang-tidy over the entire compilation
# database (correct for CI, too slow to run on every local edit). This
# script is invoked by the `lint-changed`/`tidy-changed` CMake targets
# (cmake/Linting.cmake) with the same run-clang-tidy driver, clang-tidy
# binary, and extra args CI uses, restricted to changed files.
#
# Usage (as called from CMake):
#   lint-changed.sh <run-clang-tidy> <build-dir> <clang-tidy> \
#       <clang-apply-replacements> <fix:0|1> <jobs> [extra-arg ...]
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

RUN_CLANG_TIDY="$1"
BUILD_DIR="$2"
CLANG_TIDY="$3"
CLANG_APPLY_REPLACEMENTS="$4"
FIX="$5"
JOBS="$6"
shift 6
EXTRA_ARGS=("$@")

BASE_REF="${BASE_REF:-$(git merge-base HEAD origin/main 2>/dev/null || git merge-base HEAD main 2>/dev/null || echo HEAD)}"

mapfile -t CHANGED_FILES < <(
	{
		git diff --name-only --diff-filter=ACMR "${BASE_REF}"
		git diff --name-only --diff-filter=ACMR
		git ls-files --others --exclude-standard
	} | grep -E '\.(c|cc|cpp|cxx|h|hh|hpp|hxx)$' | sort -u
)

if [ "${#CHANGED_FILES[@]}" -eq 0 ]; then
	echo "no changed C/C++ files vs ${BASE_REF}; nothing to lint"
	exit 0
fi

# Only files the compilation database lists. Every project header is listed
# as its own entry (cmake/HeaderChecks.cmake), so a changed header is
# analyzed as a main file, not only through the units that include it.
mapfile -t DB_FILES < <(
	python3 -c '
import json, sys
with open(sys.argv[1]) as f:
    print("\n".join(e["file"] for e in json.load(f)))
' "${BUILD_DIR}/compile_commands.json"
)

LINT_FILES=()
for f in "${CHANGED_FILES[@]}"; do
	[ -f "$f" ] || continue
	abs="$(cd "$(dirname "$f")" && pwd)/$(basename "$f")"
	for db_f in "${DB_FILES[@]}"; do
		if [ "$db_f" = "$abs" ]; then
			LINT_FILES+=("$f")
			break
		fi
	done
done

if [ "${#LINT_FILES[@]}" -eq 0 ]; then
	echo "changed C/C++ files are not in the compile database (fixtures or unowned files); nothing to lint"
	exit 0
fi

echo "linting ${#LINT_FILES[@]} changed file(s) vs ${BASE_REF}:"
printf '  %s\n' "${LINT_FILES[@]}"

FIX_ARGS=()
if [ "${FIX}" = "1" ]; then
	FIX_ARGS=(-fix -format)
fi

# Anchor each path as a regex so run-clang-tidy's positional file filters
# (regex-on-path) match only the intended files, not arbitrary substrings.
FILTERS=()
for f in "${LINT_FILES[@]}"; do
	FILTERS+=("$(printf '%s' "$f" | sed 's/[.[\*^$()+?{|]/\\&/g')\$")
done

exec "${RUN_CLANG_TIDY}" \
	-p "${BUILD_DIR}" \
	-j "${JOBS}" \
	-quiet \
	-hide-progress \
	-clang-tidy-binary "${CLANG_TIDY}" \
	-clang-apply-replacements-binary "${CLANG_APPLY_REPLACEMENTS}" \
	"${EXTRA_ARGS[@]}" \
	"${FIX_ARGS[@]}" \
	"${FILTERS[@]}"
