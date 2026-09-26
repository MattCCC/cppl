# cppl-lsp

`cppl-lsp` is the language server for **C++L - C++ with Laws**.

It provides editor tooling for C++L while preserving the fundamental C++L architecture:

> `cppl-lsp` owns the whole source file and internally delegates ordinary C++ semantic work to Clang/clangd-compatible infrastructure.

The editor talks to one language server: `cppl-lsp`.

`cppl-lsp` understands C++L extensions, proofs, verification, source projection, and C++L diagnostics. Ordinary C++ parsing, typing, lookup, overload resolution, templates, completion, navigation, and related compiler semantics remain the responsibility of Clang.

The goal is not to build another C++ language server.

The goal is to make C++L feel like a native extension of C++.

---

## Implementation status

Most of this document is **design**, not a description of what is built. What
ships today is:

```text
initialize / initialized / shutdown / exit
textDocument/didOpen, didChange, didClose   incremental sync
textDocument/publishDiagnostics             from the real compile pipeline
textDocument/formatting                     canonical C++L clause placement
textDocument/rangeFormatting                scoped to the requested range
textDocument/onTypeFormatting               scoped to the smallest safe unit
textDocument/completion                     C++ from Clang; C++L where the grammar
                                            admits it; case labels still owed
textDocument/hover                          C++ from Clang; C++L as written;
                                            a case subject's state partition
textDocument/codeAction                     syntax migrations; canonical fix-all
textDocument/semanticTokens/full            every name, from Clang; C++L words
                                            and names, from the frontend
textDocument/definition                     Clang, over the document's projection
textDocument/declaration                    the first declaration
textDocument/typeDefinition                 through pointers, references, `auto`
textDocument/implementation                 overrides and derived classes
textDocument/references                     across the workspace: open documents
                                            as held, other files from the index
textDocument/documentHighlight              declarations, reads and writes
textDocument/codeLens                       each Law's, proof's, function's verdict
textDocument/signatureHelp                  the call being written, from Clang
textDocument/documentSymbol                 C++ from Clang; Laws, proofs,
                                            refinement types from the frontend
textDocument/foldingRange                   C++ bodies, includes, conditionals
                                            from Clang; C++L from the frontend
textDocument/selectionRange                 Clang's constructs and the
                                            recognizer's C++L spans, nested
textDocument/inlayHint                      parameter names, deduced types,
                                            from Clang
workspace/symbol                            every declaration in the workspace,
                                            from the index and open documents
textDocument/prepareRename, rename          every place references finds, whole
                                            or refused with the reason
```

Diagnostics come from `driver::compile_buffer` over the live buffer — the same
pipeline the CLI runs — so the server holds no decomposition, exhaustiveness or
verification logic of its own. A structural linter adds contextual C++L checks
over the syntax that pipeline already recognized, rather than recognizing it a
second time. `publishDiagnostics` also reports canonical-formatting style
violations (severity `Warning`, category `Style`) from the same formatter
engine used to fix them, so an editor sees a clause-placement problem before
the user ever asks to format.

Code actions come from that engine too. Each syntax migration it knows — a Law
`ensures` that is now `proves`, a `case` that is now `cases` — is offered as a
`quickfix` where its edit would land, not everywhere in the file. Canonical
formatting of the whole document is offered as `source.fixAll.cppl`, which an
editor can run on save. Formatting runs `clang-format`, so it is computed only
when asked for by kind or by the user invoking code actions, never on the
automatic requests an editor sends as the cursor moves.

Completion and hover follow the same rule, and it is the important one: they
answer from the states the compiler's own case engine recorded while
elaborating this buffer (`elaboration::SubjectStates`), never from a
decomposition the server performed. There is exactly one case engine and it is
the compiler's (`AGENTS.md` 39). Where the compiler has not confirmed a
subject's states — the pipeline could not reach elaboration, or no provider
models the type — the server offers nothing rather than guessing, because a
suggested label that the compiler would reject is worse than no suggestion.

One consequence is worth stating plainly: recognition reruns on every edit, but
elaboration runs only where the server already pays for it, on publish. So the
offered labels can lag the buffer by an edit. The alternative is a Clang round
trip per keystroke or a second decomposition engine, and the second is
forbidden.

Completion offers each state the subject's provider lists and this statement
has no arm for yet, including the residual one — a residual state is a real
semantic state, not a catch-all, so it is offered like any other (`AGENTS.md`
39, no wildcard). Each item inserts an arm skeleton carrying the provider's own
binder names, so an accepted completion already has the right binder count for
that state's payload. Hover names the subject's resolved representation, which
provider modeled it, and the full partition with the written arms checked off.
An omitted case is marked as claimed impossible, not as proven: hover reads the
written syntax, and whether the claim checks is reported as a diagnostic. A case
split written in a verified body is a case site the same way, served from the
states the compiler recorded while elaborating the body.

Semantic tokens color every name by what it names (see "Semantic tokens").
C++ names are Clang's, and C++L's words and names are the recognizer's.

