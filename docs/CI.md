# Local CI

Every check GitHub runs can be run here first.

The build system is the single source of truth. GitHub Actions is one consumer
of it, not its definition:

```
                    CMake / CTest
                         |
                  CMakePresets.json
                   /              \
           local execution     GitHub Actions
```

A workflow job provisions a runner, installs a compiler, and then calls the
same preset a developer calls. There is no second build configuration.

---

# 1. Everyday commands

| Command | What it does | When |
| --- | --- | --- |
| `make check` | Formatting, changed-file lint, build, tests. Incremental. | After an edit |
| `make ci` | The native CI profile for this host, from a clean tree | Before a push |
| `make ci-full` | Every CI environment available on this machine | Before a large push |

`make check` is the fast loop and is meant to stay that way. It reuses the
`dev` build tree and lints only what changed against `BASE_REF`. It does not
rebuild from scratch and does not run whole-repository static analysis; those
belong to `make ci`.

Use `make check-full` for the `check` set with whole-repository lint.

Every test runs under `tests/support/bounded.sh`, locally and on every runner:
no file a test writes may grow past 256 MiB and no process it starts may use
more than 1800 CPU seconds. A runaway writer is stopped with `SIGXFSZ` and a
spinning process with `SIGXCPU`, so a test that goes wrong fails rather than
filling the disk, and a process left behind by an interrupted run does not keep
going. `make test` runs CTest itself under the same limits, which caps the log
it keeps. Windows has no such limit to set, so there the CTest timeout is the
only bound.

---

# 2. What each CI preset corresponds to

Preset names are a stable, project-owned API. A workflow job and a developer
name the same one.

| Preset | GitHub job | Runs locally on |
| --- | --- | --- |
| `ci-quality` | Quality | Linux, macOS |
| `ci-linux-gcc` | Build & test / Linux GCC | Linux, or Docker |
| `ci-linux-clang` | Build & test / Linux Clang | Linux, or Docker |
| `ci-macos-llvm` | Build & test / macOS LLVM | macOS |
| `ci-windows-clang-cl` | Build & test / Windows Clang-CL | Windows |
| `ci-asan` | AddressSanitizer | Linux, macOS |
| `ci-ubsan` | UndefinedBehaviorSanitizer | Linux, macOS |
| `ci-tsan` | ThreadSanitizer (nightly) | Linux |

Every one of them is three commands:

```sh
cmake --preset ci-macos-llvm
cmake --build --preset ci-macos-llvm
ctest --preset ci-macos-llvm
```

Each preset builds in its own directory, `build/ci/<name>`, so no compiler ever
reads another compiler's cache. `cmake/ci/CheckPresetLayout.cmake` enforces
this.

---

# 3. Native profiles

## macOS

Needs Homebrew LLVM. AppleClang is not a substitute and the configuration will
not silently fall back to it.

```sh
brew install llvm@22 ninja
make ci
```

`tools/ci/native.sh` discovers the installation with `brew --prefix llvm@22`
and exports it as `LLVM_ROOT`. No Homebrew path is committed anywhere.

## Linux

```sh
make ci                      # native clang
./tools/ci/native.sh ci-linux-gcc
```

## Windows

Windows is tested on Windows. WSL, Wine and a Linux container are a different
compiler and a different ABI, so none of them count as Windows coverage.

Install once:

```powershell
choco install llvm --version=22.1.7
choco install ninja cmake
```

Then, from an "x64 Native Tools Command Prompt for VS 2022" (clang-cl needs the
MSVC headers, libraries and ABI):

```powershell
pwsh tools/ci/windows.ps1
```

or the presets directly:

```powershell
cmake --preset ci-windows-clang-cl
cmake --build --preset ci-windows-clang-cl
ctest --preset ci-windows-clang-cl
```

---

# 4. Linux through Docker

Docker reproduces the Linux compilers on a non-Linux host. It reproduces Linux
only -- it says nothing about macOS or Windows.

