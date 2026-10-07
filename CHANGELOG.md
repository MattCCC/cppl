# Changelog

All notable changes to C++L are recorded here. A release's guarantees are
stated in [`docs/INSTALL.md`](docs/INSTALL.md), "What a release claims", and its
evidence in [`docs/STATUS.md`](docs/STATUS.md), "V1 closure".

## 1.0.0 — V1

The first stable release. Gates G1 to G17 of `docs/ROADMAP.md`, "V1 release
gates", pass on the release commit, and G18 completes when `release.yml` builds,
tests and attests the archive of the signed tag `v1.0.0` (`docs/STATUS.md`,
"V1 closure").

Stable means frozen: the language, the grammar and the kernel calculus change
only by an RFC and a new version. It does not mean production-ready, which the
README's status note withholds until the maintainer has reviewed the kernel.

### Release metadata

| Field | Value |
| --- | --- |
| C++L version | 1.0.0 |
| Kernel version | `cppl-kernel-0.9.0` |
| Formal-core version | `cppl-core-0.9.0` |
| Verification semantics | `cppl-verification-9` |
| Verification-interface format | version 3 |
| Supported C++ modes | `c++17`, `c++20`, `c++23` |
| Supported platform | Linux x86_64 (Ubuntu 24.04), LLVM/Clang 22.1.8 from apt.llvm.org, the libstdc++ of GCC 13.3 |
| Trusted components | the kernel; the C++-to-proof translation (Clang's semantics, the bridge, elaboration, obligation construction, the erasure checker); the Clang, linker and standard library that build and run the program; the provenance of imported verification interfaces (`docs/TRUST.md`) |
| ABI guarantee | none beyond Clang's own |

### Frozen

`docs/SPEC.md`, `docs/GRAMMAR.md` and `docs/KERNEL.md` are frozen at 1.0.0. Their
digests are recorded in `docs/STATUS.md`, "V1 closure: stability", and
`ci_frozendocuments` fails on any change to them that does not change the
recorded digest; such a change needs an RFC and a new version.

### What 1.0.0 provides

- **C++ first.** C++17, C++20 and C++23 keep their meaning. A C++L word used as
  a C++ name keeps its C++ meaning, and the compiler warns about it
  (`GRAMMAR.md`, WORD-001 to WORD-013). Code outside `verified` functions is
  compiled by Clang unchanged.
- **Kernel-checked proofs.** Every `PROVEN` claim comes from the kernel
  accepting exactly that goal. The kernel checks fifteen rules and adds no
  axiom. A Coq model of the checking judgment is proven sound and consistent,
  with no axiom and no admitted proof; the C++ kernel is tested against it, not
  proven to implement it (`KERNEL.md`, `tools/formal`).
- **Contracts.** `expects`, `ensures`, `decreases` and loop `invariant`s on
  verified functions, member functions and function templates. Contracts hold within a
  translation unit, and across units through checked verification interfaces.
- **Laws, induction, equality and termination.** `law` declarations with
  kernel-checked proofs, propositional `Eq<T>` with `rewrite`, and induction
  over unsigned machine integers. Recursion and loops need measures, and a
  total contract needs a termination proof.
- **Refinement types.** `type R = T where (...)`. A value enters a refinement
  either by a static proof or by an explicit `validate<R>(e)`, and a claim that
  rests on a validation names its site.
- **The verified C++ subset (RFC 0022).** Every construct of Annex X is
  classified: 96 are verified, each with a refused twin, and
  55 are refused, each with its diagnostic. The verified subset
  includes:
  - machine arithmetic, with its overflow, conversion and division rules;
  - references, modeled aliasing and the `std::vector`, `std::array` and
    `std::span` models;
  - whole struct values, copies and assignments, member arrays, and objects
    a reference parameter designates, followed member by member;
  - `&&`, `||` and `?:` as values, conditions and returns, each operand
    checked only where C++ evaluates it;
  - `if` statements, including init-statements and condition variables,
    `switch` statements with condition variables and fallthrough, and
    range-based `for` over arrays and the modeled containers;
  - default arguments, constant globals and enumerations.
- **Unsafe and trusted boundaries.** An `unsafe` block, a trusted law or a
  library model never counts as assumption-free. A call to a function whose
  unsafe code may write what it is handed is modeled as writing it.
- **Proof erasure.** Proof-only text is blanked and checked, and what remains
  is ordinary C++ that Clang compiles: in each standard mode the tests name, its
  assembly is that of a hand-erased twin at `-O0` and `-O2`.
- **Trust reporting.** Text and JSON reports, and the editor, show every
  claim's trust closure: trusted laws, library models, unsafe code, imported
  contracts with their provenance, and runtime validation sites.
- **Tooling.** `cppl`, the compiler, which takes `clang++`'s command line plus
  `--cppl-*` options; `cppl-format`, the canonical formatter; `cppl-lsp`, the
  language server; and editor support for VS Code, JetBrains IDEs, Visual
  Studio and Neovim (`editors/`).

### Known unsupported semantics

Each of these is refused wherever it would be used, so none can count as
verified by accident:

- existential quantification and its proof surface (`SPEC.md` 9);
- proof `let`;
- the proof-only `@` domains, conversions into them, and induction over them
  or over pointer structures;
- kernel type families;
- shifts and bitwise operators;
- concurrency, and pointer verification beyond the modeled references and
  views;
- base classes, unions and user-provided copy operations in verified struct
  values, and library types other than the modeled containers, such as
  `std::optional` and `std::string_view`;
- an element of a container read in a contract or an invariant other than an
  array element at a constant index: a sequence's specified value is its length
  (`SPEC.md` STDMODEL-012);
- a `switch` init-statement, which libclang does not expose, and `if
  consteval`;
- the constructs RFC 0022 lists as refused;
- a serialized proof-term format, and evidence identity in the trust report.

The Clang bridge, elaboration, obligation construction, the erasure checker and
the standard-library models are trusted, and none of them is verified
(`docs/TRUST.md`).

### Compatibility

- A verification interface written by an earlier build is refused: the
  verification semantics changed (`cppl-verification-9`).
