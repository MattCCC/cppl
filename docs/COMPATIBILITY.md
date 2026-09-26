# C++L Compatibility

**C++L — Source, Toolchain, ABI and Ecosystem Compatibility**

Status: Normative compatibility specification

This document defines the compatibility contract between C++L and the C++
ecosystem.

It specifies:

- supported C++ source-language modes;
- source-compatibility guarantees;
- preprocessing and macro compatibility;
- the reference C++ semantic authority;
- host and target compatibility dimensions;
- native ABI compatibility;
- standard-library compatibility;
- template, `constexpr`, RTTI and exception compatibility;
- compiler-extension policy;
- header, module and translation-unit compatibility;
- cross-translation-unit verification metadata compatibility;
- foreign-function and runtime-boundary compatibility;
- build-system and compiler-driver compatibility;
- proof-artifact and cache compatibility dimensions;
- release compatibility profiles.

This document is normative for compatibility classification and supported
compatibility profiles.

It does **not** define:

- C++L language meaning — see `SPEC.md`;
- the mathematical proof calculus — see `FOUNDATIONS.md`;
- the Trusted Computing Base — see `TRUST.md`;
- compiler component topology — see `ARCHITECTURE.md`;
- why compatibility decisions were chosen — see `DESIGN.md`;
- what a particular development revision currently implements — see `STATUS.md`.

Implementation progress MUST NOT weaken the compatibility contract defined here.

---

# 1. Normative terminology

The words:

```text
MUST
MUST NOT
SHOULD
SHOULD NOT
MAY
```

are normative.

A **compatibility profile** is a complete description of the C++ environment in
which a C++L compatibility claim is made.

A profile includes, as relevant:

```text
C++ language mode
C++ semantic-authority/toolchain family and version
target triple
ABI family
data model
standard-library family
standard-library version/feature set
preprocessor configuration
compiler semantic flags
exception mode
RTTI mode
floating-point mode
language extensions
verification-model versions
```

A compatibility statement without enough information to identify the relevant
profile is incomplete.

---

# 2. Authority and document relationship

`SPEC.md` defines the meaning of C++L programs.

This document defines which C++ environments C++L promises to preserve and how
compatibility is classified.

When the documents interact:

```text
SPEC.md
    says what C++L semantics require

COMPATIBILITY.md
    says which C++ environments are supported by that requirement

TRUST.md
    says what must be trusted for the result

ARCHITECTURE.md
    says how the implementation realizes it

STATUS.md
    says which supported profiles are implemented/passing today
```

**[COMPAT-AUTH-001]** This document MUST NOT redefine a C++L semantic rule from
`SPEC.md`.

**[COMPAT-AUTH-002]** `STATUS.md` MAY report an incomplete implementation, but
MUST NOT silently narrow the compatibility target.

**[COMPAT-AUTH-003]** A release MUST NOT advertise a compatibility profile as
supported unless its required conformance gates pass.

---

# 3. Core compatibility principle

C++L is a source-compatible superset of the supported C++ profiles.

Conceptually:

```text
C++ ⊂ C++L
```

For a supported profile:

```text
valid supported C++ source
    +
no C++L semantics requested
    ↓
valid C++L input with the same ordinary C++ runtime meaning
```

C++L adds formal semantics around C++.

It does not replace the C++ runtime ecosystem.

**[COMPAT-CORE-001]** Ordinary supported C++ MUST NOT require C++L annotations
merely to preserve its existing runtime behavior.

**[COMPAT-CORE-002]** Ordinary C++ semantics MUST remain controlled by the
selected supported C++ environment.

**[COMPAT-CORE-003]** Verification support and ordinary C++ compilation support
are separate compatibility dimensions.

---

# 4. What “source-compatible superset” means

Source compatibility does not mean that every program accepted by every C++
compiler is automatically in the supported C++L profile.

The guarantee applies to:

```text
ordinary C++ that is valid under a declared supported profile
```

The profile may exclude:

- unsupported compiler extensions;
- unsupported target-specific syntax;
- unsupported language modes;
- implementation-specific behavior outside the declared compatibility contract;
- malformed programs accepted only as compiler recovery;
- programs whose behavior depends on an undocumented compiler bug.

Source compatibility also does not mean that every ordinary C++ construct is
automatically usable in a verified proof.

A construct can be:

```text
source-compatible and executable
but
outside the currently modeled verified semantics
```

The stronger proof claim must then fail closed or cross an explicit boundary as
defined by `SPEC.md`.

---

# 5. Compatibility dimensions

C++L compatibility is multi-dimensional.

The following dimensions MUST be distinguished.

## 5.1 Source compatibility

Can the ordinary C++ source be accepted with its C++ meaning preserved?

## 5.2 Semantic-analysis compatibility

Does C++L use the same relevant C++ semantic interpretation as the selected C++
authority?

## 5.3 Verification compatibility

Does C++L have sound formal semantics for the operations needed by the requested
proof?

## 5.4 Runtime compatibility

Does the erased/runtime projection preserve the runtime semantics verified?

## 5.5 ABI compatibility

Does C++L preserve the ordinary native ABI promised by `SPEC.md`?

## 5.6 Library link compatibility

Can the generated native code link with an ordinary C/C++ library?

## 5.7 Formal-library compatibility

Does C++L have a sound formal model/contract for the library behavior being used
by proof?

## 5.8 Proof-artifact compatibility

Can a previously produced proof artifact or verification summary be reused?

## 5.9 Tooling compatibility

Can IDE/build-system tooling interact with the C++L compiler/services?

These dimensions MUST NOT be collapsed.

For example:

```text
link-compatible
```

does not imply:

```text
formally modeled
```

and:

```text
source-compatible
```

does not imply:

```text
proof-compatible
```

---

# 6. Supported C++ source modes

The production C++L language baseline supports the following ordinary C++ source
modes:

```text
C++17
C++20
C++23
```

These are independently selected underlying C++ modes.

The C++L proof language and verification semantics are intended to remain stable
across them except where a C++L construct necessarily refers to a C++ feature
whose existence or semantics differ by standard mode.

Examples include:

```text
std::expected availability
C++20 concepts
C++20 modules
library feature availability
constexpr language changes
```

**[COMPAT-STD-001]** C++L-specific proof semantics MUST NOT change merely because
the source mode changes when the relevant C++ entities have the same semantics.

**[COMPAT-STD-002]** Ordinary C++ syntax and standard-library availability follow
the selected C++ source mode.

**[COMPAT-STD-003]** A proof or summary produced under one C++ source mode MUST
NOT be reused under another mode unless semantic compatibility is established by
the artifact rules.

**[COMPAT-STD-004]** C++26 and later are not part of the core compatibility set
until explicitly added to this document and the required conformance profiles.

---

# 7. Compiler implementation language versus user source language

The implementation language used to build the C++L compiler is independent of
the user's selected C++ source mode.

For example:

```text
C++L compiler implementation
    built as C++23

user program
    compiled as C++17
```

is valid.

**[COMPAT-IMPL-001]** The implementation MUST NOT inject runtime constructs that
require a newer C++ standard than the selected user-source profile.

**[COMPAT-IMPL-002]** The implementation's own C++ standard is not a user-source
compatibility requirement.

---

# 8. Reference C++ semantic authority

The reference C++L implementation uses **Clang/LLVM** as the authority for
ordinary C++ semantics.

The production reference compatibility family is:

```text
upstream LLVM/Clang 22.x
```

A release may pin a narrower tested patch range.

The same semantic family SHOULD be used for:

```text
preprocessing
C++ parsing/Sema
template instantiation
constant evaluation
target semantics
native code generation
```

where practical.

Clang owns ordinary C++ questions including:

- preprocessing semantics;
- declarations;
- name lookup;
- overload resolution;
- template substitution/instantiation/specialization;
- concepts/constraints;
- implicit and explicit conversions;
- value categories;
- access control;
- object model semantics;
- `constexpr`/`consteval`;
- inheritance;
- virtual dispatch;
- exception type relationships;
- type identity;
- target-specific type properties;
- native ABI information.

**[COMPAT-CLANG-001]** C++L MUST NOT maintain a competing interpretation of
ordinary C++ when the selected Clang semantic authority supplies the authoritative
answer.

**[COMPAT-CLANG-002]** C++L verification MUST use the resolved C++ operation, not
a textual approximation.

**[COMPAT-CLANG-003]** A Clang version difference that can change proof-relevant
C++ semantics is a compatibility difference.

A verified member function keeps every C++ property of the member function it
is: its class's layout, its signature, its qualifiers, its overloading and its
calling convention are what Clang gives them, and its contract leaves no trace
in the program (`SPEC.md` CLASS-013). Which member function a call runs, and
whether it dispatches virtually, is Clang's answer; C++L refuses to verify a
virtual one rather than choose a body itself (CLASS-014).

---

# 9. Non-reference C++ compilers

GCC, MSVC, AppleClang and other C++ implementations may interoperate with C++L
through ordinary C++/ABI boundaries where a declared profile permits it.

They are not automatically interchangeable with the reference semantic authority.

In particular:

```text
verified with Clang semantics
then compiled with another compiler
```

is not automatically covered by the end-to-end `PROVEN` runtime guarantee.

