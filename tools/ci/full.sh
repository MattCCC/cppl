#!/bin/sh
#
# Run every CI environment this host can actually provide.
#
#   make ci-full
#
# An environment that cannot run here is reported SKIPPED, with the reason, and
# never counted as a pass. A green summary from this script means the
# environments named in it genuinely ran -- nothing else.
#
# Windows is never run from a Unix host. WSL, Wine and a Linux container are
# not the Windows compiler or the Windows ABI, so claiming Windows coverage
# from here would be false. See docs/DEVELOPER_GUIDE.md.

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "${root}"

host=$(uname -s)

# Records are newline-separated because a name contains spaces
# ("ci-macos-llvm (native)"); splitting on whitespace would tear it apart.
passed=""
failed=""
skipped=""

record_pass() { passed="${passed}$1
"; }

record_fail() { failed="${failed}$1
"; }

record_skip() { skipped="${skipped}$1:$2
"; }

# Every environment runs even after one fails, so a single summary reports the
# whole matrix. `if` keeps the failure from tripping `set -e`.
run() {
    name="$1"
    shift

    printf '\n=== %s ===\n\n' "${name}"

    if "$@"; then
        record_pass "${name}"
    else
        record_fail "${name}"
    fi
}

# -----------------------------------------------------------------------------
# Repository checks
# -----------------------------------------------------------------------------

run "repository-checks" sh -c '
    cmake -P cmake/ci/CheckHostPaths.cmake
    cmake -P cmake/ci/CheckPresetLayout.cmake
'

# -----------------------------------------------------------------------------
# Native
# -----------------------------------------------------------------------------

case "${host}" in
    Darwin) native_preset=ci-macos-llvm ;;
    Linux)  native_preset=ci-linux-clang ;;
    *)      native_preset="" ;;
esac

if [ -n "${native_preset}" ]; then
    run "${native_preset} (native)" ./tools/ci/native.sh "${native_preset}"
    # GitHub runs the Quality job on Linux only: its lint reads the Linux
    # system headers, so another host runs it through Docker below.
    if [ "${host}" = "Linux" ]; then
        run "ci-quality (native)" ./tools/ci/native.sh ci-quality
    fi
    run "ci-asan (native)" ./tools/ci/native.sh ci-asan
    run "ci-ubsan (native)" ./tools/ci/native.sh ci-ubsan
else
    record_skip "native" "unsupported host '${host}'"
fi

# -----------------------------------------------------------------------------
# Linux, through Docker
# -----------------------------------------------------------------------------

docker_reason=""

if ! command -v docker >/dev/null 2>&1; then
    docker_reason="docker is not installed"
elif ! docker info >/dev/null 2>&1; then
    docker_reason="the Docker daemon is not running"
fi

if [ -n "${docker_reason}" ]; then
    record_skip "ci-linux-gcc" "${docker_reason}"
    record_skip "ci-linux-clang" "${docker_reason}"
    if [ "${host}" != "Linux" ]; then
        record_skip "ci-quality" "${docker_reason}"
    fi
elif [ "${host}" = "Linux" ]; then
    # Already covered natively above; a container would retest the same ABI.
    run "ci-linux-gcc (docker)" ./tools/ci/linux.sh gcc
else
    run "ci-linux-gcc (docker)" ./tools/ci/linux.sh gcc
    run "ci-linux-clang (docker)" ./tools/ci/linux.sh clang
    run "ci-quality (docker)" ./tools/ci/linux.sh quality
fi

# -----------------------------------------------------------------------------
# Windows
# -----------------------------------------------------------------------------

if [ "${host}" = "Windows_NT" ]; then
    run "ci-windows-clang-cl" ./tools/ci/native.sh ci-windows-clang-cl
else
    record_skip "ci-windows-clang-cl" \
        "needs a real Windows host; run tools/ci/windows.ps1 there"
fi

# -----------------------------------------------------------------------------
# Summary
# -----------------------------------------------------------------------------

printf '\n'
printf '=================================\n'
printf ' Local CI summary\n'
printf '=================================\n\n'

printf '%s' "${passed}" | while IFS= read -r entry; do
    [ -n "${entry}" ] && printf '  PASS     %s\n' "${entry}"
done

printf '%s' "${failed}" | while IFS= read -r entry; do
    [ -n "${entry}" ] && printf '  FAIL     %s\n' "${entry}"
done

printf '%s' "${skipped}" | while IFS= read -r entry; do
    [ -z "${entry}" ] && continue
    printf '  SKIPPED  %s (%s)\n' "${entry%%:*}" "${entry#*:}"
done

printf '\n'

if [ -n "${skipped}" ]; then
    printf 'Skipped environments were NOT verified on this host.\n'
fi

if [ -n "${failed}" ]; then
    printf '\nLocal CI failed.\n'
    exit 1
fi

printf 'Local CI passed for every environment available here.\n'
