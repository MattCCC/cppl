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
textDocument/didOpen, didChange, didClose   full-document sync
textDocument/publishDiagnostics             from the real compile pipeline
textDocument/formatting                     canonical C++L clause placement
textDocument/rangeFormatting                scoped to the requested range
textDocument/onTypeFormatting               scoped to the smallest safe unit
textDocument/completion                     C++ from Clang; C++L where the grammar
                                            admits it; case labels still owed
textDocument/hover                          C++ from Clang; C++L as written;
                                            a case subject's state partition
textDocument/codeAction                     syntax migrations; canonical fix-all
textDocument/semanticTokens/full            proof-statement keywords
textDocument/definition                     Clang, over the document's projection
textDocument/declaration                    the first declaration
textDocument/typeDefinition                 through pointers, references, `auto`
textDocument/implementation                 overrides and derived classes
textDocument/references                     across every open document
textDocument/documentHighlight              declarations, reads and writes
textDocument/codeLens                       each Law's, proof's, function's verdict
textDocument/signatureHelp                  the call being written, from Clang
textDocument/documentSymbol                 C++ from Clang; Laws, proofs,
                                            refinement types from the frontend
textDocument/foldingRange                   C++ bodies, includes, conditionals
                                            from Clang; C++L from the frontend
textDocument/selectionRange                 Clang's constructs and the
                                            recognizer's C++L spans, nested
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

Semantic tokens cover what the editors' TextMate grammar cannot: a proof
statement spelled like a C++ declaration, such as `exact h;`, `assume h : P;`
or `contradiction name;`. Each keyword the recognizer read as a proof statement
is reported as a `keyword` token, from the positions it recorded in the buffer
as written. A `contradiction` statement in a verified body is a claim only
where the whole translation unit, headers included, uses the word for nothing
else (SPEC.md WORD-002), and only the compile of the preprocessed unit sees the
headers. So such a claim is reported only when that compile recognized claims
too; where it did not, or could not run, the statement is left uncolored, as
the grammar leaves it. A `cases` or `decompose` statement in a verified body is
a case split on the same terms (SPEC.md WORD-012): it and the keywords in its
arms are reported only when that compile recognized splits, and a claim in an
arm is reported once, as the split's.

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
assignment, a compound assignment or an increment.

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
document, one token type, `keyword`), `definitionProvider`,
`declarationProvider`, `typeDefinitionProvider`, `implementationProvider`,
`referencesProvider`, `documentHighlightProvider`, `codeLensProvider`,
`signatureHelpProvider`, `documentSymbolProvider`, `foldingRangeProvider` and
`selectionRangeProvider`.
The rest of navigation specified below, and the semantic-token categories
beyond proof-statement keywords, are not implemented and not advertised: an
editor is told what the server can do, never what it intends to do.
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

Implemented today: the keyword of every proof statement the recognizer read,
as the `keyword` type, including a `contradiction` claim in a verified body
once the compile of the whole unit has recognized it (see Implementation
status). The categories above are not yet reported.

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
owns is still the editor's choice, made per file or per project: the clients
register it for the `cppl` language only, and a project that wants its `.cpp`
files served by `cppl-lsp` maps them to that language. It never claims every
C++ file by default, since two servers on one file would publish two
diagnostic streams for it (see "Why `cppl-lsp` owns the whole file").

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
language ID only — it never takes over ordinary `.cpp` files.

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
proof-statement keywords, definition, declaration, type definition and
implementation, and references and document highlights are implemented. The
following are explicitly out of scope for this milestone and are not
implemented:

```text
references in a file no open document includes
rename
semantic tokens beyond proof-statement keywords
proof search / interactive proof state
incremental (as opposed to full) text document sync
```

Completion and hover cover every name, from Clang for C++ and from C++L's own
syntax and declarations (see "Completion" and "Hover").

`textDocument/didChange` is handled under full document sync
(`TextDocumentSyncKind.Full`): the client resends the whole document on every
change, which this server always accepts correctly regardless of what sync
kind the client actually advertises support for. A change that carries a range
anyway is applied where it lands rather than taken as the whole document, and
one whose range is malformed is skipped and logged. Every position, range and
version a client sends is checked before it is narrowed; one out of range is
refused as invalid params, or, for a version, logged and recorded as 0.
