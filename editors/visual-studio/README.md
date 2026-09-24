# C++L for Visual Studio

A thin VSIX that runs [`cppl-lsp`](../../tools/cppl-lsp/README.md) through the
Visual Studio LSP client (`ILanguageClient`). It owns no C++L semantics.

Requires Visual Studio 2022 (17.x) on Windows.

## Install

From the Visual Studio Marketplace: *Extensions → Manage Extensions*, search
for **C++L**.

From a local build, run the produced `.vsix` under
`src/bin/Release/` and restart Visual Studio.

## Build

Needs the **Visual Studio extension development** workload.

```sh
msbuild editors/visual-studio/src/CpplVsix.csproj -t:Restore -p:Configuration=Release
msbuild editors/visual-studio/src/CpplVsix.csproj -p:Configuration=Release
```

Opening `src/CpplVsix.csproj` in Visual Studio and pressing F5 launches the
experimental instance with the extension loaded.

## Configuration

`cppl-lsp` is located by the `CPPL_LSP_PATH` environment variable if set,
otherwise `build\dev\bin\cppl-lsp.exe` under the open folder, otherwise
`cppl-lsp.exe` on `PATH`. Build it first with `make build`.

## What works

Diagnostics, formatting, format-on-save and code actions, all from the server,
for `*.cppl` files. Ordinary `.cpp` files keep their normal C++ tooling.

The server answers hover over any name -- what Clang knows of a C++ name, a
C++L declaration as written with what became of its obligations -- code lenses
stating each Law's, proof's and verified function's verdict, and completion
inside a `cases` or `decompose`
arm block, which Visual Studio shows where its LSP client supports those
features. Go To Definition is answered by Clang over the file's projection,
for C++ and for the C++L declarations the projection stands for. The server
also answers references, document highlights, workspace symbols and rename;
whether Visual Studio's LSP client asks for them has not been verified.

`*.cppl` files are colored by the shared C++L TextMate grammar
([`editors/shared/cppl.tmLanguage.json`](../shared/cppl.tmLanguage.json), the
same one VS Code uses). The VSIX ships it in a `Grammars` folder that
`Grammars.pkgdef` registers with Visual Studio's TextMate colorizer, and the
grammar's `fileTypes` associates it with `.cppl`. On top of it, the server
reports every name as a semantic token of what it names, and every C++L word as
a `keyword` token. Neither has been checked in a running Visual Studio, hence
"unverified" in [`editors/README.md`](../README.md).