A non-reference compiler may become a supported semantic/code-generation profile
only when this document or an official compatibility profile explicitly defines:

- the semantic authority;
- corresponding C++ mode;
- target/ABI;
- proof-relevant semantic deviations;
- conformance requirements;
- runtime trust implications.

**[COMPAT-COMPILER-001]** Successful compilation of erased C++ by another compiler
does not by itself establish semantic equivalence with the verified Clang model.

---

# 10. Host versus target

The machine running `cppl` and the machine targeted by the C++ program are
different compatibility dimensions.

Conceptually:

```text
host
    runs compiler

target
    defines user-program machine/ABI semantics
```

Verification MUST use target semantics where the target affects the meaning of
the program.

Examples include:

```text
pointer width
integer widths
char signedness
endianness where semantically relevant
alignment
object layout
calling convention
ABI
floating representation/mode
target intrinsics
```

**[COMPAT-TARGET-001]** Cross-compilation MUST NOT accidentally reason using host
machine widths or ABI.

**[COMPAT-TARGET-002]** Target triple and proof-relevant target features MUST
participate in proof-artifact identity.

---

# 11. Core production platform profiles

The production compatibility target comprises the following native platform
families:

| Platform family | Required target architectures | Native ABI family               | Reference C++ toolchain            |
| --------------- | ----------------------------- | ------------------------------- | ---------------------------------- |
| macOS           | arm64, x86_64                 | Darwin/Itanium C++ ABI family   | upstream Clang/LLVM                |
| Linux           | x86_64, AArch64               | platform Itanium C++ ABI family | upstream Clang/LLVM                |
| Windows         | x86_64                        | Microsoft C++ ABI               | Clang/LLVM in MSVC-compatible mode |

A particular development revision may implement fewer profiles; that is recorded
in `STATUS.md`.

A release MUST test every profile it advertises.

**[COMPAT-PLATFORM-001]** Architecture-specific behavior MUST NOT be inferred from
the host when cross-compiling.

**[COMPAT-PLATFORM-002]** ABI-family differences MUST be treated as semantic
artifact-key differences.

**[COMPAT-PLATFORM-003]** Platform support means both ordinary C++ compatibility
and successful execution of the relevant C++L conformance/ABI suites.

---

# 12. Optional target profiles

Additional target profiles MAY be supported without changing C++L semantics.

Examples include:

```text
WebAssembly
Android NDK targets
Apple mobile targets
embedded Clang targets
additional Linux architectures
Windows ARM64
```

Such a target is supported only when its release profile defines:

- target triple;
- source modes;
- ABI/data model;
- standard library/runtime environment;
- supported compiler extensions;
- proof-relevant target semantics;
- conformance status.

A target merely being accepted by Clang is insufficient for an official C++L
compatibility claim.

---

# 13. Preprocessing compatibility

C++L follows the selected C++ preprocessing semantics.

C++L contextual recognition occurs on the effective preprocessed token stream as
defined by `SPEC.md`.

This preserves ordinary behavior of:

- macros;
- conditional compilation;
- include guards;
- command-line defines;
- platform headers;
- generated headers.

**[COMPAT-PP-001]** A macro named `law`, `proof`, `verified`, `pure`, `ghost`,
`trusted`, `unsafe`, or another contextual C++L word retains ordinary
preprocessor behavior.

**[COMPAT-PP-002]** Verification metadata and proof artifacts MUST be bound to the
effective preprocessed semantic program, not only to original filename/line
coordinates.

**[COMPAT-PP-003]** Different proof-relevant macro configurations are different
compatibility environments.

---

# 14. Contextual C++L vocabulary

C++L's language words are contextual rather than globally reserved except where
the grammar explicitly defines otherwise.

Ordinary C++ uses such as:

```cpp
int law = 1;
void proof();
struct ghost {};
```

must remain ordinary C++ when the C++L grammar has not entered the corresponding
formal context.

The same principle applies to contextual identifiers such as:

```text
result
old
self
readable
writable
```

outside their formal contexts.

**[COMPAT-WORD-001]** Source compatibility MUST NOT be weakened by globally
reserving contextual C++L words.

**[COMPAT-WORD-002]** A lexical shortcut that misclassifies valid ordinary C++ as
C++L syntax is a source-compatibility defect.

---

# 15. Existing C++ keywords

Existing C++ keywords retain their C++ meaning.

In particular:

```text
requires
concept
constexpr
consteval
```

are not repurposed as unrelated C++L constructs.

**[COMPAT-KEYWORD-001]** C++L MUST NOT change the meaning of an existing C++
keyword merely to make the proof-language surface shorter.

---

# 16. Source-file compatibility

Existing C++ projects are not required to rename files.

The compatibility contract covers ordinary source/header extensions including:

```text
.cpp
.cc
.cxx
.c++
.C
.h
.hpp
.hh
```

where the selected platform/toolchain treats the extension as C++.

The dedicated extension:

```text
.cppl
```

MAY be supported for explicitly C++L-oriented source.

It MUST NOT be required merely to compile ordinary supported C++.

`-x`/explicit language-selection options may be supported when their meaning can
be preserved through preprocessing, projection, semantic analysis and code
generation.

If an explicit language override creates an unsupported ambiguity for a file
containing C++L constructs, the tool MUST reject rather than silently compile the
unit with different semantics.

---

# 17. Headers

Headers are first-class C++L input through ordinary inclusion semantics.

A header may contain, where allowed by `SPEC.md`:

- C++ declarations;
- refinement declarations;
- contracts;
- Laws;
- proofs;
- templates containing formal metadata.

Third-party ordinary C++ headers require no C++L modification merely to remain
usable.

**[COMPAT-HEADER-001]** A declaration's verification metadata MUST follow the same
semantic entity across headers/translation units.

**[COMPAT-HEADER-002]** Include configuration, macros and target environment that
change header semantics MUST participate in semantic identity.

**[COMPAT-HEADER-003]** Header textual identity alone is insufficient proof
identity when preprocessing or template instantiation differs.

---

# 18. Translation units

Ordinary C++ translation-unit boundaries remain ordinary C++ boundaries.

Verification metadata may cross them independently of native ABI.

A public declaration may provide the verification interface while an out-of-line
definition provides implementation, exactly as `SPEC.md` defines.

**[COMPAT-TU-001]** Link compatibility MUST NOT be used as a substitute for
verification metadata compatibility.

**[COMPAT-TU-002]** Missing verification metadata MUST fail closed for the stronger
proof claim rather than change the native ABI.

In this implementation the metadata is a verification interface, a text file the
build names with `--cppl-emit-interface` and `--cppl-import-interface` (`SPEC.md`
Annex L.2.1). It is never compiled, adds nothing to an object file, a symbol or a
calling convention, and units that use one another's interfaces compile to the
code of their hand-written erasures (`tests/e2e/cross_tu.sh`). An interface is
bound to the compiler build, kernel, formal core, Clang, language mode and target
that produced it, and to the content of the unit's files: native link
compatibility across any of those says nothing about whether an interface may be
used, and one produced under another is refused.

---

# 19. Modules

For C++20/C++23 profiles where modules are supported by the selected Clang
profile, C++ module ownership, visibility, import and instantiation remain C++
semantics.

C++L adds no separate proof-module syntax.

Exported verified interfaces must transport the formal metadata needed by
importers.

Binary module/PCH formats are compiler-version-sensitive implementation artifacts,
not stable C++L proof ABIs.

**[COMPAT-MODULE-001]** A module compiled under an incompatible compiler/profile
MUST NOT supply stale verification metadata merely because its native symbols
link.

---

# 20. Precompiled headers

PCH support is an implementation/toolchain compatibility feature.

Where supported, a PCH may accelerate ordinary C++ semantic processing but MUST
NOT become a source of verification truth independent of its semantic inputs.

PCH reuse must account for:

- compiler version;
- language mode;
- target;
- macro configuration;
- relevant semantic flags.

Incompatible PCH input must be rejected/rebuilt.

---

# 21. Templates

C++ templates remain C++ templates.

C++L does not introduce a second template language.

Clang remains authoritative for:

- primary templates;
- partial specialization;
- explicit specialization;
- implicit instantiation;
- explicit instantiation;
- substitution;
- constraints;
- overload participation;
- dependent lookup.

A C++L contract, refinement, Law or proof attached to a template is parameterized
by the same resolved template semantics.

Proof obligations are specialization-specific unless a checked generic proof
justifies reuse.

**[COMPAT-TEMPLATE-001]** Evidence for one specialization MUST NOT be reused for a
semantically different specialization merely because both originate from the
same template source.

**[COMPAT-TEMPLATE-002]** Template specialization identity MUST participate in
cross-TU metadata and proof-artifact identity.

---

# 22. Concepts and C++ `requires`

C++ concepts and `requires` clauses control C++ template viability.

They are not automatically C++L proof evidence.

A concept may influence:

```text
which specialization exists
which overload is selected
which expressions are valid
```

but does not by itself establish an unrelated formal Law.

**[COMPAT-CONCEPT-001]** C++L proof evidence MUST NOT change overload resolution
unless ordinary C++ semantics independently make the same selection.

---

# 23. `constexpr`

