# cppl-format

`cppl-format` is the standalone command-line formatter for **C++L - C++ with
Laws**.

It is a thin CLI over `cppl::formatter` (`compiler/formatter`), the same
canonical-formatting engine `cppl-lsp` calls for
`textDocument/formatting`/`rangeFormatting`/`onTypeFormatting`. The CLI and
the language server share one engine and always agree: there is no
CLI-specific or editor-specific notion of canonical C++L formatting.

---

## What it formats

Ordinary C++ layout (indentation, line width, brace style, spacing) is
delegated entirely to `clang-format`, using the repository's `.clang-format`
by default. `cppl-format` does not reimplement any of that.

On top of `clang-format`'s output, `cppl-format` relocates C++L-specific
declaration clauses so each begins its own continuation line, indented one
level from the enclosing declaration or loop header, with the opening `{` on
its own separate line back at the declaration's column:

```cpp
// before
verified int fifty(int x) ensures (result == 50) {
    return 50;
}

// after
verified int fifty(int x)
    ensures (result == 50)
{
    return 50;
}
```

This applies to `expects`, `ensures`, `invariant`, and `proves`. There is no
whitespace between a clause keyword and its `(` — `expects` and friends are
not C++ control statements, so the repository's `SpaceBeforeParens:
ControlStatements` rule intentionally does not apply to them.

Refinement `where` clauses stay inline and are never relocated:

```cpp
type Percentage = int where (self >= 0 && self <= 100);
```

---

## How to build

`cppl-format` builds as part of the ordinary CMake project; there is no
separate build step.

```sh
make build
```

or, directly:

```sh
cmake --preset dev
cmake --build build/dev
```

The executable is produced at `build/dev/bin/cppl-format`.

---

## How to run

```sh
cppl-format [options] <file>...
```

By default, formatted output is written to stdout and the input files are
left untouched.

```text
-i, --in-place            Rewrite each file in place instead of printing to stdout.
--check                   Report formatting drift without writing anything;
                           exit nonzero if any input file is not canonical.
--clang-format=<path>     Use a specific clang-format executable instead of
                           the one selected at configure time.
```

`-i` and `--check` are mutually exclusive.

Examples:

```sh
# Print the canonical form of a file to stdout.
cppl-format path/to/file.cpp

# Reformat a file in place.
cppl-format -i path/to/file.cpp

# Verify a whole directory is canonically formatted (e.g. in CI);
# exits nonzero if anything would change.
cppl-format --check tests/fixtures/*.cpp

# Use a non-default clang-format binary.
cppl-format --clang-format=/opt/llvm/bin/clang-format -i path/to/file.cpp
```

Formatting is idempotent: running `cppl-format -i` twice on the same file
produces no further changes after the first pass.

---

## CI integration

`cppl-format --check` is folded into the repository's existing `format`/
`format-check` targets (`cmake/CpplFormatting.cmake`), alongside the
ordinary-C++ `clang-format` pass (`cmake/Formatting.cmake`):

```sh
make format          # rewrites both ordinary C++ and C++L-specific layout
make format-check     # fails if either layer has drifted
```

There is one command a developer or CI runs, not two separate formatters to
remember to invoke.

---

## How to run the tests

```sh
make test
```

or, to run only the formatter-related suites:

```sh
ctest --test-dir build/dev -R 'formatter_test|lsp_formatting_test|format_check'
```

- `formatter_test` exercises `compiler/formatter` directly: laws, verified
  functions, loops, refinements, proofs, range/on-type formatting,
  `check_style`, and an idempotency property test over the real fixture
  corpus under `tests/fixtures/`.
- `lsp_formatting_test` proves `cppl-lsp`'s formatting handlers and the
  shared engine produce byte-identical output over the same input.
- `format_check` (`tests/format/format_check.sh`) drives the installed
  `cppl-format` binary exactly as a developer or CI would, covering
  `--check`, `-i`, and idempotency.

---

## Relationship to `cppl-lsp`

`cppl-format` and `cppl-lsp` are two surfaces over one engine:

```text
                cppl::formatter (compiler/formatter)
               /                                    \
              /                                      \
             ▼                                        ▼
      cppl-format CLI                             cppl-lsp
   (--check / -i / stdout)          (textDocument/formatting, rangeFormatting,
                                      onTypeFormatting, publishDiagnostics
                                      style warnings)
```

Neither surface owns a second definition of what "canonical" means. New
formatting rules belong in `compiler/formatter`, not in the CLI or the LSP
individually.

See `tools/cppl-lsp/README.md` for the language server's formatting
capabilities, and `compiler/formatter/include/cppl/formatter/format.hpp` for
the engine's API.
