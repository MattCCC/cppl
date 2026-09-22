# docker/ci/Dockerfile
#
# Linux CI environment for C++L.
#
# One image serves both Linux compilers. GCC and Clang differ only in which
# compiler the preset selects, and the GitHub runner likewise installs LLVM on
# top of the image's GCC. Building one image keeps them from drifting apart and
# halves the local build time.
#
#   docker build -f docker/ci/Dockerfile -t cppl-ci-linux .
#
# The image holds no build policy. It installs a toolchain and nothing else;
# what gets compiled is decided by CMakePresets.json, which is mounted in at
# run time. See tools/ci/linux.sh.

FROM ubuntu:24.04

# The GitHub runner is ubuntu-latest, which installs LLVM from apt.llvm.org.
# The major version is pinned so that a local run does not silently test a
# different compiler than CI; it is the same value as LibClang_PREFERRED_MAJOR
# in cmake/FindLibClang.cmake.
ARG LLVM_MAJOR=22

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install --no-install-recommends --yes \
        ca-certificates \
        cmake \
        g++ \
        gcc \
        git \
        gnupg \
        lsb-release \
        make \
        ninja-build \
        software-properties-common \
        wget \
    && rm -rf /var/lib/apt/lists/*

# apt.llvm.org, exactly as .github/workflows/ci.yml installs it.
RUN wget -q https://apt.llvm.org/llvm.sh \
    && chmod +x llvm.sh \
    && ./llvm.sh "${LLVM_MAJOR}" \
    && apt-get install --no-install-recommends --yes \
        "clang-format-${LLVM_MAJOR}" \
        "clang-tidy-${LLVM_MAJOR}" \
        "clang-tools-${LLVM_MAJOR}" \
        "libclang-${LLVM_MAJOR}-dev" \
        "llvm-${LLVM_MAJOR}-dev" \
    && rm -rf /var/lib/apt/lists/* llvm.sh

# Read by the ci-linux-* presets. The environment supplies the path; no preset
# and no CMake file contains it. Derived from the ARG so the version is stated
# once.
ENV LLVM_ROOT=/usr/lib/llvm-${LLVM_MAJOR}

WORKDIR /workspace
