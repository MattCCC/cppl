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

A one-file change relinks every executable that depends on it, most of the
test binaries among them, so an incremental build is mostly linking. On Linux
the executables are therefore linked with LLVM's lld wherever the compiler
driver can find it (`cmake/Linker.cmake`): Clang finds `ld.lld` beside itself,
GCC on the `PATH`. It is probed at configure time, the banner names the linker
chosen, and a toolchain without lld keeps its default linker. lld reads the
same options GNU ld does, so the hardening `architecture_hardening` reads back
is unchanged. `-DCPPL_LINK_WITH_LLD=OFF` keeps the default linker. macOS and
Windows keep their platform linkers.

Lint covers headers as well as source files. Each header is in the compilation
database as a unit of its own, compiled with its owning component's flags, so
`misc-include-cleaner` holds it to the includes it states: it must compile
without its includers' help and directly include what it uses. `lint-changed`
therefore analyzes a changed header directly. The policy is in `AGENTS.md` §36.

Every test runs under `tests/support/bounded.sh`, locally and on every runner:
no file a test writes may grow past 256 MiB and no process it starts may use
more than 1800 CPU seconds. A runaway writer is stopped with `SIGXFSZ` and a
spinning process with `SIGXCPU`, so a test that goes wrong fails rather than
filling the disk, and a process left behind by an interrupted run does not keep
going. `make test` runs CTest itself under the same limits, which caps the log
it keeps. Windows has no such limit to set, so there the CTest timeout is the
only bound.

The heaviest script tests run their independent cases side by side through
`tests/support/parallel.sh`, one per online processor or `CPPL_TEST_JOBS` of
them (`CPPL_TEST_JOBS=1` runs them one at a time). A case has its own inputs and
outputs, so this changes only when each case runs: every case still runs, and
the test still fails on the first failing case in the order it names them, with
that case's own messages.

The test scripts, like those under `scripts/`, run under `set -euo pipefail`,
so they never pipe a producer into a consumer that may stop reading early:
`grep -q`, `grep -m`, `head`, `sed` with `q`, `awk` with `exit`, a `read` loop
that breaks, `cmp`. Once such a consumer exits, the producer's next write kills
it with `SIGPIPE`, and pipefail turns a match into a failure, or under `!` or
`&& fail` a failure into a pass. Whether there is a next write depends on how
much the producer writes, which for a compiler's macro list, a report, a log or
`nm` output differs from host to host, so the test fails on some machines and
loads and not others. Read the output whole first and match that:
`out=$(producer)` and then `grep -q pattern <<< "$out"`, or a file in the
test's own run directory where every byte counts, as it does for `cmp`. Where
the producer's own failure decided the outcome, keep it in the condition:
`if out=$(producer) && grep -q pattern <<< "$out"; then` holds exactly when the
pipeline did. A consumer reading a file argument itself, as in
`grep -m 1 pattern file`, is no pipeline. A pipeline whose status is discarded
on purpose, as in `diff a b | head -n 40 >&2 || true`, may be cut short.

---

# 2. What each CI preset corresponds to

Preset names are a stable, project-owned API. A workflow job and a developer
name the same one.

| Preset | GitHub job | Runs locally on |
| --- | --- | --- |
| `ci-quality` | Quality | Linux, or Docker; macOS for a native preview |
| `ci-linux-gcc` | Build & test / Linux GCC | Linux, or Docker |
| `ci-linux-clang` | Build & test / Linux Clang | Linux, or Docker |
| `ci-macos-llvm` | Build & test / macOS LLVM | macOS |
| `ci-windows-clang-cl` | Build & test / Windows Clang-CL | Windows |
| `ci-asan` | AddressSanitizer | Linux, macOS |
| `ci-ubsan` | UndefinedBehaviorSanitizer | Linux, macOS |
| `ci-tsan` | ThreadSanitizer (nightly) | Linux |
| `ci-fuzz` | Fuzzing | Linux, macOS (LLVM, not AppleClang) |

A sanitizer finding fails the run. ASan stops at its first report by default;
UBSan would print its report and let the program carry on, so a test that hit
undefined behavior would still pass, and it is built with
`-fno-sanitize-recover=undefined` to stop the same way.

Every build-and-test preset is three commands:

```sh
cmake --preset ci-macos-llvm
cmake --build --preset ci-macos-llvm
ctest --preset ci-macos-llvm
```

The Quality job builds nothing. It runs the repository checks, configures, and
runs the formatting check and static analysis:

```sh
cmake -P cmake/ci/CheckHostPaths.cmake
cmake -P cmake/ci/CheckPresetLayout.cmake
cmake --preset ci-quality
cmake --build --preset ci-quality --target format-check
cmake --build --preset ci-quality --target lint
```