A proof statement is spelled like a C++ declaration, as in `exact h;`,
`assume h : P;` or `contradiction name;`. That is what the editors' TextMate
grammar cannot tell apart, and only the recognizer knows which spellings are
statements.

A `contradiction` statement in a verified body is a claim only where the whole
translation unit, headers included, uses the word for nothing else (SPEC.md
WORD-002). Only the compile of the preprocessed unit sees the headers, so such
a claim is reported only when that compile recognized claims too. Where it did
not, or could not run, the statement is left uncolored, as the grammar leaves
it.

A `cases` or `decompose` statement in a verified body is a case split on the
same terms (SPEC.md WORD-012). It and the keywords in its arms are reported
only when that compile recognized splits. A claim in an arm is reported once,
as the split's.

Navigation is Clang's answer, read back through the projection. Each document
gets an editor unit: the analysis projection the compiler makes
(`frontend::project`), made from the buffer as written rather than from the
preprocessed unit, parsed by libclang and kept with a precompiled preamble, so
the request after an edit costs a reparse of the document and not of its
headers. Because the projection is made from the text as written, its
`#include`s stay directives: Clang reads the headers themselves, and a position
it reports in one is exact. A header that holds C++L is read as its projection
too, and so is every other open buffer, as the editor holds it.

What Clang reports is shown only where it can be traced to text an author
wrote. The projection records every run of the written text it kept in place
(`Projection::segments`) and every run it copied into a declaration it
generated (`Projection::copies`): a Law's and a proof's parameters, the
expression a clause states, a refinement's predicate. So a name inside a Law's
proposition leads to the C++ it names, a Law's parameter used in its
proposition leads to the parameter as written, and a verified function's
parameter named in its `expects` or `ensures` leads to that parameter. The
declaration generated for a Law stands for the Law's name and the alias
generated for a refinement type for the refinement's, so a proof's `proves`
clause leads to the Law, and a use of a refinement type to its declaration.
Anything else Clang reports in generated text, such as `result` or a probe, has
no written position and is not shown: no answer is better than one pointing at
text nobody wrote.

Proof statements are not C++, and Clang says nothing about the names they use.
The compiler resolves those while it elaborates the buffer -- to a proof, to a
trusted Law, or to a name an earlier `assume` bound -- and records each
resolution with where the name is written and where what it names is declared
(`elaboration::ResolvedName`). So `exact p;`, `apply p;`, `rewrite h;`,
`contradiction e;` in a proof or a verified body, and the evidence of
`omit label by contradiction e;`, lead to what they name, and references to a
proof or an assumption list every statement that names it. The server answers
from those records and never resolves a name itself. A record comes from the
last compile, so it is used only where the buffer still spells the name at the
recorded position.

References come from the same units. A name is identified as Clang identifies
it, by its USR, so every open document's unit finds it in itself and in the
headers it includes, and the answers are merged. A parameter the projection
repeats -- a proof's, copied into the declaration for its claim and into each
probe of its statements -- is several parameters to Clang and one to its
author, so a declaration Clang reports at the same written position is the
same name. A reference is reported only where its name is written: an
implicit call, or a use spelled by a macro's body, has no written name and is
not an occurrence. `documentHighlight` marks each occurrence in the document
as its declaration, a read, or a write, where a write is the target of an
assignment, a compound assignment or an increment. Files no open document holds
answer from the workspace index (see "Workspace index").

The editor unit reads text as written, which is what makes its positions exact,
and it is also its one limit: a C++L construct spelled through a macro, such as
`#define V verified`, is recognized by the compiler, which reads the
preprocessed unit, but not here. The unit Clang parses then keeps the
construct's clauses as written, Clang recovers around them, and navigation near
them finds what that recovery left. Diagnostics are unaffected: they come from
the compile, never from the editor unit.

The server advertises `textDocumentSync`, `documentFormattingProvider`,
`documentRangeFormattingProvider`, `documentOnTypeFormattingProvider`,
`completionProvider`, `hoverProvider`, `codeActionProvider` (kinds
`quickfix` and `source.fixAll.cppl`), `semanticTokensProvider` (whole
document; the legend under "Semantic tokens"), `definitionProvider`,
`declarationProvider`, `typeDefinitionProvider`, `implementationProvider`,
`referencesProvider`, `documentHighlightProvider`, `codeLensProvider`,
`signatureHelpProvider`, `documentSymbolProvider`, `foldingRangeProvider`,
`selectionRangeProvider`, `inlayHintProvider`, `workspaceSymbolProvider` and
`renameProvider` (with `prepareProvider` for a client that can prepare).
The rest of navigation specified below is not implemented and not advertised:
an editor is told what the server can do, never what it intends to do.
`docs/STATUS.md` tracks this.

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

### Where a diagnostic is shown

