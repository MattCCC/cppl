#!/usr/bin/env bash

set -euo pipefail

readonly RELEASE_BRANCH="main"
readonly REMOTE="origin"

VERIFY_ONLY=false
VERSION=""

fail() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

info() {
    printf '==> %s\n' "$*"
}

usage() {
    cat <<'EOF'
Usage:
  tools/release [--verify-only] VERSION

Examples:
  tools/release 0.3.0
  tools/release 0.4.0-rc.1
  tools/release --verify-only 0.3.0

Options:
  --verify-only   Run all release checks, build, tests, and packaging,
                  but do not create or push a Git tag.
  -h, --help      Show this help.
EOF
}

require_command() {
    command -v "$1" >/dev/null 2>&1 ||
        fail "required command not found: $1"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --verify-only)
            VERIFY_ONLY=true
            shift
            ;;

        -h|--help)
            usage
            exit 0
            ;;

        -*)
            fail "unknown option: $1"
            ;;

        *)
            if [[ -n "${VERSION}" ]]; then
                fail "multiple versions specified"
            fi

            VERSION="$1"
            shift
            ;;
    esac
done

require_command git
require_command cmake
require_command make

if [[ -z "${VERSION}" ]]; then
    fail "version is required (example: make release VERSION=0.3.0)"
fi

if [[ ! "${VERSION}" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z]+([.-][0-9A-Za-z]+)*)?$ ]]; then
    fail "invalid semantic version: ${VERSION}"
fi

readonly TAG="v${VERSION}"

readonly REPO_ROOT="$(
    git rev-parse --show-toplevel 2>/dev/null
)" || fail "not inside a Git repository"

cd "${REPO_ROOT}"

if [[ "${VERIFY_ONLY}" == true ]]; then
    info "Verifying C++L ${TAG}"
else
    info "Preparing C++L ${TAG}"
fi

# Releases are prepared only from main.
readonly CURRENT_BRANCH="$(git branch --show-current)"

if [[ "${CURRENT_BRANCH}" != "${RELEASE_BRANCH}" ]]; then
    fail "releases must be created from ${RELEASE_BRANCH}; current branch is ${CURRENT_BRANCH:-detached HEAD}"
fi

# Never validate/release with local modifications or untracked files.
if [[ -n "$(git status --porcelain)" ]]; then
    fail "working tree is not clean"
fi

# Ensure the configured remote exists.
git remote get-url "${REMOTE}" >/dev/null 2>&1 ||
    fail "Git remote '${REMOTE}' does not exist"

info "Fetching ${REMOTE}"

git fetch --prune --tags "${REMOTE}"

git rev-parse --verify "${REMOTE}/${RELEASE_BRANCH}" >/dev/null 2>&1 ||
    fail "${REMOTE}/${RELEASE_BRANCH} does not exist"

readonly LOCAL_HEAD="$(git rev-parse HEAD)"
readonly REMOTE_HEAD="$(git rev-parse "${REMOTE}/${RELEASE_BRANCH}")"

if [[ "${LOCAL_HEAD}" != "${REMOTE_HEAD}" ]]; then
    fail "local ${RELEASE_BRANCH} is not identical to ${REMOTE}/${RELEASE_BRANCH}"
fi

# A real release must use a new immutable version tag.
if [[ "${VERIFY_ONLY}" == false ]]; then
    if git rev-parse --verify --quiet "refs/tags/${TAG}" >/dev/null; then
        fail "tag ${TAG} already exists locally"
    fi

    if git ls-remote --exit-code --tags "${REMOTE}" "refs/tags/${TAG}" >/dev/null 2>&1; then
        fail "tag ${TAG} already exists on ${REMOTE}"
    fi
fi

info "Running repository checks"

make check

info "Running clean release workflow"

cmake -E remove_directory build/release
cmake --workflow --preset release

# Builds/checks must never mutate tracked source state.
if [[ -n "$(git status --porcelain)" ]]; then
    fail "release checks modified the working tree"
fi

if [[ "$(git rev-parse HEAD)" != "${LOCAL_HEAD}" ]]; then
    fail "HEAD changed during release validation"
fi

if [[ "${VERIFY_ONLY}" == true ]]; then
    printf '\n'
    printf 'C++L %s release verification succeeded.\n' "${TAG}"
    printf 'No Git tag was created or pushed.\n'
    exit 0
fi

info "Creating signed tag ${TAG}"

git tag \
    --sign \
    --message "C++L ${TAG}" \
    "${TAG}" \
    "${LOCAL_HEAD}"

info "Pushing ${TAG} to ${REMOTE}"

if ! git push "${REMOTE}" "refs/tags/${TAG}"; then
    printf 'ERROR: failed to push %s\n' "${TAG}" >&2
    printf 'Removing local tag %s\n' "${TAG}" >&2

    git tag --delete "${TAG}" >/dev/null

    exit 1
fi

printf '\n'
printf 'C++L %s released successfully.\n' "${TAG}"
printf 'GitHub Actions will build, test, package, attest, and publish the release.\n'