C++ `constexpr` remains C++ compile-time evaluation semantics.

C++L proof normalization is a different mechanism.

C++L MAY reuse a Clang-resolved constant result when the source semantics and
formal correspondence justify doing so.

It MUST NOT assume:

```text
constexpr result
    ==
kernel definitional equality
```

without a defined translation.

`constexpr` availability/behavior follows the selected C++ source mode.

---

# 24. `consteval`

`consteval` remains C++ immediate-function semantics.

The fact that a call must be evaluated at C++ compile time does not by itself
turn the function into a proof-producing total formal definition.

Formal reuse of its result requires the same correspondence discipline as other
C++ constant evaluation.

---

# 25. `constinit`

`constinit` remains ordinary C++ initialization semantics.

It does not create proof evidence or imply formal immutability.

---

# 26. C++ type qualifiers and value categories

`const`, `volatile`, references, rvalue references and C++ value categories retain
their ordinary C++ meaning.

C++L MUST NOT reinterpret:

```text
const
```

as:

```text
pure
```

or:

```text
immutable throughout all aliases
```

Likewise, C++L refinement validity does not create a new runtime qualifier.

---

# 27. Native ABI principle

Verification-only C++L constructs have no native ABI representation unless
`SPEC.md` explicitly says otherwise.

Examples include:

```text
Laws
proof declarations/evidence
contracts
ghost state
proof-only mathematical values
memory propositions
termination measures
trusted/unsafe verification metadata
refinement predicates
proof-only refinement indices
```

C++L MUST preserve the ordinary ABI of the erased runtime C++ program.

**[COMPAT-ABI-001]** Proof-only parameters MUST NOT appear in native calling
conventions.

**[COMPAT-ABI-002]** Proof-only fields/tags MUST NOT appear in runtime objects.

**[COMPAT-ABI-003]** Verification status MUST NOT alter symbol mangling.

---

# 28. Refinement ABI

A refinement erases to the runtime representation of its ultimate C++ base type.

For example:

```cpp
type Percentage = int where (self >= 0 && self <= 100);
```

has the runtime representation/ABI of `int`.

C++L MUST NOT introduce:

- wrapper objects;
- hidden tags;
- validation bits;
- hidden constructors;
- runtime theorem pointers;
- proof tokens.

Two declarations that differ only by refinement identity cannot form distinct
native overloads when their erased C++ signatures collide.

Such a collision is diagnosed.

---

# 29. Indexed/dependent refinement ABI

Proof-only refinement indices do not become hidden runtime ABI parameters.

For:

```cpp
type Index(unsigned n) = unsigned where (self < n);
```

the verification identity:

```text
Index<4>
```

is distinct from:

```text
Index<5>
```

while both erase to the ordinary runtime representation of `unsigned`.

If a dependent/index value is already an ordinary runtime C++ value for another
reason, it remains part of the ABI by ordinary C++ rules; C++L does not duplicate
it as proof metadata.

---

# 30. Class/object ABI

C++L verification metadata MUST NOT alter ordinary class layout.

Verification-only features must not change:

- `sizeof`;
- `alignof`;
- base/member offsets;
- vtable layout;
- RTTI layout;
- triviality solely because of proof metadata;
- calling convention;
- object representation.

Runtime behavior already required by the underlying C++ code remains unchanged.

---

# 31. ABI-sensitive attributes and flags

Attributes/flags that affect native layout or calling conventions are part of the
compatibility profile.

Examples include:

```text
packing/alignment controls
calling-convention attributes
visibility/export attributes
short-enum modes
target architecture/features
ABI compatibility flags
```

They MUST be supplied consistently to semantic analysis and code generation and
must participate in artifact compatibility where proof-relevant.

---

# 32. Linking ordinary C++ objects

C++L-generated object files are intended to link with ordinary C/C++ object files
under the selected ABI profile.

An ordinary library need not know that its caller was verified by C++L.

Conversely, successful linking says nothing about the truth of formal contracts
for that library.

**[COMPAT-LINK-001]** Native link compatibility MUST NOT promote unverified
external behavior to `PROVEN`.

---

# 33. Shared libraries and plugins

Shared-library boundaries remain ordinary native ABI boundaries.

Verification metadata used by downstream compilation is separate from runtime
dynamic linking.

A dynamically loaded symbol may be runtime-compatible even when its verification
summary is unavailable.

In that case, verified callers must use an explicit permitted boundary or fail
closed for any fact that depended on the absent summary.

---

# 34. LTO

Link-time optimization may change optimization strategy but must preserve C++
runtime semantics under the selected compiler profile.

C++L proof semantics do not depend on LTO being enabled.

Proof metadata must not be reconstructed from optimized LLVM IR unless a
normative/verified correspondence path defines that use.

A release may support:

```text
no LTO
ThinLTO
full LTO
```

as independent backend options.

Their availability is a release/toolchain support property, not a change in
language meaning.

---

# 35. Optimization levels

`-O0`, `-O1`, `-O2`, `-O3`, `-Os` and equivalent optimization selections do not
change the C++L theorem being proved when they preserve the selected C++ semantics.

Optimization level is not proof evidence.

If an optimization flag intentionally changes language semantics, it belongs to
the semantic-flag policy rather than this ordinary optimization category.

---

# 36. Semantic compiler flags

Some compiler flags alter the source/runtime semantics relevant to proof.

Examples can include:

```text
-fwrapv
-ffast-math and related floating-point modes
-fno-exceptions
-fno-rtti
-fshort-enums
-fsigned-char / -funsigned-char
-fpack-struct
target CPU/feature switches
Microsoft/GNU language-extension modes
```

A flag of this class must be:

```text
modeled and included in the compatibility profile
or
rejected for verified semantics
```

It must never be silently ignored.

**[COMPAT-FLAG-001]** Proof-artifact keys MUST include every option that can
change proof-relevant C++ semantics.

**[COMPAT-FLAG-002]** A semantic flag accepted by Clang but not modeled by C++L
MUST NOT silently inherit the default C++L formal semantics.

---

# 37. Warning flags

Diagnostic-only warning options do not normally change formal meaning.

They may change build success when combined with policies such as `-Werror`.

C++L should preserve their normal Clang behavior and ordering.

A warning becoming an error is a build-policy effect, not proof evidence.

---

# 38. Sanitizers

Sanitizers are runtime instrumentation.

They do not replace formal proof and do not become proof evidence.

Where supported, C++L should preserve ordinary sanitizer use such as:

```text
AddressSanitizer
UndefinedBehaviorSanitizer
ThreadSanitizer
MemorySanitizer
```

subject to the target/toolchain profile.

Sanitizer instrumentation is allowed to change debugging/runtime failure behavior
in the way that sanitizer normally does; this is a deliberate build mode, not
C++L verification semantics.

---

# 39. Debug information

C++L should preserve ordinary native debug-information generation.

Proof-only constructs may appear in source-oriented diagnostics/editor tooling but
need not have runtime debugger objects after erasure.

Runtime source mappings SHOULD map erased/lowered code back to useful original
C++L source locations where feasible.

Debug metadata is not proof evidence.

---

# 40. Exceptions

C++ exceptions remain ordinary C++ runtime semantics.

Verification distinguishes normal-return guarantees from exceptional paths as
required by `SPEC.md`.

The compatibility contract does not permit the compiler to make exceptions
disappear merely because a function has an `ensures` clause.

If the source profile is built with exceptions disabled, that configuration is a
distinct compatibility profile.

**[COMPAT-EXC-001]** Verification under exceptions-enabled semantics MUST NOT
reuse proof artifacts from exceptions-disabled semantics without validated
compatibility.

---

# 41. `noexcept`

`noexcept` remains C++ semantics.

It is not equivalent to a proof that a function cannot throw.

C++L verification may establish a stronger no-throw theorem where supported, but
the ordinary declaration meaning remains controlled by C++.

---

# 42. RTTI

RTTI remains available to ordinary C++ in profiles where it is enabled.

Operations such as:

```text
dynamic_cast
typeid
```

retain C++ semantics.

Formal reasoning about their results requires a defined verification model.

When proof-relevant RTTI semantics are unavailable, the stronger proof claim must
fail closed or cross an explicit permitted boundary.

Disabling RTTI creates a distinct semantic compatibility profile.

---

# 43. Inline assembly

Inline assembly is target/compiler-specific runtime code.

It may remain executable in ordinary/unsafe code where the selected Clang profile
supports it.

Verified code must not receive invented formal semantics for inline assembly.

Formal facts across an assembly boundary require an explicit contract/trust model
permitted by `SPEC.md`.

---

# 44. Builtins and intrinsics

Compiler builtins and hardware intrinsics are extension points whose semantics may
be target-specific.

Each proof-relevant builtin/intrinsic must be classified as:

```text
modeled
boundary-only
unsupported
```

A modeled intrinsic requires formal semantics corresponding to the selected Clang
operation and target.

A boundary-only intrinsic may execute but contributes no stronger formal facts
without an explicit permitted boundary.

---

# 45. Compiler extensions

Compiler-specific extensions are not all-or-nothing.

Each supported profile classifies an extension into one of:

```text
S — source-compatible + modeled for verified reasoning
B — source-compatible but boundary/unverified for stronger proof
R — rejected in the profile
```

