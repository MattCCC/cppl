# cppl-lsp

`cppl-lsp` is the language server for **C++L - C++ with Laws**.

It provides editor tooling for C++L while preserving the fundamental C++L architecture:

> `cppl-lsp` owns the whole source file and internally delegates ordinary C++ semantic work to Clang/clangd-compatible infrastructure.

The editor talks to one language server: `cppl-lsp`.

`cppl-lsp` understands C++L extensions, proofs, verification, source projection, and C++L diagnostics. Ordinary C++ parsing, typing, lookup, overload resolution, templates, completion, navigation, and related compiler semantics remain the responsibility of Clang.

The goal is not to build another C++ language server.

The goal is to make C++L feel like a native extension of C++.

---

## Architecture

```text
                    Editor
                      │
                      │ LSP
                      ▼
                 ┌──────────┐
                 │ cppl-lsp │
                 └────┬─────┘
                      │
          ┌───────────┴───────────┐
          │                       │
          ▼                       ▼
   C++L frontend            C++ semantics
                           Clang / clangd
          │                       ▲
          │                       │
          ├─ parse C++L           │
          ├─ laws / proofs        │
          ├─ obligations          │
          ├─ verification         │
          ├─ erasure/projection ──┘
          └─ source mapping
```

`cppl-lsp` is the owner of the document from the editor's point of view.

Clang is the authority for ordinary C++ semantics.

The C++L compiler/frontend is the authority for C++L semantics.

The language server coordinates the two.

---

## Core principle

C++L is a superset of C++.

Therefore ordinary C++ must not require a second implementation inside the language server.

For ordinary C++:

```text
C++ source
    │
    ▼
cppl-lsp
    │
    ▼
Clang-compatible semantic engine
```

For C++L:

```text
C++L source
    │
    ▼
C++L frontend
    │
    ├── verification
    │
    ├── C++L diagnostics
    │
    ├── source map
    │
    ▼
projected / erased C++
    │
    ▼
Clang-compatible semantic engine
```

Results produced against projected C++ are mapped back to locations in the original C++L source before being returned to the editor.

---

## Why `cppl-lsp` owns the whole file

The editor should not have to understand which portions of a file belong to C++ and which belong to C++L.

Running independent C++ and C++L language servers over the same document would create competing views of the source:

- duplicate diagnostics;
- invalid diagnostics on C++L syntax;
- inconsistent source locations;
- conflicting completions;
- conflicting semantic tokens;
- duplicated parsing;
- fragile coordination between tools.

Instead:

```text
Editor
   │
   ▼
cppl-lsp
   │
   ├── C++L semantics
   │
   └── C++ semantics ──► Clang
```

There is one owner and one coherent view of the source file.

---

## Responsibilities

`cppl-lsp` is responsible for the IDE-facing semantics of C++L.

This includes:

- recognizing C++L source;
- understanding C++L-specific syntax;
- exposing laws, proofs, obligations, and verification information;
- reporting C++L diagnostics;
- maintaining C++L-to-C++ source mappings;
- projecting or erasing C++L constructs when ordinary C++ semantics are required;
- forwarding appropriate semantic operations to Clang-compatible infrastructure;
- translating delegated locations and diagnostics back to the original C++L document;
- providing C++L-aware hover information;
- providing C++L-aware navigation;
- providing C++L semantic highlighting;
- exposing verification state to editors.

The LSP layer must reuse the same C++L frontend and semantic implementation used by the compiler.

It must not invent an independent interpretation of the language.

---

## What `cppl-lsp` does not own

`cppl-lsp` does **not** reimplement ordinary C++ semantics.

In particular, it should not grow its own implementations of:

- the C++ type system;
- overload resolution;
- template instantiation;
- concepts;
- name lookup;
- implicit conversions;
- C++ constant evaluation;
- standard-library awareness;
- C++ diagnostics already correctly handled by Clang;
- C++ code completion;
- C++ reference discovery;
- C++ AST semantics.