`tools/ci/run-preset.sh` holds these commands, once, as the workflow runs them.
`tools/ci/native.sh` and `tools/ci/linux.sh` both run it, so neither a native
run nor a container run can do something different from GitHub. The
`ci_workflowsteps` test (`cmake/ci/CheckWorkflowSteps.cmake`) fails when the
script and `.github/workflows/ci.yml` disagree about any job's commands.

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
make ci-quality
```

`make ci-quality` runs the Quality job where GitHub runs it, on Linux: natively
on a Linux host, and through Docker anywhere else. Lint reads the system
headers, and Linux declares POSIX names in different internal headers than
macOS, so a macOS lint passing does not show the Linux one would.

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
cannot collide with the host's own `build/ci`. Each run has a volume of its own,
removed when the run ends, so two runs at once -- from two checkouts or two
sessions on one host -- never build into or delete each other's tree.

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

Four checks run without configuring anything, and each is also a CTest test (`ci_hostpaths`, `ci_presetlayout`, `ci_frozendocuments`, `ci_filelength`); `make ci-checks` runs all four:

```sh
cmake -P cmake/ci/CheckHostPaths.cmake        # committed machine-specific paths
cmake -P cmake/ci/CheckPresetLayout.cmake     # preset conventions
cmake -P cmake/ci/CheckFrozenDocuments.cmake  # a release's frozen documents
cmake -P cmake/ci/CheckFileLength.cmake       # no C++ file over 1000 lines
```

`CheckHostPaths` does not ban absolute paths. It requires that a toolchain
location be discovered (`brew --prefix`, `llvm-config`, `find_program`) or
supplied by the environment, rather than committed as an assumption.

`CheckFrozenDocuments` holds a released version to its freeze: from 1.0.0 on,
`docs/SPEC.md`, `docs/GRAMMAR.md` and `docs/KERNEL.md` must each state the
version `CMakeLists.txt` names and have the SHA-256 `docs/STATUS.md` records
for it, and `docs/STATUS.md` must say both are frozen. A change to a frozen
document is therefore a change to its recorded digest too, made in the same
commit and only by an RFC and a new version.

`CheckFileLength` holds every C++ source and header in the tree (`.cpp`, `.hpp`,
`.h`, `.cc`, `.cxx`, `.hxx`, `.ipp`, `.inl`, `.tpp`, tests and fixtures
included) to at most 1000 lines, and names each file over the limit with its
count (AGENTS.md 36). Build trees, `tmp/`, version control and tool state, and
installed dependencies are not counted, and no file is exempt.
`ci_filelength_fixtures` (`tests/ci/file_length.cmake`)
runs the check over trees built to show what it refuses and what it must not
count.

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

---

# 10. Hardening

The `release` and `dev` presets and every `ci-*` build-and-test preset set
`CPPL_ENABLE_HARDENING`, so the configuration GitHub tests on each push is the
one that ships. `cmake/Hardening.cmake` holds the flags, per platform:

| Mitigation | Linux (GCC, Clang) | macOS | Windows (clang-cl) |
| --- | --- | --- | --- |
| Stack canaries | `-fstack-protector-strong` | same | `/GS` (default) |
| Stack clash probing | `-fstack-clash-protection` | not available | default |
| Control-flow integrity | `-fcf-protection=full` (x86-64), `-mbranch-protection=standard` (AArch64) | arm64e only | `/guard:cf`, `/CETCOMPAT` |
| Checked libc calls | `_FORTIFY_SOURCE=3` outside Debug | libc default | n/a |
| Standard library assertions | `_GLIBCXX_ASSERTIONS` | `_LIBCPP_HARDENING_MODE_FAST` | n/a |
| Position-independent executable | `-fPIE -pie` | always | default |
| Read-only relocations, non-executable stack | `-z relro -z now -z noexecstack` | dyld default | default |

None of them changes what a correct program computes; they make a defect
harder to exploit and fix none. A sanitizer preset turns hardening off, and
configuring both is an error: fortified libc calls bypass what ASan
intercepts.

A flag that is set is not a mitigation that is present, so
`architecture_hardening` reads the linked `cppl`, `cppl-lsp`, `cppl-format` and
`cppl-spec-rules` with `llvm-readobj` and `llvm-nm`. It fails when an ELF file
is not PIE, lacks RELRO or BIND_NOW, has an executable stack, or never calls
`__stack_chk_fail`, and when a Mach-O file lacks `MH_PIE` or the stack
protector. A target that stops linking `cppl_project_options` loses its flags
without a build error; this is what notices. Windows reports its mitigations
in a form the test does not read yet, so there the flags are set but not
checked.

---

# 11. Fuzzing

The code that reads bytes from outside the process has fuzz targets in
`tests/fuzz`. Each states properties beyond "no crash and no sanitizer
report":

| Target | Reads | Properties |
| --- | --- | --- |
| `lsp_message` | the language server's standard input | only `json::Error` refuses a stream; no allocation above 16 MiB, whatever a header states |
| `lsp_json` | every JSON-RPC body | only `json::Error` refuses a body; what `dump` writes parses back to itself |
| `lsp_uri` | the `file://` URI of each document | a path holds no NUL; a path's URI converts back to the path |
| `frontend` | any source text: lex, recognize, project, erase | tokens tile the input; projection is deterministic and only blanks characters; erasure only deletes and keeps lines |
| `interface` | every verification interface a unit imports, as written and with its checksum recomputed | a refusal says why; only canonical text is accepted, so what was read is what the writer produces for it, and it reads back the same |
| `kernel_proof` | a derivation the bytes choose rule by rule, sometimes with a deliberate defect | the verdict is deterministic; an acceptance carries the goal; no accepted goal is false in any interpretation of the independent model |
| `kernel_terms` | a term the bytes choose | normalization keeps a term's type and value and is idempotent; substitution and shifting mean what the independent model evaluates them to; a budget refusal is not a finding |
| `kernel_arithmetic` | machine-integer facts and a goal, with evidence from the untrusted refutation search or automation, sometimes corrupted | those of `kernel_proof`: the kernel accepts no certificate for a false goal |
| `kernel_certificate` | an integer system and a certificate decoded directly | the verdict is deterministic; a refusal says why; an accepted certificate leaves no point of a searched box satisfying the system |

