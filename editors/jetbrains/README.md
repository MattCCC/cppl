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
`*.cppl` files are registered as their own file type, and formatting is routed
exclusively to `cppl-lsp` rather than the IDE's C++ engine.

The server also answers hover and completion inside a `cases` or `decompose`
arm block, which the IDE shows where its LSP client supports those features.
Navigation is not implemented by the server yet, so it is unavailable here.

The plugin loads no grammar: all coloring is the server's. The server reports
only proof-statement keywords, as `keyword` semantic tokens, and whether this
IDE's LSP client applies them has not been verified.