A useful rule is:

> If the question would exist unchanged in an ordinary `.cpp` file, Clang should usually answer it.

If the question exists because the program contains C++L language semantics, the C++L frontend should answer it.

---

## One C++L frontend

The compiler and language server must not develop separate parsers or semantic models.

The intended architecture is:

```text
                C++L frontend
               /            \
              /              \
             ▼                ▼
         compiler          cppl-lsp
```

The shared frontend owns:

```text
parsing
   ↓
C++L semantic analysis
   ↓
proof obligations
   ↓
evidence checking
   ↓
verification kernel
   ↓
erasure / projection
   ↓
source mapping
```

`cppl-lsp` consumes those results.

It does not reproduce them.

This prevents compiler/editor semantic drift.

---

## Clang delegation

C++L deliberately does not attempt to replace Clang as a C++ compiler frontend.

For operations involving ordinary C++ semantics, `cppl-lsp` delegates to Clang/clangd-compatible infrastructure.

Examples include:

```text
completion
go-to-definition
find references
ordinary C++ hover
type information
template diagnostics
overload diagnostics
include handling
standard-library symbols
C++ semantic tokens
```

The exact implementation boundary may evolve, but the architectural invariant does not:

> C++ semantics have one authority: Clang.

`cppl-lsp` must not become a parallel C++ compiler frontend.

---

## Projection and source mapping

C++L contains syntax which Clang does not understand directly.

For Clang-backed operations, the C++L frontend can produce an ordinary-C++ semantic projection of the source.

Conceptually:

```text
original.cppl
     │
     │ C++L frontend
     ▼
projected.cpp
source-map
     │
     │ Clang
     ▼
semantic result
     │
     │ source-map
     ▼
original.cppl location
```

The projection exists for tooling and compilation infrastructure.

It is not a second source of language semantics.

Source mapping must remain stable enough that diagnostics, hovers, definitions, references, and other semantic results can be presented against the source the developer actually wrote.

---

## Diagnostics

There are two major classes of diagnostics.

### C++L diagnostics

Produced directly by the C++L implementation.

Examples include:

- malformed C++L declarations;
- invalid proof structure;
- unsatisfied proof obligations;
- invalid evidence;
- failed verification;
- invalid C++L-specific semantics.

These diagnostics refer directly to the original source.

### C++ diagnostics

Produced by Clang against the semantic C++ projection.

For example:

```text
Clang diagnostic
      │
      ▼
projection location
      │
      ▼
C++L source map
      │
      ▼
original source location
```

Users should see one coherent diagnostic stream regardless of which subsystem discovered the problem.

---

## Verification information

C++L introduces information that ordinary C++ language servers cannot expose.

`cppl-lsp` should make verification state visible directly in the editor.

For example, a developer should be able to inspect:

```text
law
proof
proof obligation
available evidence
verification result
failed obligation
source of discharged evidence
```

The exact editor presentation is separate from the language semantics.

The compiler determines what is true.

The LSP determines how that information is presented.

---

## Hover

Hover information should combine ordinary C++ information with C++L information where appropriate.

For an ordinary C++ entity, the result should primarily come from Clang.

For C++L entities, hover may expose:

```text
declaration kind
proposition
verification state
proof relationship
generated obligation
source evidence
```

The LSP must not independently recompute whether a proof is valid.

It displays the result of the C++L semantic pipeline.

---

## Navigation

Navigation should work across both ordinary C++ and C++L constructs.

Examples include:

```text
use → C++ declaration
proof → proposition
proposition → proof
evidence use → evidence declaration
C++L declaration → referenced C++ symbol
```

Ordinary C++ symbol navigation uses Clang semantics.

C++L relationships are supplied by the C++L frontend.

---

## Completion

Completion follows the same ownership rule.

Ordinary C++ completion:

```text
cppl-lsp
    │
    ▼
Clang
```

C++L-specific completion:

