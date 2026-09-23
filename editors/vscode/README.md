# C++L for VS Code

A thin VS Code client for [`cppl-lsp`](../../tools/cppl-lsp/README.md), the
C++L language server.

This extension owns no C++L semantics. It locates the `cppl-lsp` executable,
starts it as a child process speaking LSP over stdio, and registers itself for
the `cppl` language ID only. It does not attach to ordinary `.cpp` files.

## Install

From the Marketplace (VS Code) or Open VSX (VSCodium): search for **C++L**.

`cppl-lsp` must be available: build it with `make build` in a C++L checkout,
or put it on your `PATH`.

## Develop against this repository

From the repository root, open the Run and Debug view, select "Launch
cppl-vscode Extension", and press F5. This opens an Extension Development Host
window with the extension active.

The repository's `.vscode/settings.json` associates
`tests/fixtures/**/*.cpp` (and `*.cppl`) with the `cppl` language, so opening a
fixture in that window activates `cppl-lsp` automatically.

To package it:

```sh
npm install
npm run package     # produces cppl.vsix
```

Packaging first copies the shared grammar from `editors/shared/` into
`syntaxes/`; that copy is generated and git-ignored, so edit the shared file
instead. See [`editors/README.md`](../README.md) for the publishing process.

## Configuration

| Setting | Default | Meaning |
| --- | --- | --- |
| `cppl.serverPath` | `build/dev/bin/cppl-lsp`, then `PATH` | Path to the `cppl-lsp` executable. |
| `cppl.clangPath` | (cppl-lsp's own default) | Clang driver, forwarded as `cppl-lsp --clang`. |
| `cppl.clangArguments` | `[]` | Extra flags for Clang, one `--clang-arg` per entry. |
| `cppl.trace.server` | `off` | Trace JSON-RPC traffic to the output channel. |

Commands: **C++L: Restart Language Server** and **C++L: Show Language Server
Output**.

## What works

Diagnostics, formatting, format-on-save, format-on-type and code actions, all
from the server. Format-on-save and format-on-type are enabled by default for
`cppl` files only; override them in your settings under `"[cppl]"`.

Hovering a name shows what Clang knows of it -- declaration, type, value, size,
its comment -- or, for a Law, a proof, a refinement type or a verified
function, the declaration as written with each obligation's status and goal. A
code lens over each Law, proof and verified function states its verdict:
`PROVEN`, `TRUSTED`, or `UNRESOLVED` and why (`editor.codeLens` turns lenses on
and off). Inside a `cases` or `decompose` arm block
hover shows the subject's states, and completion offers the arms still owed.
Everywhere else completion offers what Clang would accept, with a call's
parameters as tab stops, and C++L's declarations, proof statements and clauses
where the grammar admits them.

Go to Definition, Go to Declaration, Go to Type Definition and Go to
Implementations are answered by Clang over the file's projection, for C++ and
for the C++L declarations the projection stands for: a Law named in a proof, a
parameter named in a clause, a refinement type. Find All References covers
every open C++L file and the headers each includes, and the editor highlights
each occurrence of the name under the cursor, marking writes apart from reads.
Rename is not implemented by the server yet.

Proof statements spelled like C++ declarations, such as `exact h;` and
`contradiction name;`, are colored from the server's semantic tokens, since
the grammar cannot tell them apart from ordinary C++. They take the grammar's
own proof-keyword scope, `keyword.other.proof.cppl`, so any theme colors them
like the proof words it already colors, and semantic highlighting is on by
default for `cppl` files (`editor.semanticHighlighting.enabled`).
