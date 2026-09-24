# C++L for VS Code

A thin VS Code client for [`cppl-lsp`](../../tools/cppl-lsp/README.md), the
C++L language server.

This extension owns no C++L semantics. It locates the `cppl-lsp` executable,
starts it as a child process speaking LSP over stdio, and registers itself for
the `cppl` language ID. It attaches to ordinary `.cpp` files only when
`cppl.serveCpp` is on.

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
| `cppl.clangArguments` | `[]` | Extra flags for Clang, one `--clang-arg` per entry, after the flags the file's `compile_commands.json` entry gives it. |
| `cppl.serveCpp` | `false` | Also serve ordinary C++ files (the `cpp` language). See "Serving all C++" below. |
| `cppl.trace.server` | `off` | Trace JSON-RPC traffic to the output channel. |

Commands: **C++L: Restart Language Server** and **C++L: Show Language Server
Output**.

### Serving all C++

With `cppl.serveCpp` on, `cppl-lsp` also serves every `.cpp` and header file:

- diagnostics from the compiler;
- navigation, references, hover, completion, signature help, outline, folding
  and selection, all from Clang;
- formatting.

C++ is C++L with no Laws in it, so a C++L project can let one server read all
of its code.

The setting is off by default. Another C++ extension, such as Microsoft's
C/C++ or clangd, usually serves those files. Two servers on one file publish
two sets of diagnostics and answer every request twice. So when this is on,
turn the other extension's IntelliSense off for the files `cppl-lsp` serves
(for Microsoft's C/C++, `"C_Cpp.intelliSenseEngine": "disabled"`).

The server starts only once a file it serves is open. A workspace that never
opens a C++L file does not run it unless this setting is on.

### Build flags

Each file is read with the flags its build compiles it with, taken from the
nearest `compile_commands.json`. That file can be in the file's directory, in a
`build` directory beside it, or in any directory above. CMake writes it with
`-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`. `cppl.clangArguments` is only needed for
flags the build does not give.

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
where the grammar admits them. Typing `(` or `,` in a call shows the
signatures it could resolve to.

Go to Definition, Go to Declaration, Go to Type Definition and Go to
Implementations are answered by Clang over the file's projection, for C++ and
for the C++L declarations the projection stands for: a Law named in a proof, a
parameter named in a clause, a refinement type. Find All References covers
every open C++L file and the headers each includes, and the editor highlights
each occurrence of the name under the cursor, marking writes apart from reads.
The Outline view, the breadcrumbs and Go to Symbol in Editor
(`Ctrl+Shift+O`) list the file's C++ declarations and its Laws, proofs and
refinement types.

The folding arrows in the gutter come from the server when
`editor.foldingStrategy` is `auto`, the default. They fold:

- C++ bodies, `#include` runs and conditional branches;
- comment blocks;
- proof bodies and `cases` arms;
- Laws and refinement types that span several lines.

Inlay hints show, inside the text, the name of the parameter each argument is
passed to and the type each variable declared `auto` was deduced as.
`editor.inlayHints.enabled` turns them on and off. A call inside a Law's
proposition is annotated where the Law writes it. An argument that already
spells its parameter's name gets no hint.

Expand Selection and Shrink Selection (`Shift+Alt+Right` and `Shift+Alt+Left`)
grow a selection one construct at a time. For C++, that is what Clang parsed.
For C++L, it runs through a clause, a proof statement, an arm, a proof body and
the declaration.

Rename Symbol (`F2`) renames the name under the cursor in every file of the
workspace, showing first which name it would rewrite. A rename the server
cannot make whole is refused with its reason, and nothing is changed. Go to
Symbol in Workspace (`Ctrl+T`) searches every file's declarations.

The server's semantic tokens color every name by what Clang says it names, and
every C++L word and name by what the recognizer read. Semantic highlighting is
on by default for `cppl` files (`editor.semanticHighlighting.enabled`).

Names take the theme's own semantic colors: namespaces, types, functions,
variables, parameters, fields, enumerators, macros, and whether a name is
declared there, constant, a static member, deprecated, or from a system header.

C++L's words take the grammar's proof-keyword scope,
`keyword.other.proof.cppl`, so any theme colors them like the words the grammar
already colors. That includes proof statements spelled like C++ declarations,
such as `exact h;` and `contradiction name;`, which the grammar cannot tell
from ordinary C++.
