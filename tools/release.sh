#!/usr/bin/env bash

set -euo pipefail

readonly VERSION="${1:-}"
readonly RELEASE_BRANCH="main"
readonly REMOTE="origin"

fail() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

info() {
    printf '==> %s\n' "$*"
}

require_command() {
    command -v "$1" >/dev/null 2>&1 ||
        fail "required command not found: $1"
}

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

# Always operate from the repository root.
readonly REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null)" ||
    fail "not inside a Git repository"

cd "${REPO_ROOT}"

info "Preparing C++L ${TAG}"

# Release only from main.
readonly CURRENT_BRANCH="$(git branch --show-current)"

if [[ "${CURRENT_BRANCH}" != "${RELEASE_BRANCH}" ]]; then
    fail "releases must be created from ${RELEASE_BRANCH}; current branch is ${CURRENT_BRANCH:-detached HEAD}"
fi

# Never release with local modifications or untracked files.
if [[ -n "$(git status --porcelain)" ]]; then
    fail "working tree is not clean"
fi

# Ensure the expected remote exists.
git remote get-url "${REMOTE}" >/dev/null 2>&1 ||
    fail "Git remote '${REMOTE}' does not exist"

info "Fetching ${REMOTE}"

git fetch --prune "${REMOTE}"
git fetch --tags "${REMOTE}"

# Ensure local main is exactly the revision present on origin/main.
git rev-parse --verify "${REMOTE}/${RELEASE_BRANCH}" >/dev/null 2>&1 ||
    fail "${REMOTE}/${RELEASE_BRANCH} does not exist"

readonly LOCAL_HEAD="$(git rev-parse HEAD)"
readonly REMOTE_HEAD="$(git rev-parse "${REMOTE}/${RELEASE_BRANCH}")"

if [[ "${LOCAL_HEAD}" != "${REMOTE_HEAD}" ]]; then
    fail "local ${RELEASE_BRANCH} is not identical to ${REMOTE}/${RELEASE_BRANCH}"
fi

# Never overwrite or reuse a release tag.
if git rev-parse --verify --quiet "refs/tags/${TAG}" >/dev/null; then
    fail "tag ${TAG} already exists locally"
fi

if git ls-remote --exit-code --tags "${REMOTE}" "refs/tags/${TAG}" >/dev/null 2>&1; then
    fail "tag ${TAG} already exists on ${REMOTE}"
fi

info "Running repository checks"

make check

# make check validates the development configuration.
# A release must independently build and test the actual release preset.
info "Building and testing release configuration"

cmake --workflow --preset release

# Recheck the tree in case a validation/build step unexpectedly modified it.
if [[ -n "$(git status --porcelain)" ]]; then
    fail "release checks modified the working tree"
fi

# Ensure HEAD did not somehow change during validation.
if [[ "$(git rev-parse HEAD)" != "${LOCAL_HEAD}" ]]; then
    fail "HEAD changed during release validation"
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