An extension accepted as ordinary C++ by Clang is not automatically `S`.

**[COMPAT-EXT-001]** No compiler extension may silently receive proof semantics
because its syntax resembles a standard C++ construct.

---

# 46. GNU extensions

GNU C++ extensions may be supported by a declared profile.

When enabled, their ordinary parsing/runtime semantics remain Clang's.

Verification support is per-feature.

Examples requiring explicit classification include:

- statement expressions;
- `typeof`;
- vector extensions;
- GNU attributes;
- designated extensions;
- inline assembly;
- builtins.

The core C++L compatibility guarantee does not require every GNU extension to be
modeled.

---

# 47. Microsoft extensions

Microsoft-compatible C++ extensions may be enabled in the Windows profile as
required for native ecosystem interoperability.

Their proof semantics remain feature-specific.

MSVC ABI compatibility does not imply that every MSVC language extension is
formally modeled.

---

# 48. Apple/Clang extensions

Apple/Clang-specific source extensions may be accepted in applicable profiles.

Ordinary execution semantics remain those of the selected toolchain.

Objective-C++ and Apple runtime boundaries are classified separately below.

---

# 49. Standard-library runtime compatibility

C++L is intended to interoperate with the standard library selected by the
compatibility profile.

Runtime library compatibility and formal-model compatibility are distinct.

A runtime library may be usable by ordinary C++ even when C++L has no model for a
particular facility.

The profile identifies the standard-library family.

Core production families are:

```text
libc++
libstdc++
MSVC STL
```

where appropriate to the target platform.

A release MAY support a subset while development is incomplete; `STATUS.md`
records that implementation state.

---

# 50. Standard-library formal models

Formal models describe proof-facing public semantics.

They MUST NOT rely on private implementation layout unless the compatibility
profile explicitly binds the model to that implementation detail.

A model may depend on:

- C++ language mode;
- feature availability;
- standard-library feature-test macros;
- selected library semantics;
- relevant implementation version where unavoidable.

**[COMPAT-STDLIB-001]** A formal model for one library/profile MUST NOT be reused
under another profile when the modeled semantics may differ.

**[COMPAT-STDLIB-002]** A runtime library being link-compatible does not make its
unmodeled behavior verified.

## 50.1 The verified sequence models

The models of `std::array`, `std::vector`, `std::basic_string<char>` and
dynamic-extent `std::span` (`SPEC.md` J.17, RFC 0020) state only what the
standard guarantees of every conforming library: a length, element places
within it, the length each modeled operation leaves, and the operations that
may reallocate. They name no member, capacity policy or small-string layout of
any library, so one model serves libc++ and libstdc++ alike; the test suite
verifies the same claims against both (`tests/e2e/containers.sh`, run on macOS
with libc++ and in the Linux GCC job with libstdc++). Where libstdc++ gives a
constructor a trailing allocator parameter with a default argument and libc++
declares a separate overload, the defaulted `std::allocator`, or one
value-initialized in place, is not an operand of the modeled construction, so
both declare the same operations. MSVC STL is not yet exercised.

A specialization receives the model by its primary template's declaration in
namespace `std`, with the library's inline namespaces transparent, and only
with the default allocator and, for strings, the default character traits of
`char`; a vendor extension, a user type of the same spelling, `std::pmr`
containers and `std::vector<bool>` are refused. The length is `size_t` at the
target's pointer width. `std::span` needs C++20; a program in an earlier mode
does not have it, and the other three models apply in every supported mode.

---

# 51. Standard-library feature availability

C++L structural proof models apply only when the corresponding C++ facility is
available in the selected profile.

Examples:

```text
std::variant        C++17+
std::optional       C++17+
std::expected       according to selected C++23/library support
std::array          according to standard mode/library
```

Availability is determined by the selected C++ environment, not by C++L
pretending a missing standard type exists.

---

# 52. Standard-library implementation identity

Provider/model selection must use canonical C++ semantic identity rather than type
spelling alone.

Implementation inline namespaces, aliases and vendor internals must not cause an
unrelated type with the same spelling to receive a standard-library proof model.

Likewise, an implementation-specific wrapper must not receive a model merely
because its printed type resembles a standard type.

---

# 53. Third-party C++ libraries

Third-party libraries may be:

```text
ordinary/unverified
runtime-validated at a boundary
specified by trusted contracts/Laws
verified from source
verified through imported checked summaries
```

Linking does not choose the assurance level.

A library can remain fully usable for ordinary C++ even if no formal model exists.

A verified caller may rely only on facts justified by the selected boundary.

---

# 54. Header-only libraries

Header-only libraries participate in ordinary C++ template/inline semantics.

When their implementations are visible, C++L may verify modeled code directly if
the required semantics are supported.

Visibility alone does not make a library verified.

Unsupported operations still fail closed for the stronger claim.

---

# 55. Binary-only libraries

A binary-only library can be ABI-compatible without being proof-compatible.

Verified callers require one of:

- checked formal interface metadata;
- explicit trusted Law/contract assumptions permitted by the language;
- runtime validation sufficient for the needed postconditions;
- an unverified/unsafe boundary.

The native symbol alone supplies no theorem.

---

# 56. C interoperability

C++L interoperates with C through ordinary C/C++ ABI mechanisms.

`extern "C"` retains ordinary C++ meaning.

C functions may be called from ordinary C++L code under the selected ABI.

Verified code may rely on C behavior only through a justified interface.

C++L does not require C code to understand proof metadata.

**[COMPAT-C-001]** C ABI compatibility MUST remain independent of proof metadata.

---

# 57. Objective-C++

Objective-C++ interoperability is an optional compatibility profile on Clang
platforms that support it.

Ordinary Objective-C++ runtime semantics remain controlled by Clang and the
Objective-C runtime.

C++L does not invent proof semantics for:

- Objective-C message dispatch;
- ARC;
- dynamic Objective-C runtime behavior;
- Cocoa/Foundation contracts;

without explicit models.

A release advertising Objective-C++ support must state whether the support is:

```text
ordinary source/runtime interoperability only
or
includes specific verified models
```

---

# 58. JNI

JNI is a foreign-function boundary.

JNI uses ordinary native ABI/runtime mechanisms.

Formal facts about Java/Kotlin-side behavior require:

- explicit contracts/trusted assumptions;
- runtime validation;
- or another checked boundary model.

Java/Kotlin types and exceptions do not automatically become C++L proof types.

JNI object handles must be treated according to JNI lifetime/threading rules when
those facts matter to verification.

---

# 59. N-API

N-API is a foreign-function boundary.

JavaScript values entering C++ code are runtime values.

A JS value does not acquire a C++L refinement merely from its JavaScript
representation.

Runtime checks may establish path facts that justify later refinement crossings.

N-API lifetime/threading/environment constraints require explicit modeling when
they matter to a proof.

---

# 60. Other FFI systems

The same principle applies to:

```text
Python C API
Rust/C ABI
Swift/C interop
COM
POSIX APIs
Win32
device SDKs
database client libraries
GPU runtimes
```

ABI compatibility is distinct from proof compatibility.

C++L does not need one special language rule for every FFI technology.

---

# 61. WebAssembly

C++L may target WebAssembly wherever an official C++L profile defines a Clang
WebAssembly target and runtime/standard-library environment.

Proof semantics remain compile-time.

C++L adds no theorem runtime to WebAssembly.

Target-dependent semantics such as pointer width and runtime environment must be
taken from the WebAssembly target profile rather than from the host compiler
machine.

---

# 62. Freestanding and embedded C++

A freestanding profile may be supported separately.

Such a profile must define:

- standard-library subset;
- exception/RTTI availability;
- target data model;
- startup/runtime assumptions;
- supported compiler intrinsics;
- memory/environment assumptions.

Freestanding support MUST NOT be inferred merely because erased C++ happens to
compile with `-ffreestanding`.

---

# 63. Build-system compatibility

C++L should integrate through a Clang-compatible compiler-driver interface where
practical.

The primary build-system model is:

```text
existing build system
    ↓
cppl as C++ compiler driver
    ↓
selected Clang/LLVM toolchain
```

C++L does not require a proprietary build system.

It should interoperate with systems such as:

```text
CMake
Ninja
Make
Meson
Bazel
custom compiler-driver builds
```

when they can invoke the declared compiler profile correctly.

---

# 64. Compiler arguments

Compiler arguments affecting the C++ program must reach semantic analysis and
native compilation consistently.

This includes, where supported:

- include paths;
- macro definitions;
- language mode;
- target triple;
- sysroot;
- standard-library selection;
- exception/RTTI mode;
- target CPU/features;
- warnings;
- optimization;
- sanitizers;
- debug information;
- LTO;
- linker inputs/options.

**[COMPAT-ARGS-001]** C++L MUST NOT analyze one effective compiler configuration
and generate code under another incompatible configuration.

---

# 65. Argument ordering

Where Clang gives command-line ordering semantic meaning, C++L must preserve that
ordering or reproduce the same effective semantics.

Examples include:

- include search order;
- macro define/undefine order;
- linker library order;
- some target/feature flag precedence.

Reordering for convenience must not change the program.

---

# 66. Compilation databases