The buffer is compiled from a scratch copy, which a `#line` directive names by
the document's own path, so every location the compile reports names the
document or a file it includes, never the copy. A location in the document is
shown where it was written, at the column its author wrote it at, counted in
UTF-16 as LSP requires. A location in an included header is shown on the
document's `#include` that brought the header in, directly or through other
headers, as `in included file: <message>`, with the header's own location as
related information. A note links to its own location, in the document or in
the header it names.

A header's C++L structure and layout are the header's to report, where it is
itself open, so the structural linter and the style check report only the
document's own constructs.

---

## Refinement types

A refinement type is a C++L declaration that also bears a C++ type, so it is a
case where both authorities are involved at once:

```cpp
type Percentage = int where (self >= 0 && self <= 100);
```

The division follows the ownership rule. `type`, `where`, `self`, the index
binders and the predicate are C++L, and the C++L frontend supplies them: the
declaration's ranges, the refinement's name, its base type, its indices and the
membership obligations a value owes where it enters the type. The base type itself,
every name inside the predicate and every ordinary use of the refinement name are
C++, and Clang answers for them against the projection, where the declaration
appears as the alias it lowers to.

What the language server should therefore be able to show:

```text
Percentage
refinement of int
where self >= 0 && self <= 100
erases to int
```

That last line matters: the hover must not present a refinement as a distinct
runtime C++ class, because it is not one. It is verification-level identity over
the base type (`SPEC.md` 17.8).

The capabilities this touches are declaration highlighting for `type` and `where`,
hover and type information combining the refinement with its base, go-to-definition
from a use of the name to the declaration, completion of refinement names where a
type is expected, and the membership diagnostics the frontend produces. Ordinary
C++ completion and navigation inside the predicate and the base type stay Clang's.

Source mapping is what keeps this usable. A refinement declaration lowers to an
alias in place, carrying one newline per newline of the declaration, so a Clang
diagnostic on any later line still maps to the line the author wrote.

---

## Formatting

`cppl-lsp` and the standalone `cppl-format` CLI (`tools/cppl-format/`) share
one canonical-formatting engine, `compiler/formatter`: both call the same
`format_document`/`format_ranges`/`format_on_type` functions and produce
byte-identical output, the same way both already share one compile pipeline
(`cppl::driver::compile_buffer`).

Ordinary C++ formatting is delegated to `clang-format` via the repository's
existing subprocess/tool-driver abstraction — the formatter does not link
LibFormat or reimplement clang-format's own layout rules. The engine only
relocates C++L-specific clauses:

```text
expects(...)
ensures(...)
invariant(...)
proves(...)
```

Each begins its own continuation line, indented one level from the enclosing
declaration or loop header, with the opening `{` on its own separate line back
at the declaration's column, and no whitespace between the clause keyword and
its `(`:

```cpp
verified int fifty(int x)
    ensures (result == 50)
{
    return 50;
}
```

Refinement `where` clauses stay inline and are never relocated:

```cpp
type Percentage = int where (self >= 0 && self <= 100);
```

`textDocument/rangeFormatting` and `textDocument/onTypeFormatting` are scoped:
a requested range or cursor position expands only to the complete C++L clause
or ordinary-C++ line it touches, never to the whole document, and clauses
outside that region are left untouched.

The same clause-placement rule is reported as `Style`-category warnings
through `publishDiagnostics` (`formatter::check_style`) — a pure token-position
check with no `clang-format` subprocess, so it is cheap enough to run on every
edit.

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

Implemented today: the compile of the buffer copies out, after the kernel has
decided, what became of every obligation it verified -- its origin, where it
is stated, its status, the trusted Laws a proven claim rests on, the goal the
kernel was given, what produced the evidence, why it is not proven when it is
not, and the proof written for a Law (`driver::ObligationRecord`). The server
shows these and decides nothing:

- a code lens over each Law, proof and verified function the document writes
  states its verdict: `PROVEN`, `PROVEN relative to trusted a, b`, `TRUSTED`,
  `UNRESOLVED` and why, or for a verified function how many of its obligations
  are proven. A proof of a Law has no obligation of its own, so its lens states
  the Law's verdict on the evidence it supplied, or the verdict of the Law it
  names when another proof's evidence decided it. A compile that stopped before
  verification says so rather than showing nothing;
- hover over any of these, or over a name that stands for one, lists each of
  its obligations with its status, its goal, and its evidence or the reason it
  is not proven.

A lens runs no command. A verdict is shown only for the version of the buffer
it was computed for, so text edited since the last compile shows none rather
than one that no longer applies. Counterexamples are not shown: no part of the
compiler produces one.

---

## Hover

Hover information should combine ordinary C++ information with C++L information where appropriate.

For an ordinary C++ entity, the result should primarily come from Clang.

