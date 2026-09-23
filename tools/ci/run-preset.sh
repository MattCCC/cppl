#!/bin/sh
#
# The commands one CI job runs for its preset, exactly as
# .github/workflows/ci.yml runs them.
#
#   tools/ci/run-preset.sh <preset>
#
# tools/ci/native.sh runs this on the host and tools/ci/linux.sh inside the
# Linux container, so a local run cannot drift from the workflow in either
# place. Every build-and-test job configures, builds and tests. The Quality
# job builds nothing: it checks the repository, configures, and runs the
# formatting check and static analysis over the compilation database.

set -eu

preset="${1:-}"

if [ -z "${preset}" ]; then
    echo "usage: tools/ci/run-preset.sh <preset>" >&2
    exit 2
fi

case "${preset}" in
    ci-quality)
        cmake -P cmake/ci/CheckHostPaths.cmake
        cmake -P cmake/ci/CheckPresetLayout.cmake
        cmake --preset ci-quality
        cmake --build --preset ci-quality --target format-check
        cmake --build --preset ci-quality --target lint
        ;;
    *)
        cmake --preset "${preset}"
        cmake --build --preset "${preset}"
        ctest --preset "${preset}"
        ;;
esac
