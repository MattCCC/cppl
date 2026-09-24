# C++L for JetBrains IDEs

A thin plugin that runs [`cppl-lsp`](../../tools/cppl-lsp/README.md) through
the JetBrains platform LSP API. It owns no C++L semantics.

## Requirements

The platform LSP API is only available in the **paid** JetBrains IDEs
(CLion, IntelliJ IDEA Ultimate, and siblings) on 2023.2 or newer. It is not
available in Community editions.

`cppl-lsp` must be on `PATH` or built at `build/dev/bin/cppl-lsp` in the
project — run `make build` in the C++L checkout.

## Install

From the JetBrains Marketplace: search for **C++L** in
*Settings → Plugins → Marketplace*.

From a local build:

```sh
cd editors/jetbrains
gradle buildPlugin
```

Then *Settings → Plugins → ⚙ → Install Plugin from Disk…* and pick the zip
under `build/distributions/`.

## Develop

```sh
gradle runIde        # sandbox IDE with the plugin loaded
gradle buildPlugin   # produce the installable zip
gradle verifyPlugin  # platform compatibility checks
```

`gradle.properties` selects the target platform. It defaults to CLion
(`platformType=CL`), the natural host for a C++ superset; set `platformType=IU`
to build against IntelliJ IDEA Ultimate instead.

## Configuration

Server resolution matches the other clients: an explicit path wins, else the
project's own `build/dev/bin/cppl-lsp`, else `cppl-lsp` on `PATH`. The
settings are stored by `CpplSettings` (`cppl.xml`).

## What works

Diagnostics, formatting, format-on-save and code actions, all from the server.
Formatting of `*.cppl` files is routed exclusively to `cppl-lsp` rather than
the IDE's C++ engine.

`*.cppl` files are colored by the shared C++L TextMate grammar
([`editors/shared/cppl.tmLanguage.json`](../shared/cppl.tmLanguage.json), the
same one VS Code uses). The build copies it into the plugin as a TextMate
bundle, which the IDE's bundled TextMate support loads, and that support owns
the `.cppl` file type.

The server answers hover over any name -- what Clang knows of a C++ name, a
C++L declaration as written with what became of its obligations -- code lenses
stating each Law's, proof's and verified function's verdict, and completion
inside a `cases` or `decompose` arm block, which the IDE shows where its LSP
client supports those features.
Go to Declaration (Ctrl/Cmd-click) asks the server for the definition, which
Clang answers over the file's projection, for C++ and for the C++L declarations
the projection stands for. The server also answers references, document
highlights, workspace symbols and rename; whether this IDE's LSP client asks for
them has not been verified.

On top of the grammar, the server reports every name as a semantic token of
what it names, and every C++L word as a `keyword` token. Neither the grammar
bundle nor semantic tokens have been checked in a running IDE, hence
"unverified" in [`editors/README.md`](../README.md).