```text
cppl-lsp
    │
    ▼
C++L frontend
```

The final completion list may combine results from both sources before returning them to the editor.

`cppl-lsp` adds C++L knowledge rather than replacing Clang's understanding of C++.

---

## Semantic tokens

Syntax highlighting should remain structurally aware of both languages.

C++ entities may use Clang semantic information while C++L adds token categories for language concepts that do not exist in ordinary C++.

Potential C++L categories include:

```text
law
proof
proposition
evidence
verification construct
```

Token classification must follow the actual language grammar rather than editor-side textual heuristics.

---

## Editor architecture

Editor extensions should remain thin.

For VS Code:

```text
vscode/
└── cppl-vscode/
```

The extension is responsible for:

- starting `cppl-lsp`;
- connecting through LSP;
- registering C++L file types;
- editor commands;
- editor presentation where LSP alone is insufficient.

It must not implement C++L semantics.

The relationship is:

```text
VS Code extension
       │
       │ LSP
       ▼
    cppl-lsp
       │
       ▼
 C++L frontend
```

Therefore:

> `cppl-vscode` is a client.

> `cppl-lsp` is the language server.

> the C++L frontend is the language authority.

---

## Repository layout

The intended layout is:

```text
src/
└── lsp/
    ├── server.*
    ├── document.*
    ├── diagnostics.*
    ├── projection.*
    ├── source_mapping.*
    ├── hover.*
    ├── completion.*
    ├── navigation.*
    └── semantic_tokens.*

tools/
└── cppl-lsp/
    └── main.*

vscode/
└── cppl-vscode/
```

The executable under `tools/cppl-lsp/` should remain thin.

Reusable LSP implementation belongs under:

```text
src/lsp/
```

Compiler/frontend semantics remain in the compiler layers rather than being moved into the LSP.

---

## Request flow

A typical semantic request looks conceptually like this:

```text
1. Editor sends request for a C++L document.

2. cppl-lsp identifies the relevant source and document revision.

3. The C++L frontend supplies the C++L semantic model.

4. If the request is C++L-specific:
       answer from C++L semantics.

5. If ordinary C++ semantics are required:
       use the C++ projection;
       delegate to Clang;
       translate the result through the source map.

6. Merge information where necessary.

7. Return one LSP response to the editor.
```

The editor should never need to coordinate these layers itself.

---

## Document synchronization

`cppl-lsp` owns the live editor document.

Changes flow through one document state:

```text
editor text
    │
    ▼
cppl-lsp document
    │
    ├── C++L frontend state
    │
    └── projected C++ state
             │
             ▼
         Clang state
```

The projected C++ document must correspond to the same source revision as the C++L semantic state.

Results from stale projections must not be mixed with newer C++L document revisions.

---

## Correctness invariants

### One document owner

The editor communicates with `cppl-lsp`, not competing semantic servers for the same file.

### One C++ authority

Clang owns ordinary C++ semantics.

### One C++L authority

The C++L compiler/frontend owns C++L semantics.

### One C++L frontend

Compiler and LSP reuse the same frontend implementation.

### No duplicated language semantics

The LSP presents semantic results; it does not redefine the language.

### Stable source identity

Diagnostics and semantic results must map back to the exact C++L source developers are editing.

### Ordinary C++ remains ordinary C++

Valid ordinary C++ inside C++L must receive the same underlying C++ semantic treatment it would receive through Clang.

### Editor features cannot change language meaning

Adding hover, completion, code actions, or visualization must never require a second interpretation of proof validity or program semantics.

---

## Performance

Interactive editor tooling has different latency requirements from batch compilation.

`cppl-lsp` should therefore reuse cached semantic state wherever correctness permits.

Important requirements include:

- incremental document updates;
- reuse of parsed C++L state;
- reuse of projections;
- avoiding whole-project verification for unrelated local operations;
- cancellation of obsolete requests;
- avoiding duplicate Clang work;
- revision-consistent caches;
- background indexing where appropriate;
- no blocking full rebuild for common editor interactions.