Implemented today: hover over a C++ name shows what Clang knows of it -- its
kind and qualified name, its declaration without a body, a variable's type, a
constant's or enumerator's value, a type's size and alignment, the comment
written for it, and the file it is declared in when that is another. Over
`auto` it shows the type deduced. Over a name that stands for a C++L
declaration -- a Law named in a proof's claim or by a proof statement, a proof,
a refinement type, a verified function, a name `assume` binds -- it shows that
declaration as written, never what the projection generated for it: a Law is
its `law` declaration, not a function returning `bool`, and a refinement type
says what it refines and that it erases to its base type. `result` in a
postcondition and `self` in a refinement predicate are declared only by
generated code, so hover says what they mean. Inside a `cases` or `decompose`
block, hover still shows the subject's states (see "Case arms").

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

Implemented today: definition, declaration, type definition and implementation
(see Implementation status). A definition request at a definition answers the
earlier declaration, as clangd does; `auto` leads to the type it was deduced
as; an `#include` leads to the file it includes; an overloaded operator leads to
the operator called. Implementation answers every override of a virtual method
and every class derived from a class, within the unit.

### Outline

The document's outline follows the same split. Clang outlines the C++: every
declaration whose name the document writes outside a function body --
namespaces, classes, structs, unions, enums and their enumerators, functions,
methods, constructors, fields, variables, aliases, macros and concepts -- nested
as declared, a member defined outside its class named with its class. An entry
is kept only when its name is text the projection kept where it was written, so
nothing the projection generated appears, whether its name is generated or
copied from C++L. The C++L frontend supplies the rest from the syntax the
compiler recognizes: each Law (`law` or `trusted law`), each proof with what it
proves, and each refinement type with what it refines, placed inside the
innermost namespace or type written around it. A verified or pure function is
Clang's entry, marked `verified` or `pure`.

A client that cannot nest an outline
(`hierarchicalDocumentSymbolSupport` unset) gets the same entries as a flat
list, each naming the entry it is nested in.

### Workspace index

Files no document holds open answer too. The server indexes every file under
each folder the client opened (`workspaceFolders`, or else `rootUri` or
`rootPath`): each C++ and C++L source and header (`.cppl`, `.cpp`, `.cc`,
`.cxx`, `.c++`, `.h`, `.hh`, `.hpp`, `.hxx`, `.h++`, `.ipp`, `.inl`), and each
file a compilation database at a root (`compile_commands.json` or
`build/compile_commands.json`) lists, wherever that file lives. The walk passes
by hidden directories, `node_modules`, build trees (a directory holding
`CMakeCache.txt`) and repositories nested in the workspace (a directory holding
`.git`). It stops at 20,000 files and 32 directories deep, so a root that is
not a project costs a bounded amount.

Each file is read from disk the way an open document is read. Clang reads it
through its projection, with the flags its build gives it (`ARCH-LSP-008`).
The compile reads it as far as elaboration, which says what each name a proof
statement uses resolves to. The index never verifies, publishes no diagnostic
and shows no verdict. It reads every file once, then looks every 2 seconds for
files added, removed or edited on disk, and reads again only those.

The index reads several files at once, on half the processors. Each thread
that reads runs at a lower priority than the thread answering requests: a
utility quality of service on macOS, a nicer niceness on Linux. A file the
index reads is parsed once, without the precompiled preamble an open document
keeps for its next edit. What a file's reading finds in a header that the
index also reads on its own is left to that header's reading, so each place
is kept once.

An open document always answers as the editor holds it, and the index answers
only for the other files (`ARCH-LSP-009`). So an edit not yet saved is what
workspace symbols and references see.

- **Workspace symbols** (`workspace/symbol`) are the declarations the outline
  shows, in open documents and indexed files, whose name holds the query's
  characters in order, ignoring case. The name itself ranks first, then a name
  it begins, then one it is part of, then one it is scattered through, each
  rank by name. At most 256 are returned, each with the declarations it is
  nested in as its container (`a::b`).
- **References** reach every indexed file: a C++ name by its USR, and a Law or
  a proof through each proof statement the compile resolved to it.
- **Progress.** A client that shows progress sees each pass that reads files as
  work in progress titled `Indexing`, with how many files are read out of how
  many it found.

### Rename

A rename rewrites every place references finds the name written, its
declarations included: in each open document as the editor holds it, and in
each file the workspace index reads. A class's constructors and destructor are
spelled with its name and are renamed with it, from the class or from any of
them; a destructor keeps its `~`. A Law, a proof or an assumption is renamed in
each proof statement the compile resolved to it too.

A rename is made whole or not at all. It is refused, with the reason, when:

- the new name is not a C++ identifier, or is a C++ keyword or an alternative
  token (`and`, `xor_eq`);
- the name is written in a file the editor does not hold open and the index
  does not read, such as a system header, as for `std::abs`;
- a place no longer spells the name, as a file edited on disk since it was
  read;
- the name is used through a macro whose body spells it. Rewriting the body
  would rename whatever else the macro names, and leaving it would break the
  use. A name passed to a macro as an argument is written where the macro is
  used, and is renamed there;