`compile_commands.json` is a supported interoperability format for repository
analysis and tooling.

A compilation database entry provides a concrete translation-unit compatibility
environment.

C++L tooling must still normalize/validate proof-relevant semantic options rather
than treating the raw command string as a proof identity by itself.

---

# 67. IDE and clangd compatibility

Ordinary C++ editor functionality should reuse clangd/Clang semantics where
possible.

C++L-specific tooling may add:

- Law/proof navigation;
- verification status;
- obligation display;
- trust dependencies;
- refinement information;
- C++L diagnostics.

Editor compatibility MUST NOT create an alternate language semantics.

The same source/profile must have the same verification meaning in:

```text
CLI
CI
VS Code
JetBrains
Neovim
other LSP clients
```

---

# 68. Formatting compatibility

C++L formatting must preserve ordinary C++ meaning.

Where C++L clauses are formatted, their canonical spacing/surface is defined by
the C++L formatter/grammar rules.

Formatting cannot be used to reinterpret ordinary identifiers as C++L constructs.

A formatting-only change must not change proof identity except where it changes
the actual semantic token stream.

---

# 69. Native runtime dependencies

C++L introduces no mandatory verification runtime.

A C++L-built executable may depend on ordinary runtime libraries because the C++
program itself depends on them.

It must not require:

```text
C++L theorem VM
proof interpreter
proof garbage collector
refinement runtime
hidden validator runtime
```

merely because verification was used.

---

# 70. Runtime validation libraries

C++L does not require a standard `validate<T>()` runtime API.

Runtime validation may be implemented using ordinary C++ appropriate to the
application.

The verifier reasons about successful runtime paths.

Compatibility with existing validation libraries is ordinary C++ library
compatibility.

---

# 71. Source-to-runtime compiler consistency

The strongest runtime proof claim assumes that the C++ semantics used during
verification correspond to those used during code generation.

The reference profile therefore keeps semantic analysis and native code
generation within the declared Clang/LLVM profile.

**[COMPAT-END2END-001]** Changing backend/compiler semantics after verification
requires re-establishing the runtime correspondence described by `TRUST.md`.

---

# 72. Proof metadata is not ABI metadata

Cross-translation-unit proof information may include:

```text
contracts
refinement identity/predicate
indexed-refinement arguments
Laws/proofs
purity
termination
effect summaries
trust closure
proof identity
formal-model version
```

This metadata is not part of the native C++ ABI.

It may be transported through:

- sidecar artifacts;
- modules;
- verified interface artifacts;
- another architecture-defined mechanism.

**[COMPAT-META-001]** Missing metadata MUST NOT be compensated for by changing
native function signatures.

---

# 73. Verification-metadata compatibility

Verification metadata is compatible only when every semantic dimension it depends
on is compatible.

At minimum the check must consider, where relevant:

```text
C++L language/spec version
formal-core version
kernel/checker version or compatible proof format
C++ source mode
semantic-authority/compiler version
target triple/data model
standard-library model identity
compiler semantic flags
entity identity
template specialization identity
refinement identity/index arguments
effect summary identity
trust dependencies
```

A native object may remain ABI-compatible while its verification metadata is
incompatible.

That is a valid state and must be reported accurately.

---

# 74. Proof-artifact compatibility

Proof artifacts are not promised to be stable merely because source syntax
remains compatible.

Compatibility dimensions include:

```text
language compatibility
formal-core compatibility
proof-format compatibility
kernel compatibility
C++ semantic-environment compatibility
target compatibility
library-model compatibility
```

An old artifact may therefore require re-verification after:

- compiler upgrade;
- formal-core change;
- target change;
- semantic flag change;
- model change;
- relevant dependency change.

This does not imply source incompatibility.

---

# 75. Cache compatibility

A cached `PROVEN` result is reusable only when the semantic compatibility key is
valid.

A cache key MUST NOT rely only on:

```text
filename
timestamp
source line
native symbol name
```

when other proof-relevant inputs differ.

Remote/local cache source does not change this rule.

---

# 76. Language-version compatibility

C++L language versions may evolve independently from the selected C++ mode.

A future C++L release may:

- preserve source syntax;
- change formal semantics;
- change proof artifacts;
- add support for another C++ mode;
- add a platform profile.

These are separate version dimensions.

A release must state any source-breaking semantic change explicitly.

---

# 77. Formal-core compatibility

The formal-core version identifies proof-term/type semantics relevant to checking.

A core version change does not necessarily change C++L source syntax.

Proof artifacts from a different core version may be rejected even when source
remains valid.

No compatibility layer may reinterpret old proof evidence under a new calculus
without a checked translation.

---

# 78. Kernel/checker compatibility

A checker implementation change that preserves the same accepted calculus may
remain proof-format compatible.

A change to primitive proof rules, normalization semantics or evidence format may
require proof-artifact invalidation.

The exact artifact rules belong to the release profile and `TRUST.md`.

---

# 79. Standard-library-model compatibility

A formal library-model version is an independent compatibility dimension.

A model change can invalidate proof reuse even when:

```text
C++ source unchanged
native library ABI unchanged
```

because the theorem interface may have changed.

Model versioning must therefore participate in summary/cache identity.

---

# 80. ABI compatibility across C++L releases

Because proof-only constructs erase, C++L version changes SHOULD NOT cause native
ABI changes merely due to proof metadata evolution.

ABI changes may still occur because:

- the underlying C++ source changed;
- the selected C++ ABI/toolchain changed;
- runtime-bearing C++L semantics were explicitly added in a future language
  version;
- target/standard-library ABI changed.

Any C++L-induced runtime ABI change must be explicit and cannot hide behind a
proof-only feature.

---

# 81. Source compatibility across C++L releases

C++L should preserve valid ordinary supported C++ across C++L releases.

New contextual syntax must be designed so ordinary C++ identifiers continue to
work outside the new grammatical context.

A future source incompatibility requires an explicit language-version decision.

---

# 82. Backward compatibility of proof source

Proof syntax/semantics have stronger stability requirements than internal proof
artifacts but are not identical to native C++ ABI stability.

A language-version migration may require proof-source changes only when the
formal language intentionally changes.

Implementation refactoring alone is not grounds to rewrite user proof source.

---

# 83. Extension compatibility and proof artifacts

If a proof depends on a compiler extension, its extension mode/version/target
must be represented in artifact compatibility.

A proof generated under one extension semantic mode cannot be reused under a
different mode merely because the source spelling is accepted by both.

---

# 84. Floating-point compatibility

Floating-point proof semantics depend on the selected C++/target/flag profile.

Flags that weaken or change IEEE-like semantics, contraction, reassociation,
denormal handling or exception assumptions must be modeled explicitly before
proofs may rely on them.

`-ffast-math` and related modes are not semantic no-ops.

A release may classify such modes as:

```text
modeled
ordinary/unverified only
unsupported for verified FP reasoning
```

but may not silently use strict-FP proofs for fast-math execution.

---

# 85. Integer-model compatibility

Integer proof semantics depend on:

- C++ type widths;
- signedness;
- integer promotions;
- usual arithmetic conversions;
- target data model;
- semantic flags such as `-fwrapv` if enabled.

Proof artifacts involving machine arithmetic must therefore be target/profile
specific unless their theorem is explicitly independent of those dimensions.

In this implementation (RFC 0019) the widths, the signedness of `char` and every
promotion and conversion are the ones Clang resolved for the selected target;
none is assumed. What verification requires does not depend on the language
mode: a conversion to a signed type that may not fit owes that it fits in C++17,
where the result is implementation-defined, and in C++20 and C++23 alike, where
it is reduced. A program that relies on that reduction verifies in no mode. A
verified program never overflows a signed operation, so `-fwrapv` and `-ftrapv`
change nothing it does.

---

# 86. Character-model compatibility

Plain `char` signedness and character/wide-character widths can be target/profile
properties.

Proofs relying on these properties must use the selected target semantics.

The verifier must not assume host defaults.

---

# 87. Pointer-model compatibility

Pointer width, alignment and ABI representation are target properties.

Formal pointer safety additionally depends on C++ object/lifetime/provenance
semantics.

A target having the same pointer width as another does not make pointer proof
artifacts automatically compatible.

---

# 88. Endianness

Endianness usually does not affect abstract C++ arithmetic semantics, but it can
affect programs that observe object representation, serialization or byte-level
layout.

Where a proof reasons about such behavior, target endianness becomes a semantic
artifact dependency.

The compiler must include it when relevant rather than globally assume it is
irrelevant.

---

# 89. Exceptions and ABI boundaries

C++ exceptions crossing shared-library or foreign boundaries require the ordinary
platform/runtime compatibility guarantees.

C++L does not strengthen an ABI that is not already safe for the selected
runtime/toolchain combination.

A formal no-throw theorem cannot repair an incompatible runtime exception ABI.

---

# 90. RTTI and ABI boundaries

RTTI/typeinfo/vtable interoperability follows the selected C++ ABI/toolchain.

C++L proof metadata does not modify RTTI objects.

Mixing object files from incompatible RTTI/ABI configurations is outside the
compatibility guarantee even if symbols happen to link.

---

# 91. Dynamic allocation/runtime library compatibility

