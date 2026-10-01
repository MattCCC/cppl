# Installing C++L

This guide covers building, installing and packaging the three tools a C++L
user runs, and what an installation depends on. What each tool does is in
[`README.md`](../README.md), [`tools/cppl-lsp/README.md`](../tools/cppl-lsp/README.md)
and [`tools/cppl-format/README.md`](../tools/cppl-format/README.md); what the
implementation provides is in [`STATUS.md`](./STATUS.md).

## What is installed

| Path              | What it is                                                      |
| ----------------- | --------------------------------------------------------------- |
| `bin/cppl`        | the compiler: `clang++`'s command line, plus `--cppl-*` options |
| `bin/cppl-lsp`    | the language server, over standard input and output             |
| `bin/cppl-format` | the canonical formatter                                         |
| `share/doc/cppl/` | this guide, the README and the licence                          |

Nothing else is installed: no library, header, model file or script. The
canonical formatting style is built into the formatter. Internal libraries,
tests and build-system artifacts stay in the build tree.

## Supported platform

Only what has been built and tested is supported. The matrix, and what has not
been tested, is in [`STATUS.md`](./STATUS.md), "V1 closure: delivery". In
short:

- **Tested:** Linux x86_64 (Ubuntu 24.04), with LLVM/Clang 22.1.8 and the
  libstdc++ of GCC 13.3, which that Clang selects, as the C++ standard library,
  in `c++17`, `c++20` and `c++23`.
- **Not tested here:** macOS, Windows, other architectures, other LLVM major
  versions, libc++ as the standard library of compiled programs. CI
  configurations exist for macOS and Windows (`docs/CI.md`); no release
  archive is produced for them.

## Requirements

An installed C++L is a client of one LLVM installation, the one it was built
against. It does not ship LLVM:

- `cppl` preprocesses and compiles with that installation's Clang driver, and
  `cppl-format` and `cppl-lsp` run its `clang-format`. Both are recorded by
  absolute path at build time.
- libclang, which resolves the C++ semantics of what is verified, is loaded
  through the runtime search path recorded in each executable: the directory
  the build linked it from.
- The C++ standard library and the linker are whatever that Clang driver uses
  on the host.

Moving that LLVM installation, or replacing it with another major version,
breaks an installation of C++L; installing C++L anywhere, or moving it
afterwards, does not. Where LLVM lives somewhere else, name it:

```sh
cppl --cppl-clang=/path/to/clang++ ...
cppl-lsp --clang /path/to/clang++
cppl-format --clang-format=/path/to/clang-format ...
LD_LIBRARY_PATH=/path/to/llvm/lib cppl ...     # libclang, on Linux
```

The Clang that analyses and the Clang that compiles must be one release: two
releases may resolve the same C++ differently, and nothing downstream would
notice. `cppl --cppl-version` names both and warns when they differ; that
configuration is not supported.

## Building from source

The build needs CMake 3.25 or later, Ninja and LLVM 22 with its development
files (libclang and `clang-c/Index.h`). The configuration is chosen by presets
(`docs/CI.md`); on a machine where LLVM is not the system compiler, name it:

```sh
export LLVM_ROOT=$(brew --prefix llvm@22)       # or /usr/lib/llvm-22
CC=$LLVM_ROOT/bin/clang CXX=$LLVM_ROOT/bin/clang++ cmake --preset release
cmake --build --preset release
ctest --preset release
```

## Installing

```sh
cmake --install build/release --prefix "$HOME/.local" --strip
```

`--strip` leaves out debug information, as the release archive does. The
installation is relocatable: it reads nothing from where it was installed, and
nothing from the source or build tree.

## Packaging and checksums

```sh
cmake --build --preset release --target package
```

writes `build/release/packages/cppl-<version>-<system>-<processor>.tar.gz`, and
beside it `<archive>.sha256`, the archive's SHA-256 in the form `sha256sum -c`
reads:

```sh
cd build/release/packages && sha256sum -c cppl-*.tar.gz.sha256
```

The archive is stripped and holds exactly what `cmake --install` installs.
`tools/release.sh --verify-only <version>` runs the release workflow from a
clean checkout of `main` and checks the record and every checksum.

The archive's bytes are not reproducible, since an archive records file times.
What is reproducible is the release record below.

`integration_installed_package` (`tests/integration/installed_package.sh`) is
the check of all of this: it installs the build to a fresh prefix, moves it,
and from an environment with nothing but a minimal `PATH` prints the record,
compiles ordinary C++ and verified C++L, has a false Law refused, formats a
file, and drives the language server through `initialize`, a refused document,
`shutdown` and `exit`; then it checks the archive against its checksum and the
install.