- the new name is a C++L word (`frontend::cppl_words`: SPEC.md 3, WORD-001,
  WORD-002, WORD-010) and a place to rename lies inside a C++L construct, where
  the word has its C++L meaning. Outside every C++L construct it is an ordinary
  identifier and may be written;
- the recognizer would read any edited file's C++L differently afterwards:
  other constructs, or a proof with other statements. Besides C++L's words,
  only the arm labels a representation reserves (WORD-005) change what it
  reads. An unqualified case label is read only as such a state, so renaming an
  enumerator written as one to `none` turns a statement the recognizer refused
  into one it reads, and is refused. The frontend decides this, never the
  server (`ARCH-LSP-010`);
- the index is still reading the workspace after 30 seconds, since a rename
  that missed a file would not be whole.

`textDocument/prepareRename` names the place under the cursor and the name
there, or refuses as a rename would, except that it neither checks a new name
nor waits for the index. Refusals are answered as `RequestFailed` (`-32803`)
with the reason as the message. A rename to the same name changes nothing.
Whether the new name collides with another declaration, or would be captured
by one, is not checked; the compile reports what it breaks.

### Folding and selection

Folding follows the same split, and the server reads neither C++ nor C++L
structure from the text itself.

- **C++ structure comes from Clang.** A body folds between its braces: a block,
  and a function's, a lambda's, a class's, an enumeration's, a namespace's, an
  `extern` block's or a braced initializer's. A run of consecutive `#include`s
  folds as imports.
- **Conditional directives are paired from Clang's tokens.** Each branch of a
  conditional directive folds as a region, up to the directive that ends it.
  Clang's cursors do not cover conditional directives, so these are the one fold
  built from tokens.
- **C++L structure comes from the recognizer**
  (`compiler/frontend/include/cppl/frontend/structure.hpp`):
  - a proof's body, a Law's body, the arms of a `cases`, `decompose` or
    `induction` statement, and each arm's body fold between their braces;
  - a Law with no body and a refinement type fold as whole lines.
- **Comments come from the frontend's lexer.** A block comment folds as a
  comment, and so does a run of line comments that each start their line. Code
  on a comment's line is never folded away with it.

Braces the projection generated are never written, so they never fold.

A client that folds only whole lines (`lineFoldingOnly`) keeps each closing
brace's line in view.

Expanding a selection (`textDocument/selectionRange`) grows from the token under
the cursor. Each step is a construct that holds the one before it:

- what Clang parsed, traced back through the source map, so a Law's proposition
  selects as the expression Clang read where the projection copied it;
- and the recognizer's C++L spans, in this order: a clause's expression, the
  clause, a proof statement, an arm's body, the arm, the arms, the statement,
  a proof's body, and the declaration.

### Inlay hints

Inlay hints are Clang's, read back through the source map. Each argument of a
call is labelled with the name of the parameter it is passed to, and each
variable declared `auto` with the type it was deduced as. A call inside a Law's
proposition or a contract is labelled where the author wrote it. The projection
repeats that text in generated declarations, but each argument is labelled
once, and nothing generated is labelled.

Some arguments are not labelled:

- one that already spells its parameter's name;
- a default argument, which is written nowhere;
- the operands of an overloaded operator;
- the arguments of a call that a macro's body writes.

A macro used as an argument is labelled where it is used. A lambda's type, a
type still to be deduced in a template, and a type longer than 32 characters
are not shown.

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

Implemented today: completion of ordinary C++ is what Clang would accept at the
position (`clang_codeCompleteAt` over the editor unit), after `.`, `->` and
`::` as well as while a name is typed. Candidates are matched against what has
been typed of the name, in the same case first, then in any case, then as the
typed letters in order, and ranked within each by Clang's own priority; a
function or method is inserted with its parameters as snippet placeholders
where the client takes snippets. A name the projection generated is never
offered, and neither is a reserved name (`__x`) unless what was typed starts
with an underscore. At most 150 items are sent; a longer list is marked
incomplete, so the client asks again as the user types more.

C++L's own words are offered where the compiler's recognizer says they may be
written, as snippets laid out as the formatter lays them out: `law`,
`trusted law`, `proof`, `verified`, `type` and `pure` where a declaration may
begin at namespace scope; the proof statements where a statement may begin in
a proof body; after `exact`, `apply`, `rewrite` or `contradiction`, the
evidence it may name, looked for as elaboration looks for it -- the premises
this body assumed in scope, innermost first, then the trusted Laws, then the
other proofs; the clauses a Law, a proof or a verified function may still take
after its parameters or one of its clauses, in the order the recognizer
checks. Text being written seldom parses whole, so the server asks the
recognizer's draft of it (`RecognitionMode::Draft`, `frontend::admissible_at`),
which keeps a Law or a proof still being written and reads past a statement it
cannot read. The server holds no copy of C++L's grammar: each statement's and
clause's word is the recognizer's own, and a test checks that every snippet
offered is what the recognizer reads it as. Nothing here resolves a name: a
suggestion the compiler would reject is only a suggestion.

