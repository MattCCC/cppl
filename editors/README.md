# C++L editor integrations

Every editor here is a **thin client**. None of them implement C++L semantics:
they locate [`cppl-lsp`](../tools/cppl-lsp/README.md), start it over stdio, and
let it answer with diagnostics, canonical formatting and code actions. Syntax
coloring is the one exception — an editor must color tokens before any server
replies — so it comes from a grammar shared by all clients.

```text
editors/
├── shared/          TextMate grammar + its regression test (one source of truth)
├── vscode/          VS Code / VSCodium  → Marketplace + Open VSX
├── jetbrains/       CLion / IDEA        → JetBrains Marketplace
├── visual-studio/   Visual Studio       → Visual Studio Marketplace
└── neovim/          Neovim              → installed from this repository
```

## What you get

| Feature | Source | VS Code | JetBrains | Visual Studio | Neovim |
| --- | --- | :-: | :-: | :-: | :-: |
| Diagnostics (C++L + Clang) | `cppl-lsp` | yes | yes | yes | yes |
| Formatting | `cppl-lsp` | yes | yes | yes | yes |
| Format on save | editor config | yes | yes | yes | yes |
| Format on type | `cppl-lsp` | yes | yes | yes | unverified |
| Code actions | `cppl-lsp` | yes | yes | yes | yes |
| Hover | `cppl-lsp` | yes | unverified | yes | yes |
| Completion | `cppl-lsp` | yes | unverified | yes | yes |
| Signature help | `cppl-lsp` | yes | unverified | yes | yes |
| Go to definition | `cppl-lsp` | yes | yes | yes | yes |
| Declaration, type definition, implementation | `cppl-lsp` | yes | unverified | unverified | yes |
| Find references, highlight occurrences | `cppl-lsp` | yes | unverified | unverified | yes |
| Verification status (code lens) | `cppl-lsp` | yes | unverified | unverified | yes |
| Outline (document symbols) | `cppl-lsp` | yes | unverified | unverified | yes |
| Folding, expand selection | `cppl-lsp` | yes | unverified | unverified | unverified |
| Inlay hints (parameter names, deduced types) | `cppl-lsp` | yes | unverified | unverified | unverified |
| Build flags from `compile_commands.json` | `cppl-lsp` | yes | yes | yes | yes |
| Syntax coloring | `editors/shared` | yes | unverified | unverified | yes |
| Semantic coloring (every name, C++L words) | `cppl-lsp` | yes | unverified | unverified | unverified |

Code actions are syntax migrations, offered as quick fixes where they would
edit, and canonical formatting as `source.fixAll.cppl`. Completion offers what
Clang would accept for C++, C++L's declarations, statements and clauses where
the grammar admits them, and inside `cases` and `decompose` the arms still owed.

Hover shows what Clang knows of a C++ name, and a C++L declaration -- a Law, a
proof, a refinement type, a verified function's contract -- as it is written,
with what became of its obligations. A code lens over each Law, proof and
verified function states its verdict: `PROVEN`, `TRUSTED`, or `UNRESOLVED` and
why.
Navigation is answered by Clang over each document's projection, for ordinary
C++ and for the C++L declarations the projection stands for: a Law named in a
proof, a Law's parameter in its proposition, a function parameter in a
contract, a refinement type. The names proof statements use (`exact p;`,
`rewrite h;`, `contradiction e;`) lead where the compiler resolved them.
References cover every open document and the headers each includes.
"Unverified" means the IDE's own LSP client documents the request but nobody
has checked it against this server. Rename is **not** implemented yet. See
"Currently unsupported" in [`tools/cppl-lsp/README.md`](../tools/cppl-lsp/README.md).

### While typing

The server compiles in the background, a change once typing pauses, so
completion, hover and every other request are answered while a compile runs.
Diagnostics, verdicts and the coloring of claims follow when the compile
finishes. A client that shows progress shows each compile as it runs, and one
that supports refreshing re-fetches its code lenses and semantic tokens after
each compile.

### Across the workspace