## Verifying a downloaded release

A published release carries, for each archive, its `.sha256` and a
`SHA256SUMS` over every file. The release workflow also attests each archive
(`.github/workflows/release.yml`, `actions/attest`). The attestation is a
signed statement, recorded by GitHub, that the archive was built by that
workflow from the tagged commit of this repository. Check both before
installing:

```sh
sha256sum -c cppl-<version>-Linux-x86_64.tar.gz.sha256
gh attestation verify cppl-<version>-Linux-x86_64.tar.gz --repo MattCCC/cppl
```

Then print the release record of the installed compiler (below) and compare
its `Source revision` and `Source tag` with the release.

The checksum says the archive is the one the release lists, and nothing more.
The attestation says which workflow run on which commit built it. Neither says
anything about what the compiler proves: that is the next section.

## What a release claims

A release claims, for the platform it names and nothing else, that its
compiler does what `STATUS.md` records as implemented, with the trust boundary
`TRUST.md` states. In particular:

- **What PROVEN means.** A claim reported PROVEN was accepted by the kernel the
  release record names, for exactly the proposition the claim states. It holds
  relative to everything the trust report lists for it: its trusted laws, the
  library models, the unsafe blocks, the imported contracts, and the runtime
  validations, which the program performs as its erasure keeps them.
- **The trust closure a user inherits.** A user of a verified program trusts:
  - the kernel;
  - the C++-to-proof translation, which is Clang's semantics, the bridge,
    elaboration, obligation construction and the erasure checker (`TRUST.md`
    7 to 17 and 29), none of it verified;
  - the Clang, linker and standard library that build and run the program;
  - the provenance of every verification interface a build imports, which
    nothing authenticates (`TRUST.md` 31.1).

  Every category of the closure appears in the report of each claim that rests
  on it. Nothing is trusted silently.
- **What it does not claim.** A release does not claim:
  - that C++ outside the verified subset (RFC 0022) is verified;
  - that the kernel's rules are mechanically proven sound: the Coq model proves
    a model of the checking judgment, with its normalization and arithmetic,
    sound (`KERNEL.md` 17), but the C++ kernel is not proven to implement it;
  - an ABI guarantee beyond Clang's own;
  - any platform other than Linux x86_64;
  - that the language or proof system is frozen.

  A 0.x release is experimental (`STATUS.md`, "Current release policy"). Until
  every gate of `ROADMAP.md`, "V1 release gates", passes, no release is V1.

## The release record

`cppl --cppl-version` prints what a build is traced by and what its
verification results are bound to (`TRUST.md` `TCB-REPRO-001`):

| Field                                        | Meaning                                                                        |
| -------------------------------------------- | ------------------------------------------------------------------------------ |
| `C++L compiler`                              | the compiler version                                                           |
| `Source revision`                            | the Git commit it was built from, or `unknown`                                 |
| `Source tag`                                 | the tag at that commit, or `none`                                              |
| `Source tree`                                | `clean`, or `modified` when a tracked file differed from the commit            |
| `Built with`                                 | the compiler that compiled `cppl`                                              |
| `Verification semantics`                     | the declared version of what a proven contract means (`SPEC.md` `TUBOUND-005`) |
| `Verifier-semantics digest`                  | the digest of the sources that implement it                                    |
| `Kernel version`, `Formal core version`      | the proof kernel and the calculus it checks                                    |
| `Interface format version`                   | the format of verification interfaces                                          |
| `Clang`                                      | the libclang that resolves C++ semantics                                       |
| `Clang driver`                               | the Clang that preprocesses and compiles                                       |
| `Target`, `C++ mode`, `C++ standard library` | what that driver selects for the arguments given                               |

Given arguments, it reports what they select: `cppl --cppl-version -std=c++20`
names `c++20`. No field names a path or a time, so a clean checkout of one
commit built against one toolchain prints the same record
(`tests/integration/release_metadata.sh`). When the Clang driver cannot be run,
the fields it would answer say `unavailable` and the command fails.

## Editors

Point an editor's language client at the installed `cppl-lsp`; `editors/`
holds the VS Code, Neovim, JetBrains and Visual Studio integrations and their
own installation notes. The server reads each document's flags from the
nearest `compile_commands.json` (`tools/cppl-lsp/README.md`).