`new`, `delete`, allocation functions and standard allocators remain C++ runtime
mechanisms.

Verification of lifetime/ownership does not replace the allocator ABI.

Programs that require compatible allocation/deallocation across binary boundaries
remain subject to ordinary platform/toolchain rules.

---

# 92. C++ standard-library ABI modes

Some standard libraries expose ABI-selection modes or versioned inline namespaces.

Those modes are part of the runtime compatibility profile when they affect native
interoperability.

Formal provider selection must use canonical semantic identity and MUST NOT assume
ABI compatibility from source spelling alone.

---

# 93. Mixed verified and ordinary objects

A native binary may contain:

```text
ordinary C++ objects
C++L-verified objects
trusted-boundary objects
unsafe-boundary objects
foreign objects
```

provided ordinary native ABI requirements are met.

Proof status is metadata about claims, not a different object-file format required
by the runtime linker.

---

# 94. Mixed C++ standard modes

Different translation units in one native program may be compiled under different
C++ modes if the underlying platform/toolchain/ABI permits that combination.

C++L verification metadata across those units may be reused only if the relevant
interface semantics are compatible.

A C++17 caller cannot rely on a C++23-only formal/library interface that is not
available in its own verification environment merely because native linking
succeeds.

---

# 95. ODR compatibility

C++L does not weaken the C++ One Definition Rule.

Formal metadata associated with one C++ entity must be semantically consistent
across declarations/definitions as required by `SPEC.md`.

Conflicting C++L contracts or refinement metadata are not repaired by choosing
one translation unit as the winner.

---

# 96. Name mangling

Native name mangling remains controlled by the selected C++ ABI.

Proof-only constructs do not alter mangled names.

Refinement identity does not create a new mangling distinction when the erased
C++ type is the same.

Template arguments that are ordinary C++ template arguments remain part of
mangling according to normal C++ rules.

---

# 97. Symbol visibility

Visibility/export/import attributes remain ordinary C++/platform semantics.

Proof metadata distribution may use separate visibility rules but cannot cause a
hidden native symbol to become exported or vice versa unless ordinary source says
so.

---

# 98. Static and dynamic linking

C++L supports both static and dynamic native linking according to the target
profile.

Verification metadata transport is logically separate from the linker.

A release may provide metadata packaging integrated with:

```text
static libraries
shared libraries
modules
package managers
```

but the proof layer must not require a replacement native linker.

---

# 99. Package-manager compatibility

C++L does not require a new package-manager ecosystem.

Ordinary C++ dependencies may continue to arrive through existing package
mechanisms.

Formal models/contracts may be distributed alongside those packages or supplied
by the consuming project.

Package provenance and trust are separate from native package resolution.

---

# 100. Generated source

Generated C++/C++L source is compatible when it participates in the same
preprocessing, semantic-analysis, verification and provenance rules as handwritten
source.

Generated source does not receive privileged proof authority.

A generator change that changes proof-relevant semantics invalidates dependent
artifacts.

---

# 101. Code generation tools

Bindings generators, RPC generators, schema compilers and similar tools may
produce ordinary C++ interfaces.

Verified use of generated APIs requires the same contracts/models as handwritten
APIs.

Generated annotations may be accepted if they are normal C++L source and pass the
same verification/trust rules.

---

# 102. Compiler plugins

Clang/compiler plugins may participate in an official profile only when their
effect on:

- preprocessing;
- AST/Sema;
- code generation;
- ABI;
- semantics;

is understood.

A plugin cannot silently extend proof authority.

If it changes proof-relevant C++ semantics, it belongs in the compatibility
profile and trust analysis.

---

# 103. Build reproducibility compatibility

Proof-artifact reuse assumes the semantic environment can be identified
reproducibly.

Absolute build paths, temporary-directory names and wall-clock timestamps are not
semantic compatibility dimensions unless the source itself observes them.

Macros such as:

```text
__FILE__
__DATE__
__TIME__
```

may make otherwise environmental details part of the actual program and must then
be treated according to their real preprocessing effect.

---

# 104. `__cplusplus` and feature-test macros

The selected C++ language mode must expose ordinary `__cplusplus` behavior from
the selected toolchain.

Standard/library feature-test macros remain controlled by the actual C++/library
environment.

C++L must not falsify these macros merely to make a proof model available.

---

# 105. Vendor macros

Vendor and platform macros remain ordinary preprocessor inputs.

A proof depending on their selected branch is tied to that preprocessed program
and therefore to the corresponding compatibility environment.

---

# 106. Error-recovery compatibility

Compiler error recovery is not part of source compatibility.

A malformed program for which Clang creates a recoverable AST is still malformed.

C++L MUST NOT prove through semantically invalid C++ merely because an internal
frontend representation exists.

---

# 107. Diagnostic compatibility

C++L should preserve useful underlying C++ diagnostics while adding formal
diagnostics.

Exact diagnostic wording is not part of the language compatibility contract.

Structured diagnostic categories and source provenance should remain stable enough
for tooling.

A diagnostic difference is not automatically a source-semantic difference.

---

# 108. Exit-status/build-policy compatibility

The compiler driver may expose policies such as:

```text
allow ordinary unverified C++
require requested verification to succeed
require fully verified/trust-policy-clean build
```

These policies affect build acceptance.

They do not change theorem meaning.

A program rejected by strict policy may remain valid ordinary C++.

---

# 109. Security hardening flags

Hardening options such as stack protectors, control-flow protection or compatible
linker hardening normally affect generated runtime code without redefining the
source theorem.

Where a flag changes observable language semantics or ABI relevant to the claim,
it must be promoted into the semantic profile.

Classification is based on semantic effect, not flag category/name.

---

# 110. Profile manifest

Every production release SHOULD publish a machine-readable compatibility manifest
covering the profiles it advertises.

A profile record should include:

```text
profile identifier
C++L language version
supported C++ source modes
Clang/LLVM version range
host platforms
target triples
ABI families
standard-library families/versions
supported semantic compiler flags
extension classifications
module/PCH compatibility constraints
formal-library-model versions
proof-artifact format/core versions
known profile exclusions
```

`STATUS.md` may point to the manifest for exact current implementation coverage.

The manifest refines this document; it cannot weaken its semantic requirements.

---

# 111. Compatibility tiers

A release may classify profiles into tiers.

Recommended meanings:

```text
Tier 1
    release-blocking, full conformance and ABI test coverage

Tier 2
    supported, tested regularly, not required on every developer host

Experimental
    intentionally incomplete; no production compatibility guarantee
```

Tier labels are support-policy metadata.

They do not weaken proof soundness: an Experimental verifier still must fail
closed rather than accept unsound proofs.

---

# 112. Required conformance gates

A profile may be advertised as production-supported only after passing the
applicable gates.

At minimum:

```text
ordinary C++ source conformance
contextual-keyword compatibility
preprocessor/macro compatibility
selected standard-mode tests
ABI equivalence tests
refinement erasure/ABI tests
header/TU metadata tests
template-specialization identity tests
exception/RTTI profile tests where enabled
target arithmetic/data-model tests
standard-library model tests
cross-TU verification tests
artifact invalidation tests
differential Clang behavior tests
negative/soundness tests
```

Platform profiles additionally require native execution tests on the target or an
accepted equivalent execution environment.

---

# 113. Differential C++ conformance

For source that requests no C++L semantics, C++L should compare against the
selected Clang profile.

Conceptually:

```text
clang++ <profile> source
cppl    <profile> source
```

must agree on the ordinary C++ program meaning within the guarantees C++L claims.

Useful comparisons include:

- accept/reject result;
- resolved symbols/types where instrumented;
- ABI/layout;
- runtime behavior for conformance fixtures;
- generated native interface.

Byte-identical optimized machine code is not required.

Semantic equivalence is.

---

# 114. ABI conformance tests

ABI tests should verify, where applicable:

```text
sizeof
alignof
offsetof
triviality/properties relevant to ABI
name mangling
calling convention
parameter/return passing
vtable/RTTI interoperability
exception interoperability
C ABI symbols
shared-library boundaries
```

Proof-only changes must not alter these results.

Refinement and ghost tests are especially important because they are intended to
erase.

---

# 115. Standard-mode conformance matrix

The production source-mode matrix is:

| C++L feature category                   | C++17                                         | C++20                        | C++23                                              |
| --------------------------------------- | --------------------------------------------- | ---------------------------- | -------------------------------------------------- |
| Core C++L propositions/Laws/proofs      | required                                      | required                     | required                                           |
| Contracts/refinements                   | required                                      | required                     | required                                           |
| Indexed refinements                     | required                                      | required                     | required                                           |
| Templates                               | required                                      | required                     | required                                           |
| C++ concepts interaction                | not applicable                                | required                     | required                                           |
| C++ modules interaction                 | not applicable                                | profile-dependent            | profile-dependent                                  |
| `std::variant` / `std::optional` models | required where standard library provides them | required                     | required                                           |
| `std::expected` model                   | not part of standard profile                  | not part of standard profile | required where library provides conforming support |
| ABI erasure guarantees                  | required                                      | required                     | required                                           |

“Not applicable” does not mean the source spelling is emulated in an earlier
standard.

---

