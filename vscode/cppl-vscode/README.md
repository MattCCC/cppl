# cppl-vscode

A thin VS Code client for `cppl-lsp`, the C++L language server.

This extension owns no C++L semantics. It locates the `cppl-lsp` executable,
starts it as a child process speaking LSP over stdio, and registers itself
for the `cppl` language ID only. It does not attach to ordinary `.cpp` files;
see `tools/cppl-lsp/README.md` for the full architecture this client sits in
front of.

## Running it

There is no packaging or publishing step for this milestone. From the
repository root, open the Run and Debug view in VS Code, select "Launch
cppl-vscode Extension", and press F5. This opens an Extension Development
Host window with the extension active.

The repository's `.vscode/settings.json` already associates
`tests/fixtures/**/*.cpp` (and, for the future native extension, `*.cppl`)
with the `cppl` language, so opening a fixture in that window activates
`cppl-lsp` automatically.

## Configuration

| Setting                | Default                       | Meaning                                                           |
| ----------------------- | ------------------------------ | ------------------------------------------------------------------ |
| `cppl.serverPath`      | `build/dev/bin/cppl-lsp`      | Path to the `cppl-lsp` executable.                                |
| `cppl.clangPath`       | (cppl-lsp's own default)      | Clang driver executable, forwarded as `cppl-lsp --clang`.         |
| `cppl.clangArguments`  | `[]`                           | Extra flags forwarded to Clang, one `--clang-arg` per entry.      |

`cppl-lsp` must be built first (`make build` from the repository root); this
extension does not build it.
