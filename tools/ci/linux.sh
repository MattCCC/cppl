#!/bin/sh
#
# Reproduce a Linux CI environment locally, through Docker.
#
# Usage:
#
#   tools/ci/linux.sh gcc
#   tools/ci/linux.sh clang
#   tools/ci/linux.sh quality
#   tools/ci/linux.sh asan
#
# Docker needs host-level commands, which is the only reason this script
# exists. It builds the image, mounts the repository, and runs the commands
# GitHub runs for that job (tools/ci/run-preset.sh). It contains no compiler
# flags and no test list -- a difference between this and the workflow would
# defeat its purpose.

set -eu

profile="${1:-}"

case "${profile}" in
    gcc)     preset=ci-linux-gcc ;;
    clang)   preset=ci-linux-clang ;;
    quality) preset=ci-quality ;;
    asan)    preset=ci-asan ;;
    ubsan)   preset=ci-ubsan ;;
    tsan)    preset=ci-tsan ;;
    "")
        echo "usage: tools/ci/linux.sh <gcc|clang|quality|asan|ubsan|tsan>" >&2
        exit 2
        ;;
    *)
        echo "tools/ci/linux.sh: unknown profile '${profile}'" >&2
        exit 2
        ;;
esac

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "${root}"

if ! command -v docker >/dev/null 2>&1; then
    echo "tools/ci/linux.sh: docker is not installed." >&2
    echo "Linux CI cannot be reproduced on this host." >&2
    exit 1
fi

if ! docker info >/dev/null 2>&1; then
    echo "tools/ci/linux.sh: the Docker daemon is not running." >&2
    echo "Linux CI cannot be reproduced until it is started." >&2
    exit 1
fi

image=cppl-ci-linux

docker build \
    --file docker/ci/Dockerfile \
    --tag "${image}" \
    .

# The build tree lives in a named volume rather than in the mount. A Linux
# build tree written into the host checkout would collide with the host's own
# build/ci directory, and CMake caches absolute paths, so the two would corrupt
# each other. The source is mounted read-write because the build is configured
# from it, but no CI build output reaches the host.
volume="cppl-ci-linux-build"

# Docker creates a named volume owned by root. The container runs as the
# invoking user -- so that anything written into the source mount stays owned
# by the developer -- which would otherwise leave the build tree unwritable.
# Ownership is fixed once, as root, before the build runs as the user.
docker volume create "${volume}" >/dev/null

docker run \
    --rm \
    --user 0:0 \
    --volume "${volume}:/build" \
    "${image}" \
    chown "$(id -u):$(id -g)" /build

exec docker run \
    --rm \
    --init \
    --user "$(id -u):$(id -g)" \
    --volume "${root}:/workspace" \
    --volume "${volume}:/workspace/build/ci" \
    --env "CPPL_CI_PRESET=${preset}" \
    --workdir /workspace \
    "${image}" \
    sh -c '
        set -eu
        cmake -E rm -rf "build/ci/${1#ci-}"
        sh tools/ci/run-preset.sh "$1"
    ' sh "${preset}"