The server indexes every source and header under the folders the editor
opened, in the background, and reads again what changes on disk. A workspace
symbol search (VS Code's Go to Symbol in Workspace, Neovim's
`vim.lsp.buf.workspace_symbol()`) lists declarations from every file, and find
references reaches files no open document includes. A file open in the editor
is answered as the editor holds it, saved or not.

### Build flags

The server reads each file with the flags its build compiles it with, taken
from the nearest `compile_commands.json`. That file can be in the file's
directory, in a `build` directory beside it, or in any directory above. Every
client gets this the same way, because the server finds the database itself.
An extension's own Clang-argument setting adds flags after the build's.

### Plain C++ files

Every client registers the server for the `cppl` language: `.cppl` files, and
whatever a project maps to that language. The server handles plain C++ exactly
as Clang does, since C++ is C++L with no Laws in it, so a C++L project can hand
it its `.cpp` files too. In VS Code, `cppl.serveCpp` does this for every C++
file. It is off by default, and no client claims C++ files on its own. Two
language servers on one file publish two diagnostic streams for it, so each
file should have one owner, and the project chooses it. Where `cppl-lsp` owns a
project's C++, turn off the other C++ extension's IntelliSense for those files.

The server's semantic tokens color every name by what it names, as Clang
resolved it: namespaces, types, functions, variables, parameters, fields,
enumerators and macros. They also color every C++L word and every name C++L
declares, as the recognizer read them. This is also how a proof statement
spelled like a C++ declaration, such as `exact h;` or `contradiction name;`, is
colored where the compiler read it as one: the grammar cannot tell. Literals,
comments and C++'s own keywords are left to the grammar.

## The shared grammar

[`shared/cppl.tmLanguage.json`](shared/cppl.tmLanguage.json) is the single
definition of C++L coloring. It follows the normative lexical rule in
[`docs/GRAMMAR.md`](../docs/GRAMMAR.md) §1: C++L words are *contextual*, so
`law`, `type`, `result` and friends are colored only where the grammar gives
them meaning and remain ordinary identifiers everywhere else.

Every client ships this one file rather than a copy of it:

- VS Code copies it in when the extension is packaged
  (`editors/vscode/scripts/sync-grammar.js`);
- JetBrains ships it as a TextMate bundle the build assembles;
- Visual Studio's VSIX links it into its `Grammars` folder;
- Neovim uses its own syntax file, which adds the same contextual words to the
  bundled C++ syntax.

That boundary is enforced by a test, not by inspection:

```sh
node editors/shared/test-grammar.js
```

It also runs under `ctest -R editors_grammar` and in CI. Edit the grammar and
this test together; `tests/fixtures/contextual_identifiers.cpp` is the fixture
it defends.

### The highlighter is not the language authority

```text
lexer / highlighter        ≠  language authority
parser + C++ semantic context  =  authority for contextual words
```

C++L reserves nothing. Every C++L word is *contextual*, so outside a
grammatical C++L construct the ordinary C++ reading wins (SPEC.md 3.1). The
grammar here therefore follows one rule when a construct is ambiguous:
**never mis-color valid C++.**

The sharpest case is a ghost local with no explicit type:

```cpp
struct ghost {};
ghost value;        // ordinary C++ — MUST stay ordinary C++
ghost int x = 1;    // C++L ghost local — unambiguous, and colored
int ghost = 1;      // ordinary identifier — MUST stay valid
```

`ghost value;` and a hypothetical C++L ghost-local declaration are spelled
identically. Resolving it requires the frontend's syntactic and semantic C++
context together with the §3.1 precedence rule — not a name lookup, since C++
declaration parsing is more context-dependent than "is this token currently a
known type name". A TextMate grammar has none of that, so it defers to the C++
reading. That is the conservative and correct behavior, not a language defect.
Where a statement of that kind is a proof statement, `cppl-lsp` colors it with
a semantic token taken from the compiler's own recognition, not from spelling.

**Do not "fix" this by making a contextual word globally special.** Doing so
would contradict the compatibility guarantee and would encourage the frontend
to drift toward keyword-like treatment. `tests/fixtures/contextual_identifiers.cpp`
pins all three shapes above for every contextual word, and is compiled and run
across C++17/20/23 by `conformance_contextual_identifiers`.