# 116. Core platform target matrix

Before C++L 1.0 may claim full production portability, the intended Tier-1
compatibility matrix is:

```text
macOS arm64     C++17 / C++20 / C++23
macOS x86_64    C++17 / C++20 / C++23

Linux x86_64    C++17 / C++20 / C++23
Linux AArch64   C++17 / C++20 / C++23

Windows x86_64  C++17 / C++20 / C++23
```

`STATUS.md` records which rows are currently implemented and passing.

Additional targets do not alter language semantics.

---

# 117. Reference standard-library matrix

The target runtime-library matrix is:

```text
macOS
    libc++

Linux
    libc++ and/or libstdc++ according to release profile

Windows
    MSVC STL under the supported Clang/MSVC ABI profile
```

Formal-library models must remain based on public semantics where possible so
that one logical model can serve multiple implementations without relying on
private layout.

Where implementation behavior is relevant, the model/profile must say so.

---

# 118. AppleClang

AppleClang may be used by ordinary Apple toolchains and may be supported in
specific host/target workflows.

It is not automatically equivalent to upstream Clang of the same apparent
language-feature level.

A production C++L profile using AppleClang as semantic authority must be declared
and tested independently.

Using AppleClang only as an external platform tool does not make it the proof
semantic authority.

---

# 119. GCC

GCC-compiled libraries may interoperate through a compatible platform ABI.

C++L does not currently define GCC as the reference semantic authority merely
because such objects link.

A future GCC semantic profile would require explicit compatibility and trust
definition.

---

# 120. MSVC

MSVC-produced libraries may interoperate on Windows where the selected Clang/MSVC
ABI profile supports that interoperability.

MSVC is not automatically the C++L semantic authority.

A future MSVC semantic profile requires explicit definition rather than inference
from ABI compatibility.

---

# 121. Cross-compilation

Cross-compilation is supported architecturally when the declared target profile
is available.

The compiler must obtain all proof-relevant target properties from the target
configuration.

Cross-compilation artifacts are target-specific.

A proof about:

```text
x86_64 Linux
```

must not be reused for:

```text
AArch64 Linux
```

merely because source is identical when machine semantics affect the theorem.

---

# 122. Sysroots and SDKs

A sysroot/SDK is part of the effective target environment.

Headers, feature macros, library declarations, ABI and target runtime interfaces
may change with SDK version.

Where those differences affect the preprocessed/semantic program or modeled
library interface, the SDK identity/version participates in compatibility and
artifact invalidation.

---

# 123. C runtime and C++ runtime versions

The native C/C++ runtime environment may change independently from C++L.

Where a proof depends only on standard-guaranteed semantics, compatible runtime
updates need not invalidate the proof.

Where a model depends on implementation-specific behavior, the relevant runtime
version becomes part of the model/profile.

C++L must not guess that distinction after the fact.

---

# 124. Operating-system APIs

Operating-system APIs are foreign/external interfaces.

Source and ABI compatibility can exist without formal semantics.

Proofs about OS behavior require:

- explicit formal model;
- trusted contract;
- runtime validation;
- or another permitted boundary.

The operating system is not silently treated as verified.

---

# 125. Environment-dependent behavior

Programs may observe environment properties such as:

- locale;
- filesystem;
- process environment;
- time;
- random sources;
- network;
- device state.

Compatibility with those APIs is runtime compatibility.

Formal properties require explicit modeled assumptions or runtime facts.

C++L does not turn environment nondeterminism into a compile-time constant.

---

# 126. `volatile`

`volatile` remains C++ semantics, including implementation/target-specific
behavior where applicable.

It is not a synchronization primitive unless C++ says so.

Verified reasoning involving volatile access requires a sound model appropriate
to the target/claim.

Otherwise the stronger proof claim fails closed.

---

# 127. Atomics

C++ atomic types/operations remain C++ memory-model semantics.

Their source/runtime availability follows the selected standard/library/target.

A proof that ignores memory order or interference is not compatible with a
concurrent execution merely because the code compiles.

Concurrency verification support is distinct from source compatibility.

---

# 128. Threads

`std::thread`, platform threads and synchronization primitives remain ordinary
runtime APIs.

The ability to link/run them does not imply that a theorem over shared state has
been proved concurrently.

This distinction must remain visible in compatibility claims.

---

# 129. Filesystem/network libraries

Standard and third-party filesystem/network facilities are ordinary runtime
libraries.

C++L adds no hidden sandbox/runtime.

Formal reasoning about their effects requires explicit models/boundaries.

Their mere availability does not change the proof calculus.

---

# 130. Deterministic proof results across compatible profiles

Two profiles may be runtime/source compatible yet produce different proof results
when the semantics relevant to a theorem differ.

For example:

```text
different target width
different FP mode
different library feature availability
```

That is not nondeterminism.

Within one fixed semantic profile and identical formal inputs, the accepted
proof result must be deterministic.

---

# 131. Compatibility failure behavior

When the requested environment is outside a supported profile, the compiler must
choose an explicit result such as:

```text
unsupported target/profile
unsupported semantic flag
unsupported extension for verification
incompatible proof artifact
missing library model
missing cross-TU metadata
```

It MUST NOT silently pretend the closest known profile applies.

---

# 132. Ordinary compilation versus verification failure

A construct may be valid ordinary C++ while unavailable to verified reasoning.

The toolchain must distinguish:

```text
C++ source error
```

from:

```text
verification/model unsupported
```

where the surrounding language/policy permits ordinary unverified code.

Conversely, if a `verified` construct requires semantics the implementation cannot
model, that verification must fail closed.

---

# 133. No compatibility by hidden runtime checks

C++L MUST NOT preserve apparent proof compatibility by inserting runtime checks
that the programmer did not write.

For example, an unsupported refinement crossing cannot be made “compatible” by
silently inserting a validator.

Runtime checking remains an explicit runtime mechanism.

---

# 134. No compatibility by hidden trust

An unsupported external library or compiler extension cannot become
proof-compatible by silently trusting its behavior.

Trust must use the explicit trusted surface defined by `SPEC.md`.

Compatibility policy is not a trust-admission mechanism.

---

# 135. No compatibility by weakening semantics

A platform/toolchain profile cannot be supported by:

- dropping an obligation;
- changing signed overflow to mathematical arithmetic;
- treating non-null as readable;
- ignoring exceptional exits;
- ignoring template specialization identity;
- ignoring concurrency;
- erasing trust provenance.

If exact semantics cannot be preserved, the stronger verified feature is
unsupported in that profile.

---

# 136. Relationship to erasure

Compatibility is strongest when proof-only constructs erase without changing
runtime behavior.

The runtime program is ordinary C++ under the selected profile.

This means compatibility testing must check both:

```text
source/formal side
```

and:

```text
erased/native side
```

A proof-only feature that changes runtime ABI is a compatibility defect unless
`SPEC.md` intentionally defines that runtime change.

---

# 137. Relationship to TRUST.md

A compatible environment is not necessarily a fully trusted environment.

For end-to-end `PROVEN` claims, the relevant C++ semantic authority, native
compiler/backend, linker, runtime and modeled external components participate in
the trust chain described by `TRUST.md`.

Compatibility answers:

```text
is this environment supported and semantically classified?
```

Trust answers:

```text
what correctness assumptions remain?
```

---

# 138. Relationship to FOUNDATIONS.md

Formal proof rules are independent of platform details except where formal terms
model platform-dependent C++ values/operations.

For example:

```text
logical implication
```

does not change between x86_64 and AArch64.

But:

```text
sizeof(long)
machine integer range
pointer width
```

may.

Compatibility selects the concrete C++ semantic environment in which the formal
model is interpreted.

---

# 139. Relationship to ARCHITECTURE.md

The architecture must ensure that one compatibility profile flows consistently
through:

```text
driver
preprocessor
projection
Clang semantic bridge
VIR
obligation generation
proof artifact identity
erasure validation
code generation
linking
```

No stage may silently substitute a different target/language/library
configuration.

---

# 140. Relationship to STATUS.md

This document defines the compatibility target and compatibility categories.

`STATUS.md` records implementation maturity such as:

```text
implemented
prototype
partial
not implemented
temporarily refused
passing on CI
```

Therefore this document intentionally contains no section named:

```text
Implemented compatibility
```

A temporary absence of Windows support, module support, a library model, or a
specific extension belongs in `STATUS.md`.

---

# 141. Relationship to release manifests

Exact patch-level toolchain/SDK/runtime support can change more frequently than
this architectural compatibility specification.

A release manifest may narrow a broad family from this document to exact tested
versions.

For example:

```text
COMPATIBILITY.md
    upstream Clang 22.x family

release manifest
    exact tested Clang 22.x patch releases
```

A release manifest MUST NOT broaden semantics beyond what this document allows
without a compatibility-spec update.

---

# 142. Compatibility review checklist

Before adding or changing a supported compatibility profile, verify:

1. Which C++ source modes are supported?
2. Which semantic-authority/toolchain version is used?
3. Is preprocessing performed under the same effective configuration?
4. What is the target triple and data model?
5. What ABI family applies?
6. Which standard library and version/features apply?
7. Which compiler extensions are enabled?
8. Which semantic flags differ from defaults?
9. Are exceptions and RTTI enabled?
10. Is floating-point mode modeled correctly?
11. Are integer widths/promotions/conversions correct?
12. Are target intrinsics involved?
13. Does the profile preserve contextual C++ source compatibility?
14. Do refinements/ghost/proof-only constructs erase without ABI change?
15. Are templates verified with correct specialization identity?
16. Are headers/modules/PCH semantically bound to the profile?
17. Is cross-TU proof metadata compatible?
18. Are stdlib models selected by semantic identity?
19. Can ordinary unverified libraries still link normally?
20. Are FFI boundaries explicit?
21. Are proof artifacts invalidated on profile changes?
22. Are runtime/codegen semantics aligned with verification semantics?
23. Do differential C++ conformance tests pass?
24. Do ABI tests pass?
25. Do soundness/negative tests pass?
26. Is the profile reflected accurately in `STATUS.md` and release metadata?

---

# 143. Prohibited compatibility shortcuts

The following are explicitly prohibited.

## 143.1 “Clang accepts it, therefore verified”

```text
Clang source acceptance
    !=
formal-model availability
```

## 143.2 “It links, therefore trusted”

```text
native ABI compatibility
    !=
verified behavior
```

## 143.3 “Same source, therefore same proof artifact”

```text
same text
    !=
same target/profile semantics
```

## 143.4 “Same width, therefore same ABI”

ABI depends on more than integer/pointer width.

## 143.5 “Same standard mode, therefore same semantic profile”

Toolchain, target, flags, macros and library environment also matter.

## 143.6 “Erased code compiles elsewhere, therefore end-to-end proof carries over”

A different compiler may require a new runtime-correspondence argument.

## 143.7 “Unsupported extension can be approximated”

Approximation must never strengthen proof.

## 143.8 “Runtime validation can repair compatibility silently”

Runtime checks must remain explicit runtime code.

## 143.9 “Trust can repair missing compatibility metadata”

Explicit trusted assumptions may state external facts, but they do not change the
identity of the compiler/ABI/profile under which a proof was produced.

---

# 144. Production compatibility contract

A production-supported profile must satisfy all of the following:

```text
ordinary supported C++ remains ordinary C++

C++L contextual syntax does not globally reserve ordinary identifiers

preprocessing and C++ semantic analysis use the declared environment

verification reasons about the selected target, not the host

Clang remains the declared ordinary-C++ semantic authority for the reference profile

proof-only constructs preserve native ABI

refinements erase to their underlying C++ representation

proof metadata remains separate from runtime ABI

template specialization identity is preserved

standard-library runtime support is distinct from formal-model support

compiler extensions are explicitly classified

exceptions, RTTI, floating point and semantic flags are profile dimensions

foreign code remains an explicit boundary

cross-TU verification metadata is versioned and validated

proof artifacts never cross incompatible semantic profiles silently

ordinary build systems and native linkers remain usable

no theorem runtime is required

unsupported proof semantics fail closed
```

---

# 145. Final compatibility rule

The fundamental compatibility distinction is:

```text
can parse
can compile
can link
can execute
can model
can prove
```

These are six different claims.

C++L succeeds only if it preserves the earlier C++ claims without pretending they
automatically imply the later formal claims.

The compatibility rule is therefore:

> Preserve ordinary C++ exactly within the declared profile, attach formal
> semantics only where those semantics are sound, keep verification metadata
> separate from native ABI, and fail closed whenever the requested proof exceeds
> the profile's modeled semantics.

That is how C++L remains both:

```text
a C++ superset
```

and:

```text
a sound verification language.
```

---

# Annex A — Compatibility profile schema

A production profile should be representable approximately as:

```yaml
profile:
  id: string

  cppl:
    language_version: string
    formal_core_version: string
    proof_artifact_version: string
    stdlib_model_version: string

  cpp:
    standard: c++17 | c++20 | c++23
    semantic_authority: clang
    compiler_family: llvm-clang
    compiler_major: 22

  host:
    os: string
    arch: string

  target:
    triple: string
    abi_family: string
    data_model: string
    endianness: string
    char_signedness: string

  runtime:
    standard_library: libc++ | libstdc++ | msvc-stl | other
    standard_library_version: string
    c_runtime: string

  language:
    exceptions: boolean
    rtti: boolean
    extensions: [string]
    semantic_flags: [string]
    floating_point_mode: string

  verification:
    library_models: [string]
    extension_models: [string]
    foreign_models: [string]

  support:
    tier: tier1 | tier2 | experimental
```

The exact serialization format is non-normative.

The semantic fields are not.

---

# Annex B — Compatibility class matrix

| Feature             | Ordinary source/runtime compatibility | Verified semantics requirement               |
| ------------------- | ------------------------------------- | -------------------------------------------- |
| Ordinary arithmetic | supported C++ profile                 | exact machine/UB model                       |
| Templates           | ordinary Clang templates              | generic or per-specialization proof          |
| Concepts            | C++20+ source semantics               | not automatically proof evidence             |
| `constexpr`         | ordinary C++                          | checked correspondence before reuse          |
| Exceptions          | ordinary C++                          | explicit exceptional-flow model for proof    |
| RTTI                | ordinary C++                          | proof model required for RTTI facts          |
| Inline assembly     | profile extension                     | unsafe/boundary unless modeled               |
| Standard library    | native runtime/library                | formal model only for claimed facts          |
| C library           | ordinary ABI                          | explicit verified/trusted/validated contract |
| Objective-C++       | optional Clang profile                | runtime boundary models as needed            |
| JNI                 | native FFI                            | explicit boundary                            |
| N-API               | native FFI                            | runtime validation/contracts                 |
| WASM                | optional target profile               | target-specific machine model                |
| Sanitizers          | runtime instrumentation               | not proof evidence                           |
| LTO                 | backend optimization                  | not proof evidence                           |
| Ghost/proof state   | compile-time only                     | erased                                       |
| Refinements         | erased base ABI                       | crossing/validity proof required             |

---

# Annex C — Semantic flag classification

The implementation should classify relevant compiler flags into:

```text
A — analysis/runtime semantics changing
B — ABI/data-layout changing
D — diagnostic/build-policy only
O — optimization only under preserved semantics
I — instrumentation
U — unsupported/unclassified
```

Examples:

| Flag/category                       | Typical class | Compatibility action                     |
| ----------------------------------- | ------------- | ---------------------------------------- |
| `-std=c++17/20/23`                  | A             | profile key                              |
| target triple                       | A+B           | profile key                              |
| `-fwrapv`                           | A             | model or reject for affected proofs      |
| `-ffast-math`                       | A             | model/classify explicitly                |
| `-fno-exceptions`                   | A             | profile key                              |
| `-fno-rtti`                         | A             | profile key                              |
| `-fsigned-char` / `-funsigned-char` | A             | target/profile key where relevant        |
| `-fshort-enums`                     | B             | ABI/profile key                          |
| packing/alignment flags             | B             | ABI/profile key                          |
| `-Wall`                             | D             | preserve diagnostic behavior             |
| `-Werror`                           | D             | build-policy behavior                    |
| `-O0`…`-O3`                         | O             | no theorem change under normal semantics |
| `-fsanitize=*`                      | I             | runtime instrumentation, not proof       |
| unknown proof-relevant flag         | U             | fail closed / refuse verified semantics  |

The exact classification of a flag follows its actual semantics, not its name.

---

# Annex D — ABI verification checklist

For a proof-only feature, verify that adding/removing only the formal annotation
does not change, where applicable:

```text
sizeof(type)
alignof(type)
object representation
member offsets
base offsets
triviality/standard ABI traits
function symbol name
parameter passing
return passing
calling convention
vtable layout
RTTI identity
exception ABI
C ABI linkage
shared-library symbol interface
```

Refinement aliases require additional collision tests because two verification
types may erase to one native C++ type.

---

# Annex E — Release profile minimum evidence

A production release profile should have automated evidence for:

```text
source conformance
preprocessor conformance
contextual-word regressions
C++17 matrix
C++20 matrix
C++23 matrix
target data-model tests
ABI tests
stdlib compatibility/model tests
template specialization tests
cross-TU summary tests
artifact invalidation tests
exceptions/RTTI mode tests
semantic-flag tests
erasure equivalence tests
ordinary-library link tests
FFI smoke tests
differential Clang tests
negative/soundness tests
```

The release may provide additional platform-specific evidence.

---

# Annex F — Compatibility decision rule

For any new C++ feature, extension, platform or library, classify it using this
sequence:

```text
1. Is it valid ordinary C++/extension in the selected environment?

    no  -> source unsupported
    yes -> continue

2. Can the erased runtime program preserve that behavior?

    no  -> incompatible
    yes -> continue

3. Does native ABI interoperate as required?

    no  -> runtime/link profile unsupported
    yes -> continue

4. Does C++L have sound formal correspondence for the facts requested by proof?

    no  -> ordinary/unverified or explicit boundary only
    yes -> continue

5. Are all proof/runtime metadata versions and semantic inputs compatible?

    no  -> reverify / reject stale artifact
    yes -> verified use permitted
```

This decision tree prevents the common mistake of treating C++ source acceptance
as equivalent to formal verification support.