Performance optimizations must not create a second semantic implementation.

Incrementality is an execution strategy, not a different definition of the language.

---

## Failure isolation

A failure in one semantic layer should not unnecessarily destroy all editor functionality.

For example, a C++L proof error should not prevent unrelated ordinary C++ navigation from working when a usable projection exists.

Likewise, a C++ compile error should not prevent the LSP from presenting structurally valid C++L information that is already known.

Partial editor results are acceptable.

Contradictory semantic models are not.

---

## File types

C++L source is associated with `cppl-lsp`.

Ordinary C++ projects may continue using their normal C++ tooling.

Because C++L is a superset of C++, a C++L file may contain entirely ordinary C++:

```cpp
#include <iostream>

int main() {
    std::cout << "hello\n";
}
```

That does not cause `cppl-lsp` to implement the semantics of this program itself.

It routes the relevant semantic work to Clang.

---

## Relationship to the compiler

`cppl-lsp` sits above the compiler architecture.

It depends on compiler concepts such as:

```text
C++L parsing
        ↓
semantic analysis
        ↓
proof obligations
        ↓
evidence
        ↓
verification kernel
        ↓
erasure / projection
        ↓
Clang
```

The language server follows language semantics rather than defining them.

New syntax or proof behavior belongs in the language specification and compiler/frontend first.

Once that behavior exists in the semantic pipeline, `cppl-lsp` can expose it to the editor.

---

## Development order

`cppl-lsp` should be built on stable core language slices rather than being used to prototype language semantics.

The dependency direction is:

```text
stable C++L syntax
        ↓
stable semantic model
        ↓
stable proof / obligation pipeline
        ↓
stable verification semantics
        ↓
stable erasure / projection
        ↓
stable source mapping
        ↓
stable compiler diagnostics
        ↓
cppl-lsp
```

The initial LSP does not require every future C++L feature to exist.

It does require the semantic boundaries it consumes to be stable.

Richer IDE functionality can then be added incrementally.

---

## Initial implementation scope

The first useful `cppl-lsp` does not need to reproduce every feature of clangd.

Its first responsibility is to establish the architecture correctly.

A useful initial slice is:

```text
LSP transport
        ↓
document ownership
        ↓
C++L frontend integration
        ↓
projection
        ↓
source mapping
        ↓
C++L diagnostics
        ↓
Clang-backed C++ diagnostics
        ↓
mapped unified diagnostics
```

Once this boundary is reliable, additional capabilities can build on it:

```text
hover
navigation
completion
semantic tokens
references
code actions
verification visualization
```

---

## Non-goals

`cppl-lsp` is not:

- a replacement for Clang;
- a fork of clangd containing a second definition of C++L;
- a second C++L compiler;
- an editor-specific parser;
- a regex-based recognizer for C++L;
- a place to prototype language semantics;
- a reason to duplicate compiler logic;
- a requirement for compiling C++L outside an editor.

C++L must remain usable from the command line independently of the language server.

---

## Design rule

When adding a feature to `cppl-lsp`, first ask:

```text
Who owns this fact?
```

If the answer is:

```text
C++
```

delegate to Clang.

If the answer is:

```text
C++L
```

obtain it from the C++L frontend/compiler.

If the answer is:

```text
editor presentation
```

implement it in `cppl-lsp` or the thin editor client.

This boundary is the central architectural rule of the project.

---

## Summary

```text
cppl-lsp owns the document.

C++L owns C++L semantics.

Clang owns C++ semantics.

The editor owns presentation.
```

Or, structurally:

```text
Editor
   │
   ▼
cppl-lsp
   │
   ├── C++L semantic model
   │
   ├── projection + source map
   │
   └── Clang semantic model
```

`cppl-lsp` exists to compose those systems into one coherent developer experience.

It adds the tooling required by C++L while continuing to inherit the mature C++ understanding of the Clang ecosystem.