Each target runs two ways:

- Every build links it with `tests/fuzz/replay.cpp` and registers
  `fuzz_<name>_replay`, which runs it over `tests/fuzz/corpus/<name>/`. That
  happens on every compiler and under each sanitizer preset.
- `ci-fuzz` (the GitHub "Fuzzing" job, `make ci-fuzz`,
  `tools/ci/linux.sh fuzz`) links libFuzzer, builds only the targets, and runs
  `fuzz_<name>_search` under ASan and UBSan: `CPPL_FUZZ_RUNS` inputs (200000)
  from seed `CPPL_FUZZ_SEED` (1), 10 s per input, 2 GiB of memory. A fixed
  seed and count make a failure repeat when the run does; a change to the code
  changes the path the search takes. The work is fixed, not the time: a kernel
  search's inputs grow costlier as its corpus grows: on a 32-thread x86_64
  workstation `kernel_terms` took 513 s to 593 s and `kernel_proof` 265 s to
  797 s, depending on what else it ran. A search may take
  `CPPL_FUZZ_TIMEOUT` seconds (3600), and the preset runs four at a time.

When a search fails, libFuzzer writes the input under
`build/ci/fuzz/tests/fuzz/<name>/artifacts/`. Run it with
`build/ci/fuzz/bin/fuzz_<name> <file>`, shrink it with `-minimize_crash=1`,
fix the cause, and add the input to `tests/fuzz/corpus/<name>/` as
`regression-<what it broke>`: the replay test then holds the fix on every
compiler. What a search finds otherwise stays under `build/`.

For a longer run than CI's, call a target directly, for example
`build/ci/fuzz/bin/fuzz_lsp_json -max_total_time=600 -max_len=65536 <empty dir> tests/fuzz/corpus/lsp_json`.
A new target is a `<name>.cpp` defining `LLVMFuzzerTestOneInput` that states
its properties with `cppl::testing::fuzz::require`, seeds in
`corpus/<name>/`, and one `cppl_add_fuzz_target` line.

libFuzzer needs LLVM Clang: AppleClang ships without it, and `ci-fuzz` uses the
LLVM named by `LLVM_ROOT` (`tools/ci/native.sh` finds it). Windows has no
`ci-fuzz`; the replay tests still run there.

---

# 12. Static analysis

`lint` runs `.clang-tidy` over every translation unit and header, and every
finding is an error. Beyond the analyzer, `bugprone`, `cert`, `concurrency`,
`performance` and `portability`, it enforces the checks that guard ownership
(`owning-memory`, `no-malloc`), lifetime (`missing-std-forward`, the coroutine
capture checks, `misc-coroutine-hostile-raii`), bounds
(`pro-bounds-array-to-pointer-decay`), initialization (`init-variables`,
`pro-type-member-init`), exception safety (`bugprone-exception-escape`), C
varargs (`pro-type-vararg`), mutable globals and misleading or confusable
source text (`misc-misleading-bidirectional`, `misc-confusable-identifiers`).

A check is adopted by measuring it first, never by enabling it and suppressing
what it finds:

```sh
run-clang-tidy -p build/dev -checks='-*,<check>' -quiet
```

Its findings are fixed in the same change. Where a construct is genuinely
required, as in the crash handler's signal-safe globals, a
`NOLINTNEXTLINE(<check>)` names the check beside the comment stating why. The
checks measured and left off are listed, with the reason, at the top of
`.clang-tidy`.