The Neovim syntax in [`neovim/syntax/cppl.vim`](neovim/syntax/cppl.vim)
applies the same contextual rules through Vim's regex engine, and likewise
sources the bundled C++ syntax rather than replacing it.

The VS Code extension cannot reference a file outside its own root, so
`npm run package` copies the grammar in via `scripts/sync-grammar.js`. The copy
under `editors/vscode/syntaxes/` is generated and git-ignored — never edit it.

---

## Publishing

Publishing is **tag-driven and automatic**. The
[`Editors`](../.github/workflows/editors.yml) workflow builds all four
integrations on every pull request;
[`Editors release`](../.github/workflows/editors-release.yml) reuses those same
jobs and turns on the publish steps when a tag is pushed:

```sh
git tag editors-v0.1.0
git push origin editors-v0.1.0
```

Editors use their own `editors-v*` tag series, separate from the compiler's
`v*.*.*` releases, so a client fix does not require a compiler release.

On a tag the workflow publishes VS Code to both the Visual Studio Marketplace
and Open VSX, the JetBrains plugin to the JetBrains Marketplace, and the VSIX
to the Visual Studio Marketplace. On a pull request it only builds and uploads
artifacts, so a packaging break is caught before release. Neovim needs no
publishing step — users install it straight from this repository.

### Before the first release

Bump the version in each manifest, then tag. Keep them in step:

| Editor | Version lives in |
| --- | --- |
| VS Code | `editors/vscode/package.json` → `version` |
| JetBrains | `editors/jetbrains/gradle.properties` → `pluginVersion` |
| Visual Studio | `editors/visual-studio/src/source.extension.vsixmanifest` → `Identity Version` |

The `publisher` fields (`cppl`, `dev.cppl.jetbrains`, the VSIX `Identity Id`)
are placeholders until the marketplace accounts exist; each marketplace must
have the publisher registered before its first publish succeeds.

### Required repository secrets

Publishing is skipped for any marketplace whose secret is absent, so you can
roll these out one at a time.

| Secret | Marketplace | How to get it |
| --- | --- | --- |
| `VSCE_PAT` | Visual Studio Marketplace (VS Code) | Azure DevOps PAT, scope *Marketplace → Manage* |
| `OVSX_PAT` | Open VSX | Access token from your open-vsx.org profile |
| `JETBRAINS_MARKETPLACE_TOKEN` | JetBrains Marketplace | Marketplace profile → *Tokens* |
| `JETBRAINS_CERTIFICATE_CHAIN` | JetBrains plugin signing | Signing certificate chain (PEM) |
| `JETBRAINS_PRIVATE_KEY` | JetBrains plugin signing | Signing private key (PEM) |
| `JETBRAINS_PRIVATE_KEY_PASSWORD` | JetBrains plugin signing | Password for that key |
| `VS_MARKETPLACE_TOKEN` | Visual Studio Marketplace (VSIX) | Azure DevOps PAT, scope *Marketplace → Publish* |

### Publishing by hand

Only needed when debugging a failed release.

```sh
# VS Code
cd editors/vscode && npm install && npm run package
VSCE_PAT=… npm run publish:vsce
OVSX_PAT=… npm run publish:ovsx

# JetBrains
cd editors/jetbrains && gradle buildPlugin
JETBRAINS_MARKETPLACE_TOKEN=… gradle publishPlugin

# Visual Studio (Windows)
msbuild editors/visual-studio/src/CpplVsix.csproj -p:Configuration=Release
```

---

## Local development

Each client resolves the server the same way, so a local build is picked up
with no configuration: an explicit setting wins, otherwise
`build/dev/bin/cppl-lsp` under the workspace root, otherwise `cppl-lsp` on
`PATH`. Build it first with `make build`.

Per-editor setup and configuration lives in each subdirectory's README:
[vscode](vscode/README.md), [jetbrains](jetbrains/README.md),
[visual-studio](visual-studio/README.md), [neovim](neovim/README.md).