```sh
make ci-linux-gcc
make ci-linux-clang
```

The container runs the same presets as everything else:

```sh
cmake --preset ci-linux-gcc
cmake --build --preset ci-linux-gcc
ctest --preset ci-linux-gcc
```

`docker/ci/Dockerfile` installs the toolchain and nothing else. The LLVM major
version is pinned there so a local run does not test a materially different
compiler than the runner does.

The Linux build tree lives in a named Docker volume, not in the checkout, so it
cannot collide with the host's own `build/ci`.

---

# 5. Clean builds

CI presets always configure from nothing. A reused cache hides the defects this
system exists to find: a stale compiler path, a dependency that is only found
because a previous configure recorded it, a generated file no longer produced,
a source file missing from source control.

```sh
make ci-clean          # remove every CI build tree
./tools/ci/native.sh ci-macos-llvm --dirty   # keep the tree, for debugging
```

`--dirty` is for iterating on a failure. It is not CI parity.

---

# 6. Host-environment leakage

CMake folds `CFLAGS`, `CXXFLAGS`, `CPPFLAGS` and `LDFLAGS` into the build on the
first configure. A developer shell that exports one silently changes what gets
built. Both of these were found on a real machine, left behind by an unrelated
Homebrew package:

```
LDFLAGS=-L/opt/homebrew/opt/wxwidgets/lib
  -> ld: warning: search path '/opt/homebrew/opt/wxwidgets/lib' not found

CPPFLAGS=-I/opt/homebrew/opt/wxwidgets/include
  -> an include path the runner does not have, searched before the real ones
```

That is a build the runner never performs, and nothing in the repository
records it. CI presets therefore refuse to inherit these variables
(`cmake/ci/HostEnvironment.cmake`), and the launchers clear them.

Developer presets are unaffected; exporting `CXXFLAGS` to try something is
legitimate.

Two checks run without configuring anything:

```sh
cmake -P cmake/ci/CheckHostPaths.cmake     # committed machine-specific paths
cmake -P cmake/ci/CheckPresetLayout.cmake  # preset conventions
```

`CheckHostPaths` does not ban absolute paths. It requires that a toolchain
location be discovered (`brew --prefix`, `llvm-config`, `find_program`) or
supplied by the environment, rather than committed as an assumption.

---

# 7. Unavailable environments

`make ci-full` reports an environment it cannot run as `SKIPPED`, with the
reason, and never as a pass:

```
  PASS     ci-macos-llvm (native)
  PASS     ci-linux-gcc (docker)
  SKIPPED  ci-windows-clang-cl (needs a real Windows host)

Skipped environments were NOT verified on this host.
```

A green summary means the environments named in it actually ran.

---

# 8. Adding a compiler

1. Add a `ci-<name>` configure preset inheriting `ci-base`, with
   `binaryDir` set to `${sourceDir}/build/ci/<name>`.
2. Add build and test presets of the same name.
3. Add a matrix entry to `.github/workflows/ci.yml` that installs the compiler
   and calls that preset. The job gets no `-D` flags of its own.
4. If it is a Linux compiler, make sure `docker/ci/Dockerfile` provides it.
5. Run `cmake -P cmake/ci/CheckPresetLayout.cmake`.

Configuration that belongs in the preset, never in the workflow: build type,
warning policy, sanitizer selection, binary directory, project feature
switches, CTest configuration.

The workflow keeps only genuinely runner-specific setup: installing packages,
selecting a compiler, and exporting `LLVM_ROOT`.

---

# 9. Optional pre-push hook

Not installed automatically. If you want it:

```sh
printf '#!/bin/sh\nexec make ci\n' > .git/hooks/pre-push
chmod +x .git/hooks/pre-push
```

`make ci` is the native profile and takes a few minutes. `make ci-full` adds
the Docker Linux environments and takes considerably longer; it is a better fit
for a release branch than for every push.