A draft only says where the author is, so it decides nothing (`ARCH-LSP-007`).
Evidence is offered only from declarations written whole and from `assume`
statements that could be read. The compiler never recognizes a draft, and
elaboration refuses any syntax that holds a node only a draft keeps.

Signature help shows, while a call's arguments are written, every declaration
Clang says it could resolve to, with the argument being written marked. Clang
decides all of it: whether the position is inside a call's argument list, which
overloads can still take the arguments written, best first, and which
parameter the argument being written stands for. A call inside a Law's
proposition or a contract is helped like any other.

### Case arms

Arm completion, residual arms, binder completion, duplicate-arm and missing-arm
diagnostics, invalid-label diagnostics, hover, and go-to-definition for a
referenced state all read the **same decomposition model the verifier uses**
(`compiler/decomposition`, SPEC.md 20). There is no second list of a
representation's states anywhere in the editor path: a provider describes a
partition once, and both verification and the editor consume it.

This means arm support is not written per representation. Completion offers
whatever cases the subject's provider lists, plus its residual label when its
exhaustiveness model has one — `unnamed` for a scoped enumeration today, and
whatever a later provider names. Stale exhaustiveness after a source change
falls out of the same model: a representation that gains a state gains a case,
and the proof that wrote no arm for it reports a missing case.

Editor support uses the arm syntax the specification already defines
(`docs/GRAMMAR.md` 5.7). It invents no editor-only syntax.

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

What is implemented has two sources, and neither reads the text itself.

**C++ names come from Clang.** Every identifier the document writes that
resolves to something is a token of that thing's kind:

- `namespace`, `type` (an alias or a concept), `class`, `struct` (a union
  too), `enum`, `typeParameter`;
- `parameter`, `variable`, `property` (a field), `enumMember`;
- `function`, `method`, `macro` (only the macro's own name).

The modifiers are:

- `declaration` where the name is declared;
- `readonly` for a `const` object or an enumerator;
- `static` for a static member of a class;
- `deprecated`;
- `defaultLibrary` for a name declared in a system header.

A name inside a Law's proposition, a contract or a proof's claim is where the
author wrote it, read through the source map. The generated declaration for a
Law is the Law's written name. Nothing else the projection generated is a
token.

**C++L's words and names come from the recognizer**
(`src/lsp/include/cppl/lsp/semantic_tokens.hpp`):

- `keyword` for every C++L word: `law`, `trusted`, `proof`, `proves`, each
  clause's keyword, `verified`, `pure`, a refinement type's `type` and `where`,
  each proof statement's keyword, and `omit` and `by`;
- `function` for each Law's and proof's name;
- `type` for each refinement type's name;
- a constant `variable` for each name `assume` binds;
- for each name a proof statement uses, what the compile resolved it to: an
  assumption as a variable, a proof or a trusted Law as a function.

A word the recognizer left to C++, such as `int law = 1;`, is never a C++L
token. The gating of runtime claims and splits is described under
"Implementation status".

---

## Editor architecture

Editor extensions should remain thin.

For VS Code:

```text
editors/
└── vscode/
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

> the VS Code extension is a client.

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

editors/
├── shared/          TextMate grammar shared by every client
├── vscode/
├── jetbrains/
├── visual-studio/
└── neovim/
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

### Measured

`tools/cppl-lsp/bench.py` drives any language server over stdio as an editor
would: it opens one file of a workspace and times each request (see its
docstring). Measured on 2026-09-24 on an Apple M3 Max (16 cores, 48 GiB, macOS
26.6), against clangd 22.1.8, the server Clang itself provides. The workspace
was this repository at `e0287f0`, configured so that `build/` holds its
compilation database of 209 commands. The file was `src/lsp/src/server.cpp`,
with 20 repetitions at each of three names. Each server ran three times,
alternating and cold: clangd's saved index was moved away before each run.
Each figure is the median of the three runs.

| | cppl-lsp | clangd |
| --- | --- | --- |
| `initialize` | 13 ms | 45 ms |
| first hover, sent as the file opens | 2.70 s | 1.84 s |
| first diagnostics | 2.70 s | 1.84 s |
| hover | 0.2 ms | 0.6 ms |
| definition | 0.1 ms | 0.1 ms |
| references | 0.7 ms | 0.5 ms |
| document symbols | 0.3 ms | 0.4 ms |
| completion after `documents_.` | 26.6 ms | 28.1 ms |
| workspace symbols | 0.6 ms | 0.3 ms |
| whole workspace indexed | 22.3 s | 27.3 s |
| memory once indexed (physical footprint) | 389 MiB | 536 MiB |
| peak memory (physical footprint) | 1.6 GiB | 1.7 GiB |

Every request above was answered with a result, none empty.

- **The two indexes read different files.** cppl-lsp read 342 files: every
  source and header under the root, headers on their own too, and the C++L
  fixtures, which the compile reads as well. clangd read the 209 files its
  compilation database lists and reached headers through them.
- **Opening a file is slower.** cppl-lsp reads an opened file twice: the
  compile reads the preprocessed unit, and the editor unit reads the
  projection with a precompiled preamble. The editor unit is built on the
  thread that answers requests, when the first request arrives, and the
  compile's diagnostics are published only once that thread is free again.
  clangd builds one tree for both.
- **Memory is the physical footprint** the system charges, as Activity Monitor
  shows it. The largest resident set is about 2.1 GiB for either server, most
  of it memory freed and kept by the allocator.

Microsoft's C/C++ extension is not in the table. Its language server,
`cpptools`, answers `initialize`, then refuses to run outside Microsoft's
products, so it can only be measured inside VS Code itself.

To measure again:

```sh
git archive HEAD | tar -x -C /tmp/ws
cmake -S /tmp/ws -B /tmp/ws/build -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCPPL_BUILD_TESTS=ON
python3 tools/cppl-lsp/bench.py --name cppl-lsp --workspace /tmp/ws --file src/lsp/src/server.cpp \
    --at 'view_for(id.uri)' --at 'documents_.get(' --at 'index_->mentions' \
    --complete-after 'documents_.' --query Server -- build/dev/bin/cppl-lsp
python3 tools/cppl-lsp/bench.py --name clangd --workspace /tmp/ws --file src/lsp/src/server.cpp \
    --at 'view_for(id.uri)' --at 'documents_.get(' --at 'index_->mentions' \
    --complete-after 'documents_.' --query Server -- clangd --compile-commands-dir=/tmp/ws/build
```

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

A file with no C++L at all is handed to Clang exactly as written, so every
editor service works on plain C++ as it does on C++L. Which files the server
owns is still the editor's choice, made per file or per project. The clients
register it for the `cppl` language, and a project that wants its `.cpp` files
served by `cppl-lsp` maps them to that language. VS Code's `cppl.serveCpp` does
this for every C++ file. By default nothing claims every C++ file, since two
servers on one file would publish two diagnostic streams for it (see "Why
`cppl-lsp` owns the whole file").

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

---

## How to build

`cppl-lsp` builds as part of the ordinary CMake project; there is no separate
build step.

```sh
make build
```

or, directly:

```sh
cmake --preset dev
cmake --build build/dev
```

The executable is produced at `build/dev/bin/cppl-lsp`.

---

## How to run manually

`cppl-lsp` speaks LSP over stdio: Content-Length-framed JSON-RPC 2.0 on
stdin, the same framing on stdout, and nothing else on stdout (its own
diagnostics about malformed requests or a missing Clang go to stderr).

```sh
build/dev/bin/cppl-lsp --clang /path/to/clang++ --clang-arg -std=c++20
```

`--clang` selects the Clang driver executable used to preprocess and
semantically check documents. When it is omitted, the toolchain's `clang++` is
used. Repeat `--clang-arg` for each extra flag (include paths, defines, target
flags) that every document should be read with. These come after the flags the
document's build gives it (see "Compile flags"). `--clang` mirrors `cppl`'s own
`--clang` option.

### Compile flags

A document is read with the flags its build compiles it with. The server looks
for a `compile_commands.json` in the document's directory, then in a `build`
directory beside it, then in each directory above in turn. CMake writes one with
`-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`, and Bear or a build tool's own export
writes one for other builds.

The document's own entry gives its flags. A header, or a file the build does
not compile, takes the flags of the entry most like it: first one with the same
name and another extension, then the one sharing the most directories with it.

Only flags that change how the text reads are kept:

- include, system and framework paths, made absolute against the entry's
  directory;
- macros defined and undefined, and forced includes;
- the language standard and standard library;
- the target and system root;
- optimization levels, which define `__OPTIMIZE__`;
- the few `-f` and `-m` switches that define macros or change the language.

Output, dependency, warning and code-generation flags are dropped, and so is
the input.

The database belongs to the project, and opening a file must not run the
project's code. The compiler an entry names is never run: the server always
uses its own Clang. Only the flags above are passed on. A plugin (`-fplugin=`,
`-Xclang -load`), a tool search path (`-B`) or a toolchain elsewhere never
reaches Clang.

The document's editor unit and its compile read it with the same flags
(`ARCH-LSP-008`), so navigation and diagnostics never see two different
programs. An edited database is read again on the next request. A document with
no database is read with `--clang-arg` alone.

A quoted `#include` is looked for beside the document, as a compiler looks for
it beside the file that writes it. This holds even though the compile reads a
copy of the buffer from a scratch directory.

There is normally no reason to type LSP JSON-RPC by hand; an editor extension
(see below) does this. To confirm the process itself starts and speaks the
protocol, an `initialize` request can be piped in directly:

```sh
printf 'Content-Length: 100\r\n\r\n{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":null,"rootUri":null,"capabilities":{}}}' \
  | build/dev/bin/cppl-lsp
```

---

## How to run the fixture and unit tests

```sh
make test
```

or, to run only the LSP suites:

```sh
ctest --test-dir build/dev -R '^lsp_'
```

This runs, among others, `lsp_fixtures_test`, which drives the same
buffer-compile pipeline the server uses against real files under
`tests/fixtures/` and asserts the primary acceptance invariant: a valid C++L
fixture produces zero Clang/C++-semantic diagnostics, while a genuine
ordinary-C++ error and a malformed C++L construct are still reported.

`make check` additionally runs formatting and linting over the whole
repository, including `src/lsp`, `tools/cppl-lsp`, and the driver's shared
pipeline.

---

## How to launch it from VS Code

A thin client extension lives at `editors/vscode/`. It starts
`build/dev/bin/cppl-lsp` as a child process and registers it for the `cppl`
language ID. It takes over ordinary `.cpp` files only when `cppl.serveCpp` is
on.

To try it against the repository's own fixtures:

1. Open this repository in VS Code.
2. Open the Run and Debug view, select "Launch cppl-vscode Extension", and
   press F5. This opens a second VS Code window ("Extension Development
   Host") with the extension active.
3. In that window, open a file under `tests/fixtures/`. The repository's
   `.vscode/settings.json` already associates `tests/fixtures/**/*.cpp` (and,
   for the future native extension, `*.cppl`) with the `cppl` language mode,
   so these files activate `cppl-lsp` instead of the ordinary C++ tooling.
   Every other `.cpp` file in the repository keeps using normal C++ tooling.

See `editors/vscode/README.md` for extension-specific configuration (pointing
it at a non-default `cppl-lsp` binary or Clang installation), and
`editors/README.md` for the other editors and the publishing process.

---

## Reference state and void contracts

The shared compiler bridge handles scalar reference parameters, verified void
functions, reference writes and verified call post-state. `compile_buffer`
reports the same refinement-crossing and stale-alias proof failures as the CLI;
the editor does not infer a separate alias model. Source locations come from the
write, call or return that produced the obligation. `lsp_fixtures_test` covers
successful storage flows and a call invalidating a possibly aliased const
reference. Detailed pointer-state and effect hovers are not implemented.

## Currently unsupported

Transport, document synchronization, diagnostics, canonical C++L formatting,
code actions, proof-decomposition completion and hover, semantic tokens for
every name and C++L word, definition, declaration, type definition and
implementation, references and document highlights, workspace symbols, and
rename are implemented. The following are explicitly out of scope for this
milestone and are not implemented:

```text
semantic tokens for a range or as a delta (whole documents only)
proof search / interactive proof state
verification interfaces (--cppl-import-interface)
```

A call to a verified function another unit defines is therefore shown refused,
with the `cppl.verification.interface` code, exactly as the CLI refuses it when
no interface is imported (`docs/DEVELOPER_GUIDE.md` 15.4).

Completion and hover cover every name, from Clang for C++ and from C++L's own
syntax and declarations (see "Completion" and "Hover").

### Responsiveness

A compile runs on a thread of its own, so the server answers requests while one
runs.

- **When compiles run.** A document is compiled as soon as it opens. A change
  is compiled once typing has paused for 300 ms, and a later change replaces
  one still waiting, so typing costs one compile per pause, not one per
  keystroke.
- **Stale results are dropped.** A compile's result is applied only while the
  document still holds exactly the text it compiled. A compile of text since
  edited is dropped, never published: a compile of the edit is on its way.
  This is what keeps a verdict from describing text other than the text shown
  (`ARCH-LSP-005`).
- **Cancellation.** The input is read on a thread of its own too, so a request
  the client withdraws (`$/cancelRequest`) while it waits its turn is answered
  as cancelled (`-32800`) instead of run. A withdrawal of a request already
  answered is ignored.
- **Progress.** A client that shows progress (`window.workDoneProgress`) sees
  each compile as work in progress, titled with the file it checks, and each
  pass of the workspace index (see "Workspace index"). Each is reported on a
  token the server asks the client to create ahead of time: one for compiles,
  and one more while a workspace is indexed.
- **Refresh.** After a compile, a client that supports it
  (`workspace.codeLens.refreshSupport`, `workspace.semanticTokens.refreshSupport`)
  is asked to fetch its code lenses and semantic tokens again. Those are what a
  compile decides: each verdict, and which statements are claims.

`run_transport` compiles in the foreground unless `TransportOptions` asks
otherwise, so a test can read what a notification produced as soon as it
returns. `cppl-lsp` itself always compiles in the background.

`textDocument/didChange` is handled under incremental document sync
(`TextDocumentSyncKind.Incremental`). The client sends only what changed. Each
change with a range replaces what the range covers, counted in UTF-16 units.
One without a range replaces the whole document, so a client that sends whole
documents is served too. A notification's changes apply in order, each to the
text the last one left, and the result is recognized once. A change whose range
is malformed is skipped and logged. Every position, range and
version a client sends is checked before it is narrowed; one out of range is
refused as invalid params, or, for a version, logged and recorded as 0.
