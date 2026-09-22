# C++L Design

**C++L — C++ with Laws**

Status: Non-normative design rationale

This document records the design rationale of C++L: the problems the language is
trying to solve, the principles that constrain the solution, the major choices
that have been made, the alternatives that were rejected, and the tradeoffs that
future proposals should preserve.

This document is intentionally **non-normative**.

It does not define legal syntax, language semantics, proof rules, trust
classification, supported C++ modes, implementation status, or compiler topology.
Those responsibilities belong to other documents.

The purpose of this document is to answer questions such as:

- Why extend C++ rather than replace it?
- Why are Laws language constructs rather than comments or test conventions?
- Why are proofs, assumptions, runtime checks, and unsafe code kept distinct?
- Why does C++L reason about the real C++ object and execution model?
- Why do refinements erase to their base representation?
- Why are mathematical domains visibly separate from machine types?
- Why does structural proof reason over existing C++ types instead of introducing
  a second runtime data model?
- Why is semantic fidelity preferred over permissive but approximate verification?
- Why is a small proof checker desirable but not sufficient to describe the whole
  end-to-end Trusted Computing Base?
- Why is the language designed to work well with automated and AI-generated code
  without trusting automation as an authority?

When this document gives code, the code illustrates a design decision. The
authoritative syntax is defined by `GRAMMAR.md`, and the authoritative meaning is
defined by `SPEC.md`.

---

# 1. Document boundaries and authority

C++L deliberately separates specification, rationale, trust, mathematics,
architecture, compatibility, and implementation status.

| Document                               | Responsibility                                                  |
| -------------------------------------- | --------------------------------------------------------------- |
| [SPEC.md](./SPEC.md)                   | Normative language semantics                                    |
| [GRAMMAR.md](./GRAMMAR.md)             | Normative concrete syntax                                       |
| [FOUNDATIONS.md](./FOUNDATIONS.md)     | Formal calculus and mathematical basis                          |
| [TRUST.md](./TRUST.md)                 | Trusted Computing Base and assurance boundaries                 |
| [COMPATIBILITY.md](./COMPATIBILITY.md) | Supported C++ modes, toolchains, ABI and platform compatibility |
| [ARCHITECTURE.md](./ARCHITECTURE.md)   | Compiler structure and implementation data flow                 |
| [STATUS.md](./STATUS.md)               | Actual implementation coverage                                  |
| [ROADMAP.md](./ROADMAP.md)             | Implementation sequencing, when present                         |
| `DESIGN.md`                            | Why the project chose its present direction                     |

The distinction matters.

A design rationale may explain why a rule is desirable, but it cannot weaken,
extend, or reinterpret that rule. A historical implementation limitation is not a
design principle. A convenient compiler architecture is not a language semantic.
A test that happens to pass is not an authority over the specification.

If this document conflicts with a normative document, the normative document
wins.

---

# 2. The problem C++L is solving

C++ is exceptionally strong at expressing how software executes.

It provides direct control over:

- representation;
- object lifetime;
- allocation;
- ownership;
- native calling conventions;
- hardware interaction;
- operating-system interfaces;
- templates and generic programming;
- deterministic destruction;
- low-level performance;
- interoperability with enormous existing ecosystems.

What ordinary C++ does not provide as a first-class language layer is a general
way to state and mechanically establish reusable propositions about that
execution.

Important program requirements are therefore often spread across:

- prose;
- comments;
- code review;
- unit tests;
- property tests;
- runtime assertions;
- static analyzers;
- conventions;
- type encodings;
- institutional knowledge.

All of those mechanisms remain useful. The missing capability is different.

A project may want to state something like:

```text
for every input satisfying these assumptions,
this operation preserves this invariant
```

and then have the toolchain establish that claim compositionally rather than
merely exercise examples of it.

C++L exists to add that layer without discarding C++ as the execution language.

---

# 3. The central thesis: Provable C++

The shortest description of the project is:

> C++L is an attempt to make important properties of C++ programs stateable and
> mechanically provable while retaining C++ as the runtime language.

Conceptually:

```text
C++ runtime program
+
formal intent
+
machine-checkable evidence
```

The runtime program remains the thing that is compiled and shipped.

The formal layer exists to make claims about that program explicit, reusable,
composable, and independently checkable.

This produces the fundamental design relationship:

```text
C++ ⊂ C++L
```

and the corresponding erasure direction:

```text
erase : C++L -> C++
```

The exact semantic requirements are normative in `SPEC.md`. The design point is
that C++L is not trying to create a second runtime language beside C++.

---

# 4. Why C++L extends C++ instead of replacing it

A clean-sheet verification language could simplify many problems.

It could:

- use a smaller type system;
- ban undefined behavior by construction;
- control aliasing globally;
- use a simpler memory model;
- require immutable values;
- define a proof-friendly standard library;
- avoid C++ preprocessing;
- avoid C++ overload resolution;
- avoid C++ ABI constraints.

That would also abandon much of the reason people use C++.

Existing C++ systems depend on:

```text
existing source
existing headers
existing templates
existing libraries
existing binary interfaces
existing build systems
existing debuggers
existing profilers
existing platform SDKs
existing operating-system interfaces
existing hardware integrations
```

C++L deliberately accepts the harder technical problem because its intended value
is verification **inside the C++ ecosystem**.

The project therefore treats compatibility pressure as a fundamental design
constraint rather than an inconvenience to be removed.

---

# 5. Why C++L is a language, not only an external verifier

An external verifier can prove useful properties of C++.

That alone does not require a new language.

C++L becomes a language because it wants programs to be able to carry
language-defined entities such as:

```text
propositions
Laws
proof declarations
proof evidence
formal quantification
refinement types
ghost state
termination measures
trusted assumptions
```

with common semantics that different source files, libraries, tools, and proof
producers can share.

The distinction is important.

An external verifier may answer:

```text
Does this program satisfy P?
```

C++L also wants the language to represent:

```text
P
```

and:

```text
evidence for P
```

so that the result can participate in another proof.

That turns verification results into reusable interfaces rather than isolated
tool outcomes.

---

# 6. Why C++L is not merely a C++ library

C++ is powerful enough to encode almost anything eventually.

Templates, `constexpr`, concepts, macros, `static_assert`, attributes, and library
types can simulate many proof-like APIs.

A library might define something resembling:

```cpp
Proof<ForAll<X, P<X>>>
```

but the language itself does not know that such a type is proof evidence.

Its meaning depends on:

- the library implementation;
- conventions;
- template machinery;
- compiler behavior;
- user discipline.

At sufficient complexity, the library has implemented a logical language
indirectly inside C++.

C++L instead makes the logical layer explicit.

The design test is:

> If a feature can be expressed naturally and completely as ordinary C++ without
> introducing distinct logical semantics, it probably does not belong in C++L.

C++L should extend the language only where the new concept genuinely crosses from
ordinary computation into specification or proof.

---

# 7. Why C++ remains the runtime authority

C++L does not introduce a second runtime interpretation for ordinary C++ syntax.

That is essential.

If the verification system reasons about one meaning while the native toolchain
executes another, internally valid proofs can become irrelevant to the shipped
program.

The intended relationship is therefore:

```text
ordinary C++ syntax
        |
        v
ordinary C++ semantics

C++L specification/proof layer
        |
        v
claims about those semantics
```

C++L may add compile-time-only meaning around C++ constructs, but it should not
quietly replace ordinary C++ runtime behavior with a proof-friendlier imaginary
machine.

---

# 8. Why adoption is incremental

Real C++ systems are heterogeneous.

One executable may contain:

- new verified code;
- old unverified code;
- generated source;
- third-party libraries;
- assembly;
- operating-system APIs;
- device interfaces;
- proprietary binaries.

Requiring the entire dependency graph to be proven before any local theorem is
useful would make adoption unrealistic.

C++L therefore favors **local guarantees with explicit boundaries**.

Conceptually:

```text
ordinary C++
    ↓
specified boundary
    ↓
verified region
    ↓
stronger verified subsystem
```

This does not mean all assurance levels are equivalent.

It means useful proof can exist without pretending the rest of the machine has
already been proved.

---

# 9. Claims, evidence, and assumptions are different things

A central design choice is to keep these three concepts separate:

```text
claim
evidence
assumption
```

A proposition being written in source does not make it true.

A proof establishes a claim.

A trusted declaration admits a claim as an explicit assumption.

This separation supports auditability.

A reviewer should be able to ask independently:

```text
What is being claimed?
Why is it accepted?
What assumptions does it depend on?
```

Collapsing those questions would make the verification result much harder to
interpret.

---

# 10. Why Laws are first-class

Function contracts are important, but not every stable property belongs to one
function.

Useful properties include:

- algebraic identities;
- relationships between operations;
- conservation rules;
- protocol invariants;
- state-transition properties;
- cross-function consistency;
- domain laws;
- properties of data representations.

A named Law gives such a property identity independent of whichever
implementation currently establishes it.

That is especially valuable when implementation code changes while the intended
property remains stable.

A Law can therefore act as a durable formal interface.

---

# 11. Why contracts and Laws are both needed

Contracts and Laws operate at different scales.

A function contract naturally answers:

```text
under which conditions may this function be called?
what does it guarantee on normal return?
```

A Law naturally answers:

```text
what reusable proposition does this module or domain establish?
```

Forcing every theorem into a function postcondition would make broader properties
awkward.

Forcing every local API condition into a standalone theorem would make routine
contracts unnecessarily indirect.

Keeping both concepts gives the language a local specification mechanism and a
reusable theorem mechanism.

---

# 12. Why proofs are explicit concepts

Proof evidence separates:

```text
finding a proof
```

from:

```text
checking a proof
```

That is strategically important because proof producers may vary dramatically.

Evidence may be produced by:

- a human;
- compiler automation;
- a rewriting engine;
- a decision procedure;
- an SMT solver;
- a specialized tactic;
- an AI agent;
- a future external prover.

Those producers can be large, heuristic, changing, or probabilistic.

The acceptance criterion should be substantially more stable.

This is why C++L distinguishes proof construction from proof validation.

---

# 13. Why automation is not the definition of truth

Strong automation is necessary for usability.

Manual proof of every trivial arithmetic or control-flow fact would make the
language impractical.

But the project does not want:

```text
the automation said yes
```

to be the ultimate semantics of proof.

The desired pattern is:

```text
powerful automation
        ↓
candidate evidence
        ↓
independent checking
```

The precise Trusted Computing Base is defined in `TRUST.md`; the design principle
is that search power and logical authority should be separated whenever
practical.

---

# 14. Why AI is outside the authority boundary

AI is expected to be extremely useful for C++L.

It can help:

- write implementations;
- propose contracts;
- discover invariants;
- generate proofs;
- repair failed proofs;
- generate negative tests;
- explain proof failures;
- refactor verified code.

But AI output is still candidate source and candidate evidence.

The desirable relationship is:

```text
human intent
    ↓
formal specification
    ↓
AI-generated candidate implementation/proof
    ↓
independent verification
```

This lets the project benefit from improving models without making model
reliability a premise of mathematical correctness.

It also gives AI a better target: satisfy explicit Laws rather than infer intent
from prose after the fact.

---

# 15. Why specification is not derived from implementation

A dangerous engineering pattern is:

```text
the implementation behaves this way
therefore the specification should say this
```

C++L is designed to support the opposite direction:

```text
this property is required
therefore the implementation must satisfy it
```

That distinction is especially important for agentic development.

An implementation agent should not be allowed to turn a missing feature,
temporary simplification, or currently green regression test into new language
meaning.

The specification represents intended semantics; `STATUS.md` represents progress
toward them.

---

# 16. Why purity is explicit

Formal reasoning is easier when a computation behaves like a mathematical
function.

C++ deliberately permits effects such as:

- mutation;
- I/O;
- volatile access;
- atomics;
- global state;
- resource ownership;
- external interaction.

Pretending all functions are mathematically pure would be dishonest.

C++L therefore makes purity explicit so stronger reasoning can be requested where
it is appropriate without imposing functional programming on ordinary C++.

---

# 17. Why purity and verification are separate

Purity does not imply correctness.

A pure function can return the wrong answer.

Conversely, an imperative function can satisfy a useful verified contract.

The concepts therefore answer different questions:

```text
pure
    what effects may this computation have?

verified
    what claimed properties have been established?
```

Keeping them separate prevents one modifier from carrying unrelated semantic
burdens.

---

# 18. Why termination is separate from purity

A function can have no side effects and still diverge.

That matters in proof-relevant computation because unrestricted nontermination can
invalidate logical reasoning.

At the same time, ordinary systems software often intentionally contains
nonterminating behavior:

- event loops;
- servers;
- schedulers;
- kernels;
- embedded controllers.

C++L therefore separates:

```text
partial correctness
```

from:

```text
total correctness
```

and gives termination its own proof role rather than hiding it inside purity.

---

# 19. Why proof-relevant computation must be terminating

Runtime divergence can be meaningful.

Proof construction cannot rely on divergence as if it were evidence.

The proof layer therefore needs a well-founded account of any computation whose
result participates in proof.

This is also why `decreases` is conceptually distinct from induction:

```text
decreases
    explains why computation terminates

induction
    explains why a proposition holds over a structure/domain
```

They may use related measures, but they solve different problems.

---

# 20. Why quantification belongs in the language

Many useful program properties are not about one concrete input.

They are statements such as:

```text
for every x, P(x)
```

or:

```text
there exists x such that P(x)
```

Encoding such claims as repeated tests or enumerated examples changes their
meaning.

First-class quantification allows Laws to express the intended scope directly and
allows proof evidence to compose according to that scope.

---

# 21. Why implication, conjunction, and disjunction are proof concepts

Boolean computation and logical reasoning are related but not identical.

A runtime Boolean expression computes a value under C++ evaluation semantics.

A logical connective combines propositions and evidence.

C++L needs both.

The design therefore gives specification contexts explicit logical meaning while
leaving ordinary runtime expressions under C++ semantics.

This distinction is especially important for:

- short-circuit evaluation;
- definedness;
- side effects;
- overloaded operators;
- proof decomposition.

The exact boundary is normative in `SPEC.md`.

---

# 22. Why equality has more than one notion

C++L distinguishes:

```text
definitional equality
propositional equality
ordinary C++ operator==
```

because they answer different questions.

Definitional equality is what the formal system can normalize to the same meaning.

Propositional equality is a statement requiring evidence.

C++ `operator==` is a runtime operation selected by C++ semantics.

Treating those as interchangeable would allow runtime overloads, conversions, or
implementation-defined behavior to leak into the proof calculus.

---

# 23. Why mathematical domains are visibly different from machine types

Executable integers are machine values.

Mathematical integers are not.

Likewise:

```text
std::vector<T>
```

is a runtime container, while an abstract sequence used in a specification has no
reason to inherit its allocator, capacity, iterator, lifetime, or ABI semantics.

C++L therefore uses visibly formal domains such as:

```text
@N
@Z
@Seq<T>
@Set<T>
@Map<K, V>
```

The `@` prefix makes the boundary visually and lexically obvious.

This prevents accidental claims that:

```text
int == mathematical integer
```

or:

```text
std::set<T> == mathematical set
```

and keeps proof-only abstraction separate from runtime representation.

---

# 24. Why refinements exist

Many runtime types represent much broader domains than an application actually
accepts.

Examples include:

```text
percentage
index
port number
non-negative count
validated identifier
bounded quantity
```

A refinement lets the logical type record that narrower domain.

This turns repeatedly re-proven conditions into reusable type-level facts.

The design goal is not to create wrapper objects.

A refinement is intended to add **verification identity and obligations** while
retaining the runtime representation of its base C++ type.

That gives stronger static reasoning without forcing a new runtime abstraction.

---

# 25. Why refinement validity belongs to values, not variable names

A source variable is not eternally equal to the value it once held.

Mutation matters.

Aliasing matters.

Calls matter.

A refinement fact therefore belongs to a logical value/version, not permanently
to the spelling of a variable or address of an object.

Conceptually:

```text
place
    stores
logical value version
    for which
refinement predicate is established
```

A later write creates a new value that needs its own justification.

This choice is what makes refinements compatible with ordinary imperative C++
instead of pretending C++ variables are immutable theorem constants.

---

# 26. Why semantic validity is recursive through objects

If an object contains a refinement-bearing subobject, the validity of the object
must account for that subobject.

For example:

```cpp
type Positive = int where (self > 0);

struct S {
    Positive x;
};
```

the verification meaning of a valid `S` includes the validity of `x`.

Otherwise a refined member would become weaker merely because it was nested
inside another C++ type.

The design therefore treats semantic validity recursively.

This supports modular reasoning at verified boundaries: a parameter whose type is
known valid may expose the refinement facts of its current valid subobjects
without requiring a proof of its entire historical construction provenance.

Mutation still invalidates the affected current-version facts.

---

# 27. Why unverified callers can violate erased refinement preconditions

Refinements are intended to erase.

That means an ordinary C++ caller can physically construct a representation whose
bytes do not satisfy the refinement predicate.

This is not a contradiction.

The verification guarantee is conditional on the semantic validity required at
the verified boundary, just as a normal precondition is conditional on the caller
satisfying it.

The alternative would require hidden runtime wrappers or validators, which would
break the project's erasure and ABI goals.

---

# 28. Why indexed refinements use declaration binders but C++-style application

An indexed refinement has two different syntactic roles.

Declaration introduces the index variable:

```cpp
type Index(unsigned n) = unsigned where (self < n);
```

Use applies the type constructor:

```cpp
Index<4u>
```

The distinction is intentional.

Parentheses are natural for binding the declaration's formal index.

Angle brackets integrate application with ordinary C++ type syntax, template
arguments, nested type positions, and dependent contexts.

Using `Index(4u)` as a type application would look like an expression or
functional cast and would create a second, unnecessary type-application syntax.

---

# 29. Why casts do not manufacture refinement

A C++ cast can change the runtime interpretation or representation of a value
according to C++ rules.

It does not prove an arbitrary predicate.

Therefore:

```text
runtime conversion
```

and:

```text
refinement introduction
```

must remain distinct operations in the verification model.

This is a recurring design theme: C++ operations keep their C++ meaning, while
logical authority comes only from established evidence.

---

# 30. Why runtime validation remains necessary

Some facts do not exist until execution.

Examples include values obtained from:

- networks;
- users;
- databases;
- files;
- sensors;
- operating-system calls;
- foreign libraries.

Compile-time proof cannot predict arbitrary future input.

The right model is therefore:

```text
runtime value
    ↓
ordinary C++ validation
    ↓
successful control-flow path
    ↓
stronger fact available to verification
```

C++L does not need a mandatory validation runtime to support this idea.

Ordinary C++ performs the check; the verifier reasons about the successful path.

---

# 31. Why failed proof does not silently become runtime validation

A failed static proof and a runtime check have different meanings.

Silently replacing one with the other changes:

- assurance;
- runtime behavior;
- failure mode;
- performance;
- deployment semantics.

That trade should never happen accidentally.

If the programmer wants a runtime check, it should be present as runtime C++.

If the programmer requested proof, inability to prove should remain inability to
prove.

---

# 32. Why storage needs a semantic identity separate from values

Imperative C++ distinguishes:

```text
where a value is stored
```

from:

```text
which value is currently stored there
```

Proof reasoning needs the same distinction.

A storage location may:

- be written;
- be aliased;
- have subobjects;
- change lifetime;
- be reached through references;
- be reached through pointers;
- be invalidated by calls.

This motivates the conceptual distinction between a **place** and a **logical
value version**.

The exact implementation representation belongs in `ARCHITECTURE.md`; the design
reason is that refinements and contracts must track changing storage without
confusing storage identity with immutable logical values.

---

# 33. Why member paths and projections need a common model

Special-casing:

```text
local
member
member of member
array element
reference target
pointer target
```

independently creates duplicated semantics.

C++L instead benefits from one conceptual storage model in which those forms are
different projections of storage.

That makes the important rules uniform:

```text
read current value
write new value
invalidate possible aliases
re-establish refinement validity
check capability before access
```

Uniformity is important for both soundness and implementation maintainability.

---

# 34. Why alias reasoning is conservative

Aliasing is one of the places where unsound optimism is particularly dangerous.

If the verifier assumes two places are disjoint when they may actually refer to
the same storage, it can preserve facts that runtime mutation has invalidated.

The preferred asymmetry is therefore:

```text
prove disjointness when justified
otherwise assume mutation may interfere
```

This can lose proof power.

It does not invent correctness.

That trade matches the project's general preference for honest incompleteness over
false proof.

---

# 35. Why type-based alias shortcuts are treated cautiously

C++ aliasing rules are entangled with defined behavior.

Using a type-based aliasing conclusion as a premise before the verifier has
established that the execution respects the relevant C++ rules can become
circular:

```text
assume program has no forbidden aliasing
therefore prove operation is safe
therefore conclude program has no relevant UB
```

C++L therefore favors alias conclusions grounded in resolved object/storage
identity and explicitly modeled semantics rather than optimistic assumptions that
presuppose the safety being proved.

---

# 36. Why memory capabilities are separate facts

A non-null pointer does not imply:

```text
live object
readable storage
writable storage
initialized value
sufficient extent
valid provenance
correct dynamic object
```

Those are different properties.

Treating all of them as consequences of:

```cpp
p != nullptr
```

would create a large hidden axiom.

C++L therefore separates pointer-state facts from memory-access capabilities.

This permits proofs to say exactly what is known rather than promoting one weak
fact into many stronger ones.

---

# 37. Why bounds are not the same as capabilities

Bounds are often arithmetic relationships:

```text
index < extent
```

while readability/writability/lifetime are properties of storage.

Mixing them into one undifferentiated "pointer valid" bit would make the model
less precise and harder to compose.

The design therefore favors separate reasoning:

```text
storage capability
+
arithmetic bounds proof
```

before an indexed access is justified.

---

# 38. Why `non_null` binds nothing

Case analysis over a pointer can establish:

```text
null
```

or:

```text
non_null
```

The non-null case intentionally does not bind a magical "valid address" object.

Doing so would encourage users and implementation code to treat non-nullness as a
capability.

The proof obtains exactly the fact being split on and no stronger one.

---

# 39. Why the C++ object model cannot be abstracted away casually

Real C++ correctness depends on:

- lifetime;
- active union member;
- subobject identity;
- references;
- aliasing;
- moves;
- destruction;
- pointer provenance;
- initialization;
- virtual dispatch;
- object construction;
- exception paths.

A verifier can become simpler by replacing those semantics with an idealized
machine.

It also becomes less relevant to actual C++.

C++L therefore chooses progressive fidelity to the real object model rather than
pretending difficult C++ semantics do not exist.

---

# 40. Why structural reasoning uses existing C++ types

Proofs need structural operations such as:

- case analysis;
- exhaustiveness;
- decomposition;
- induction.

An earlier possible direction was to introduce a second family of algebraic data
types and runtime-looking pattern matching.

That would duplicate the runtime type model and move proof away from the program
that actually executes.

C++L instead reasons over existing C++ structures where a sound logical model is
available.

Examples include:

- scoped enums;
- `std::variant`;
- `std::optional`;
- `std::expected`;
- pointers;
- records;
- tuples;
- arrays.

The proof language adds proof-only structural operations rather than a parallel
runtime data language.

---

# 41. Why case analysis must cover the real C++ state space

A proof is only exhaustive if its cases correspond to every runtime state the
modeled type can actually inhabit.

That means convenient source-level intuition is not always enough.

Examples:

- a scoped enum may hold an underlying value with no enumerator;
- a `std::variant` may be valueless;
- an optional may be disengaged;
- a pointer may be null.

Residual states therefore matter.

If the proof language ignored them, it could prove a theorem about a smaller
fictional datatype rather than the C++ object.

---

# 42. Why residual cases are named rather than hidden by a wildcard

A wildcard makes proofs brittle in a subtle way.

Suppose a proof handles all known enum alternatives with:

```text
_
```

as the fallback.

Adding a new named state later may silently flow through that old fallback.

C++L prefers explicit residual cases so that structural evolution causes proofs to
be revisited when appropriate.

The intent is not verbosity for its own sake.

The intent is to make exhaustiveness maintainable.

---

# 43. Why decomposition and case analysis are different

A sum-like value has alternatives.

A product-like value has components.

Those are different proof operations.

Case analysis divides the state space.

Product decomposition merely exposes components of the one current state.

Keeping them separate avoids treating every record as if it represented multiple
alternatives and makes proof obligations correspond more directly to the
structure being reasoned about.

---

# 44. Why induction is proof-only

Induction is a way to establish a proposition over a recursively or
well-foundedly structured domain.

It is not a runtime pattern-matching construct.

Keeping induction proof-only avoids:

- new runtime control flow;
- ABI questions;
- duplicated data representations;
- confusion between recursive execution and proof reasoning.

The runtime program continues to use ordinary C++ constructs.

---

# 45. Why ghost state exists

Some useful proof concepts have no reason to survive execution.

Examples include:

- snapshots;
- logical histories;
- auxiliary counters;
- abstract resource tokens;
- witnesses;
- proof bookkeeping.

Representing those concepts as normal runtime objects could add:

- storage;
- construction;
- destruction;
- side effects;
- ABI differences.

Ghost state gives proofs an auxiliary vocabulary while preserving the principle
that proof-only information should not alter runtime behavior.

---

# 46. Why ghost operations are restricted

Erasure is only sound if removing ghost state cannot change observable execution.

Therefore the design of ghost state cannot permit:

```text
runtime branch depends on ghost
runtime return depends on ghost
ghost constructor performs observable I/O
ghost destructor mutates runtime state
ghost address escapes to runtime
```

The exact rules are normative in `SPEC.md`.

The rationale is simple: proof-only state is allowed precisely because execution
does not depend on it.

---

# 47. Why `unsafe` exists

Systems programming reaches operations a verifier may not model completely.

Examples include:

- inline assembly;
- hardware registers;
- platform intrinsics;
- low-level allocators;
- foreign runtimes;
- special pointer manipulations.

Rejecting every such program would make the language unusable.

Pretending those operations are proven would make the language dishonest.

An explicit `unsafe` boundary records that the strongest guarantee stops there.

---

# 48. Why `unsafe` does not create proof

"Not proven" and "assumed true" are fundamentally different states.

If `unsafe` automatically generated logical facts, any low-level operation could
become a proof escape hatch.

C++L therefore keeps:

```text
unsafe
```

separate from:

```text
trusted
```

`unsafe` acknowledges missing verification.

Trust explicitly admits a proposition.

That difference is central to auditability.

---

# 49. Why trusted assumptions exist

No practical verification system proves the universe from first principles.

Eventually a program relies on something external:

- hardware behavior;
- an operating-system contract;
- an external library;
- a protocol guarantee;
- a device specification;
- a cryptographic assumption;
- environmental behavior.

The wrong response is to hide that dependence.

C++L therefore permits explicit trust so that assumptions can be named, reviewed,
tracked, and propagated.

The exact trusted surface and trust closure are defined by `SPEC.md` and
`TRUST.md`.

---

# 50. Why trust is not a fallback for failure

A proof failure is evidence that the requested derivation has not been
established.

Automatically converting:

```text
UNRESOLVED
```

into:

```text
TRUSTED
```

would make trust invisible and make the language's assurance levels meaningless.

Explicit trust must be an intentional source-level act, not an error-recovery
strategy.

---

# 51. Why assurance states remain distinct

A mature verification workflow needs more information than:

```text
green / red
```

Different code may be:

- proven;
- dependent on explicit trust;
- runtime-checked;
- unsafe;
- unverified;
- unresolved.

Those states describe different assurance.

Keeping them distinct helps:

- code review;
- CI policy;
- release auditing;
- security analysis;
- trust reporting;
- agentic workflows.

The normative status meanings belong in `SPEC.md`.

---

# 52. Why machine arithmetic is modeled as machine arithmetic

Replacing executable integer arithmetic with mathematical integers silently is
tempting because proofs become simpler.

It is also wrong for many C++ programs.

Machine arithmetic involves:

- finite widths;
- promotions;
- conversions;
- wrapping where defined;
- undefined overflow where applicable;
- shifts;
- bit operations;
- signedness.

C++L therefore reasons about the machine operation that actually executes unless
the programmer deliberately moves into a mathematical domain.

This keeps proof relevant to runtime behavior.

---

# 53. Why signed overflow cannot be hand-waved away

For ordinary signed C++ arithmetic, "the mathematical result exists" does not imply
"the C++ operation has defined behavior."

A verification system that proves:

```text
a + b = c
```

while ignoring reachable signed overflow may be proving a theorem about a
different language.

Definedness therefore has to participate in proof.

This is representative of a broader design rule:

> Verification may abstract C++ semantics only when the abstraction is sound for
> the property being claimed.

---

# 54. Why floating point is not real arithmetic

C++ floating-point execution includes:

- rounding;
- finite precision;
- infinities;
- NaNs;
- signed zero;
- target and mode effects.

Treating floats as exact real numbers may produce elegant but irrelevant
theorems.

The project therefore prefers an explicit abstraction boundary when real-number
reasoning is desired rather than silently changing the meaning of floating
runtime values.

---

# 55. Why undefined behavior is central to proof

Formal reasoning requires a meaningful execution to reason about.

Reachable undefined behavior can destroy that foundation.

A verifier that proves postconditions while ignoring an earlier UB path risks
proving statements about an execution that C++ does not define.

This is why C++L treats defined behavior as part of the verification problem, not
as an unrelated static-analysis concern.

---

# 56. Why exceptions need explicit semantic treatment

Exceptions create control-flow and lifetime behavior that differs from normal
return.

A normal postcondition cannot automatically be assumed on an exceptional exit.

Likewise:

- partially constructed objects;
- destructors during unwinding;
- mutation before throw;
- exception specifications;

can affect what remains true.

C++L therefore avoids treating exceptions as merely another hidden branch of
normal-return reasoning.

---

# 57. Why concurrency cannot be proven with sequential reasoning

Threads, atomics, locks, memory ordering, and data races introduce semantics that
do not reduce to ordinary sequential paths.

A proof that is sound for one thread in isolation may be invalid under concurrent
interleavings.

The design therefore rejects the shortcut:

```text
verified sequentially
therefore thread-safe
```

Concurrency properties require semantics adequate to the concurrency claim.

Ordinary concurrent C++ can still exist outside such verified claims.

---

# 58. Why C++ templates remain C++ templates

C++L does not need a second runtime generic-programming system.

C++ templates already define:

- generic declarations;
- substitution;
- instantiation;
- specialization;
- overload interactions;
- dependent names.

C++L's formal layer should attach obligations to the actual instantiated C++
semantics rather than invent a parallel template mechanism.

This is also why indexed refinements use C++-style angle-bracket application.

---

# 59. Why concepts are not automatically proofs

A concept constrains C++ template participation according to C++ semantics.

That does not automatically make every proposition suggested by its spelling a
formal theorem.

For example, a concept named `Ordered` is still whatever C++ expression defines
it.

C++L therefore distinguishes:

```text
C++ constraint satisfaction
```

from:

```text
formal evidence for a proposition
```

unless a specified correspondence explicitly connects them.

---

# 60. Why standard-library models are semantic adapters, not replacements

Real C++ programs depend heavily on the standard library.

C++L should reason about useful abstractions such as:

- `std::optional`;
- `std::variant`;
- `std::expected`;
- arrays;
- spans;
- vectors;
- strings;
- smart pointers;
- algorithms.

But proof models must describe the public semantics relevant to the claim rather
than assume private implementation layout.

This preserves portability and keeps formal reasoning connected to the abstraction
the C++ program actually uses.

---

# 61. Why headers and translation units remain first-class

C++ interfaces do not live only in implementation files.

Headers and module interfaces contain:

- declarations;
- templates;
- inline functions;
- types;
- constants;
- contracts;
- reusable proof declarations.

Verification therefore has to compose across translation units without changing
native calling conventions merely to transport proof metadata.

This is part of fitting the C++ ecosystem rather than imposing a new project
organization.

---

# 62. Why proof metadata is separate from native ABI

A caller may need formal information that the native ABI does not encode.

Examples include:

- contracts;
- refinement predicates;
- effect summaries;
- theorem evidence;
- trust provenance.

Changing native calling conventions to carry that information would undermine
interoperability.

The design therefore treats verification metadata and runtime ABI as separate
layers.

The metadata may be required for proof, but it should not become hidden runtime
state.

---

# 63. Why proof information erases

Proof evidence exists to justify compilation, not to become production data by
default.

Keeping it at runtime would create unnecessary:

- memory cost;
- code size;
- calling-convention effects;
- object-layout effects;
- deployment dependencies.

The preferred lifecycle is:

```text
specify
    ↓
prove
    ↓
accept/reject
    ↓
erase proof-only information
    ↓
compile ordinary C++
```

This is one of the project's defining systems-programming choices.

---

# 64. Why there is no mandatory theorem runtime

C++L is not intended to make every native executable host a proof interpreter.

A mandatory theorem VM, proof garbage collector, or runtime proof-object system
would undermine the goal of adding compile-time assurance to ordinary native C++.

Runtime libraries remain available for ordinary application needs.

What is rejected is a runtime dependency that exists only because static proof was
used.

---

# 65. Why native ABI stability matters

C++ is often selected specifically because it can interoperate with:

- existing binaries;
- operating-system APIs;
- platform frameworks;
- C interfaces;
- plugin systems;
- device SDKs.

Verification-only features that unnecessarily alter representation or calling
conventions would make adoption much harder.

This pressure strongly favors:

```text
proof identity != runtime representation
```

for refinements and other proof-only abstractions.

---

# 66. Why erasure must preserve runtime meaning

Erasure is not a cosmetic compiler pass.

It is the boundary connecting the verified source to the program that executes.

If erasure:

- removes a real runtime check;
- alters an expression;
- changes control flow;
- changes object lifetime;
- changes layout;
- changes calling convention;

then verification may no longer describe the shipped program.

This is why erasure correctness belongs to the runtime trust chain even though the
proof constructs themselves are compile-time-only.

---

# 67. Why a small proof kernel is desirable but not the whole story

A small proof checker is easier to audit than an entire compiler.

That is a valuable architecture.

But a kernel only proves the proposition it receives.

If a frontend maps:

```text
source property P
```

to:

```text
different core property Q
```

a perfect kernel can soundly prove `Q` while the tool incorrectly reports `P`.

The project therefore distinguishes:

```text
logical proof validity
```

from:

```text
source-to-proof correspondence
```

and from:

```text
verified-source-to-runtime correspondence
```

`TRUST.md` defines those boundaries precisely.

The design principle is not "trust only the kernel."

It is "make each unavoidable trust boundary explicit and shrink it where
independent checking is practical."

---

# 68. Why C++ semantic infrastructure should be reused

Reimplementing C++ would require reproducing enormous complexity:

- preprocessing;
- name lookup;
- overload resolution;
- templates;
- conversions;
- `constexpr`;
- access control;
- object lifetime;
- value categories;
- language modes;
- extensions;
- layout;
- ABI rules.

It would also create another opportunity for verification semantics to drift from
execution semantics.

C++L therefore prefers to consume resolved C++ meaning from mature C++ semantic
infrastructure rather than creating a competing C++ implementation without a
compelling reason.

The concrete integration strategy belongs in `ARCHITECTURE.md`.

---

# 69. Why Clang is a natural semantic authority without being the language definition

Clang is an attractive implementation foundation because it already understands
real C++ and integrates with LLVM.

That does not mean C++L semantics are defined as "whatever Clang happens to do."

The language definition remains in C++L's normative documents.

The implementation uses a selected C++ semantic authority to answer ordinary C++
questions within the supported compatibility envelope.

This distinction allows implementation strategy to evolve without turning the
current frontend into the specification.

---

# 70. Why a permanent compiler fork is not a design requirement

A compiler fork can provide deeper hooks.

It also creates continuing costs in:

- upstream merging;
- security updates;
- new C++ standards;
- platform support;
- release maintenance.

C++L should not require a permanent fork merely because proof is being added.

If a future hard requirement justifies one, that is an architectural decision
that should be evaluated on its merits rather than baked into the language model.

---

# 71. Why C++L needs a verification representation distinct from the C++ AST

A C++ AST represents C++ syntax and semantic entities.

Verification additionally needs:

- propositions;
- proof terms;
- logical binders;
- obligations;
- logical value versions;
- places/storage facts;
- refinement validity;
- effect summaries;
- proof-only mathematical values.

Trying to make the raw C++ AST be the entire proof representation would entangle
logical reasoning with frontend implementation details.

C++L therefore benefits from a verification-oriented representation derived from
resolved C++ meaning.

The crucial requirement is correspondence: the verification representation must
describe the same program, not invent a more convenient one.

---

# 72. Why semantic drift is treated as a first-class risk

One of the worst possible verification failures is:

```text
prove model A
execute program B
```

where the difference is invisible to the user.

This can happen through:

- incorrect source projection;
- wrong overload correspondence;
- missed conversions;
- stale metadata;
- incorrect alias modeling;
- unsound effect summaries;
- incorrect erasure;
- backend mismatch.

C++L therefore treats correspondence as an explicit design and trust problem
rather than assuming that a correct proof kernel automatically solves it.

---

# 73. Why tests remain necessary

Formal proof only covers properties represented in the formal model.

Real software also has concerns such as:

- performance;
- UI behavior;
- operational integration;
- deployment;
- external services;
- hardware;
- platform bugs;
- properties not yet formalized.

Tests remain valuable for those concerns and for compiler regression detection.

What C++L rejects is the equivalence:

```text
many passing examples
=
universal theorem
```

Testing and proof are complementary.

---

# 74. Why counterexamples are useful but asymmetric

One valid counterexample can refute a universal statement.

Failure to discover a counterexample usually cannot establish the statement.

This makes counterexample generation highly valuable for:

- diagnostics;
- debugging;
- proof development;
- fuzzing;
- AI repair loops.

It does not make counterexample search a proof checker.

---

# 75. Why deterministic semantic results matter

Verification participates in:

- CI;
- code review;
- reproducible builds;
- security auditing;
- caching;
- distributed development.

Accepted or rejected proof meaning should not accidentally depend on:

- memory addresses;
- hash iteration order;
- thread scheduling;
- temporary paths;
- random search order.

Search may be heuristic.

The semantic result, given the same formal inputs and accepted evidence, should be
stable.

---

# 76. Why semantic identity matters more than file identity

Proof validity depends on semantic inputs, not editor accidents.

A timestamp, temporary path, or source buffer address is not a mathematical
dependency.

This motivates using semantic identities for:

- declarations;
- obligations;
- imported contracts;
- proof artifacts;
- caches.

That improves reproducibility and incremental verification without turning cache
mechanics into language semantics.

---

# 77. Why incremental verification matters

Large C++ repositories cannot afford to reprove everything after every edit.

The desired model is:

```text
source change
    ↓
semantic dependency change
    ↓
affected obligations
```

rather than:

```text
source change
    ↓
verify entire world
```

The optimization is architectural, but the design pressure affects how formal
interfaces and semantic identities should be structured.

Proof reuse must never weaken dependency correctness.

---

# 78. Why proof-critical code should be conservative

Proof-critical code has a different optimization target from product features.

Useful qualities include:

- small surface area;
- explicit control flow;
- determinism;
- auditable data structures;
- obvious ownership;
- minimal hidden state;
- negative-testability.

A clever abstraction is not automatically bad.

But sophistication that obscures the proof acceptance path raises the cost of
auditing the system whose job is to establish confidence.

---

# 79. Why contextual C++L vocabulary was chosen

C++ already contains decades of user identifiers.

Words useful to C++L, such as:

```text
law
proof
pure
ghost
verified
```

may already occur in valid C++ programs.

Globally reserving them would create unnecessary source incompatibility.

Contextual interpretation allows C++L to add a formal surface while preserving
ordinary use outside the relevant grammatical contexts.

---

# 80. Why existing C++ keywords are not repurposed casually

Words such as:

```text
requires
concept
constexpr
consteval
```

already have precise C++ meanings.

Giving them unrelated proof semantics would create both cognitive and parsing
ambiguity.

The design preference is:

```text
reuse C++ syntax when the concept really is the C++ concept
introduce distinct formal vocabulary when the concept is new
```

This is why `requires` remains C++ rather than becoming a C++L contract keyword.

---

# 81. Why C++ declarators remain recognizable

C++ declarators already carry substantial complexity:

- templates;
- attributes;
- cv/ref qualifiers;
- `noexcept`;
- constraints;
- trailing return types;
- `override`;
- `final`;
- calling conventions.

Inserting an unrelated grammar deeply inside that machinery would increase
ambiguity and implementation pressure.

C++L therefore prefers extension points that leave the underlying C++ declarator
structurally recognizable.

The exact legal placement is owned by `GRAMMAR.md`.

---

# 82. Why declaration and application syntax are not always identical

Binding parameters and applying a parameterized type are different operations.

For indexed refinements:

```cpp
type Index(unsigned n) = unsigned where (self < n);
```

the declaration introduces `n`.

Use:

```cpp
Index<4u>
```

applies the type constructor.

This design deliberately follows C++-style type application rather than
function-call-looking syntax.

It makes indexed refinements fit naturally inside existing C++ type grammar.

---

# 83. Why C++L does not define `data` or runtime `match`

Adding algebraic data types and runtime pattern matching would create a second
runtime type/control-flow layer.

That would raise questions about:

- representation;
- constructors;
- ABI;
- lifetime;
- interoperability;
- lowering;
- duplicate modeling of existing C++ types.

C++L's need is proof decomposition, not a replacement runtime data model.

Therefore structural proof is expressed through proof-only constructs over
existing C++ types.

---

# 84. Why proof arm syntax is uniform

Case analysis, product decomposition, and induction all benefit from a consistent
visual grammar:

```text
Label(bindings) => {
    ...
}
```

Uniformity reduces the number of proof-specific mini-languages users and tools
must understand.

The labels still have domain-specific meaning; the shared structure is about
readability and tooling.

---

# 85. Why proof binders and assumptions are distinct

A runtime/program value and a proof hypothesis are different things.

Induction, for example, may introduce:

- a predecessor value;
- an induction hypothesis about that predecessor.

Binding both positionally in one arm would blur program data with proof evidence.

C++L therefore prefers structural binders for values and explicit `assume` for
context-supplied proof hypotheses.

That keeps the proof context visible and prevents a pattern arm from silently
granting arbitrary propositions.

---

# 86. Why `assume` names evidence instead of creating it

The word "assume" can be dangerous in a proof language.

If it meant "make this proposition true," it would be an axiom escape hatch.

C++L instead uses it to name a premise already supplied by the current proof
context.

That lets proof scripts give readable names to evidence without enlarging the
assumption set.

Explicit trust remains a separate language mechanism.

---

# 87. Why grammar is separate from semantics

Syntax and meaning evolve at different rates.

A language may improve the spelling of a construct without changing its formal
semantics.

Conversely, a semantic clarification should not require rewriting every grammar
production explanation.

Separating:

```text
GRAMMAR.md
```

from:

```text
SPEC.md
```

lets each document be precise about one job.

`DESIGN.md` explains why a spelling direction was chosen only when that rationale
is important to preserve.

---

# 88. Why design rationale is separate from the specification

A specification answers:

```text
What does this program mean?
```

Design rationale answers:

```text
Why did we choose that meaning?
```

Mixing the two makes it difficult to distinguish:

- mandatory behavior;
- historical explanation;
- engineering preference;
- rejected alternative;
- future evaluation criterion.

C++L therefore keeps rationale non-normative.

This also means obsolete rationale can be rewritten without silently changing the
language.

---

# 89. Why trust policy is separate from design rationale

"Why minimize trust?" is a design question.

"What exactly is trusted?" is a normative trust question.

The second requires much more precision.

`DESIGN.md` therefore explains the motivation for:

- explicit assumptions;
- independent checking;
- visible trust closure;
- correspondence auditing.

`TRUST.md` defines the actual TCB requirements.

---

# 90. Why implementation architecture is separate from design rationale

Compiler architecture will evolve.

Passes may be split or merged.

Internal representations may change.

Caching may move processes.

Solvers may be replaced.

None of those changes should automatically redefine C++L.

`ARCHITECTURE.md` owns the current intended implementation structure.

`DESIGN.md` records only durable reasons that should constrain architecture
choices.

---

# 91. Compatibility versus proof power

Supporting real C++ makes proof harder.

Weakening C++ compatibility would simplify:

- aliasing;
- lifetime;
- templates;
- exceptions;
- concurrency;
- UB;
- library modeling.

Weakening proof fidelity would make verification less meaningful.

C++L accepts this tension deliberately.

The preferred response to hard semantics is:

```text
model them soundly
or fail closed
```

not:

```text
pretend they are simpler
```

---

# 92. Formal expressiveness versus usability

A language with dependent types, induction, quantified propositions, refinements,
and explicit proofs can become intimidating.

C++L therefore favors progressive disclosure.

A developer should be able to begin with:

- simple contracts;
- straightforward Laws;
- refinements;
- automatic proofs;

before needing:

- explicit quantifier manipulation;
- structural induction;
- advanced proof terms;
- abstract mathematical domains.

Power should exist without becoming the entry price for basic verification.

---

# 93. Automation versus transparency

Too little automation makes formal methods impractical.

Too much opaque authority makes results difficult to audit.

The desired balance is:

```text
strong automation
+
explicit specifications
+
independently checkable evidence
+
visible trust boundaries
```

Users should benefit from automation without needing to treat it as magic.

---

# 94. Proof strength versus verification cost

More precise reasoning often costs more.

The project should not solve compile-time performance problems by quietly
weakening theorem meaning.

Preferred engineering responses include:

- incrementality;
- dependency analysis;
- proof reuse;
- parallel search;
- specialized decision procedures;
- semantic caching.

Those optimizations should operate around stable semantics rather than redefining
what counts as proof.

---

# 95. C++ familiarity versus formal clarity

Making every new feature look exactly like ordinary C++ may reduce visual novelty.

It may also hide important semantic boundaries.

C++L therefore tries to retain C++ syntax where the concept is genuinely C++ and
use visibly formal syntax where the concept is genuinely new.

Examples include:

```text
C++ template application      -> existing C++ syntax
formal mathematical domain    -> visible @ prefix
C++ requires                  -> remains C++ requires
Law conclusion                -> distinct proves vocabulary
```

The goal is recognizability without pretending proof is ordinary computation.

---

# 96. Zero runtime cost versus dynamic reality

Proof-only information can often disappear completely.

Dynamic validation cannot.

The design therefore distinguishes:

```text
information needed only to justify compilation
```

from:

```text
checks inherently required because a runtime value was unknown beforehand
```

This permits zero-cost proof abstractions without making impossible promises about
external data.

---

# 97. Local reasoning versus whole-system truth

Developers need useful local guarantees.

Those guarantees inevitably depend on:

- callers;
- external components;
- runtime environment;
- libraries;
- compiler/backend correctness;
- explicit assumptions.

C++L therefore focuses on **explicit dependencies** rather than claiming that a
local theorem proves the whole machine correct.

This is a more useful and more honest model for real systems.

---

# 98. Stability versus language evolution

C++L has to evolve as difficult areas are understood more deeply.

At the same time, proof-bearing source depends more heavily on semantic stability
than ordinary syntax sugar does.

The preferred evolution strategy is therefore:

```text
understand the property
    ↓
define semantics
    ↓
define trust implications
    ↓
choose syntax
    ↓
design implementation
    ↓
add conformance tests
```

rather than freezing syntax around an incompletely understood model.

---

# 99. Why unsupported verification fails closed

Verification has asymmetric failure costs.

A false negative means:

```text
a true property was not established
```

A false positive means:

```text
an unestablished property was presented as proven
```

For a proof system, the second failure is substantially worse.

C++L therefore prefers conservative refusal when required reasoning is missing.

That preference is not permission to leave the language permanently incomplete;
it is the correct behavior of an incomplete implementation on unsupported cases.

---

# 100. Why unsupported verification does not make ordinary C++ invalid

"Cannot prove this claim" and "this C++ program is ill-formed" are different
statements.

C++L's additive adoption model depends on preserving that distinction where the
normative language permits ordinary unverified C++.

A verification limitation should not automatically become a new C++ language
restriction.

Likewise, ordinary C++ acceptance must not be misreported as formal proof.

---

# 101. AI-oriented design

AI changes the economics of implementation generation.

Producing candidate code is becoming cheap.

Establishing that candidate code satisfies durable human intent remains hard.

C++L's Laws and contracts can act as an interface between:

```text
human/domain intent
```

and:

```text
machine-generated implementation
```

An agent can attempt many implementations.

The specification remains stable.

The proof checker determines whether the attempt satisfies the required property.

This makes formal specification especially valuable in an agentic engineering
environment.

---

# 102. Why the documentation should be agent-addressable

A mature specification may be too large to serve as the raw prompt for every
coding task.

That is a tooling/documentation problem, not a reason to weaken the specification.

The project therefore benefits from:

- stable rule identifiers;
- feature indexes;
- dependency maps;
- implementation maps;
- test matrices;
- bounded task packets.

The canonical specification remains complete.

Agents receive the relevant slice plus its dependencies.

This separates:

```text
source of truth
```

from:

```text
working context
```

and reduces the intelligence needed merely to discover task scope.

The exact agent tooling belongs outside this design rationale.

---

# 103. Criteria for future language proposals

A language proposal should answer questions such as:

1. What problem does the feature solve?
2. Why does that problem require language semantics rather than tooling or a
   library?
3. Does it describe actual C++ execution or create a competing model?
4. Does it preserve C++ source and ABI compatibility where promised?
5. Can proof-only information remain proof-only?
6. Does it make assumptions more explicit or less explicit?
7. Does it enlarge the TCB?
8. Can unsupported cases fail closed?
9. Does it compose with mutation, aliasing, templates, exceptions, and
   translation units?
10. Does the syntax collide with ordinary C++?
11. Can automation exploit the feature without becoming a hidden authority?
12. Is the feature understandable without knowing compiler internals?
13. Does it introduce a second runtime abstraction C++ already has?
14. Can its semantics be stated independently of the current implementation?

A proposal that cannot answer those questions is probably not ready for the
language surface.

---

# 104. Criteria for future implementation decisions

Implementation decisions should be judged by a different set of questions:

1. Does the implementation preserve already-defined semantics?
2. Does it reuse authoritative C++ semantic information where appropriate?
3. Does it introduce duplicate semantic authorities?
4. Does it increase any TCB layer unnecessarily?
5. Can the result fail closed?
6. Is semantic identity stable and reproducible?
7. Can the component be independently checked or fuzzed?
8. Does it preserve erasure/runtime correspondence?
9. Does it scale to large C++ repositories?
10. Does it create architecture debt that will make later semantic coverage
    harder?
11. Does it create a feature-specific path where a common semantic mechanism
    should exist?
12. Are negative and adversarial tests as strong as happy-path tests?

The current implementation topology belongs in `ARCHITECTURE.md`, not here.

---

# 105. Rejected direction: mandatory whole-program verification

Whole-program proof before any local guarantee was rejected as an adoption model.

It would make useful verification depend on proving:

- third-party libraries;
- the operating system;
- device code;
- proprietary binaries;
- unrelated application modules.

C++L instead supports useful local claims whose external assumptions and
boundaries remain explicit.

---

# 106. Rejected direction: tests as proofs

Tests observe selected executions.

A universal theorem ranges over the domain stated by its proposition.

No quantity of ordinary test cases changes that logical distinction.

Tests remain complementary evidence about software quality, not theorem evidence.

---

# 107. Rejected direction: runtime assertions as compile-time proofs

A runtime assertion can stop one execution when a condition fails.

That does not establish a universal compile-time proposition.

Treating assertions as proof would blur runtime validation and theorem proving and
would make assurance depend on execution reaching the assertion.

---

# 108. Rejected direction: silent runtime fallback after proof failure

Automatically inserting a runtime check when proof fails was rejected because it
silently changes both runtime semantics and assurance.

The programmer may choose runtime validation explicitly.

The compiler should not choose it on the programmer's behalf as a hidden
degradation mode.

---

# 109. Rejected direction: solver as unconditional truth authority

Solvers are powerful.

They are also large pieces of software with their own translations, heuristics,
and potential defects.

Where practical, C++L prefers solver-generated evidence that can be independently
checked.

If a solver is ever trusted directly, that should be an explicit trust decision,
not an invisible consequence of using automation.

---

# 110. Rejected direction: global reservation of C++L vocabulary

Globally reserving every C++L word would invalidate existing C++ unnecessarily.

Contextual interpretation was chosen to reduce that compatibility cost.

---

# 111. Rejected direction: repurposing unrelated C++ keywords

Existing C++ keywords already carry established meaning.

Using them for unrelated proof constructs would create avoidable ambiguity.

In particular, C++ `requires` remains C++ rather than becoming C++L's contract
syntax.

---

# 112. Rejected direction: unsafe as a proof escape hatch

If entering `unsafe` could produce arbitrary trusted facts, the verification
system could be bypassed trivially.

`unsafe` therefore means a reduction in what is proved, not an increase in what
may be assumed.

---

# 113. Rejected direction: silently replacing machine values with mathematics

Machine integers, floating-point values, pointers, and runtime containers do not
become ideal mathematical objects merely because they appear in a specification.

C++L uses explicit mathematical domains when mathematical abstraction is desired.

---

# 114. Rejected direction: proving a shadow implementation

Maintaining:

```text
implementation A for proof
implementation B for execution
```

creates a semantic synchronization problem.

C++L instead aims to prove properties connected to the program that actually
executes, with erasure removing only proof-specific material.

---

# 115. Rejected direction: mandatory proof runtime

A theorem interpreter in every executable would add runtime cost and deployment
complexity unrelated to ordinary C++ execution.

The project therefore treats proof as a compile-time concern unless the
application itself explicitly chooses runtime mechanisms.

---

# 116. Rejected direction: reimplement all C++ semantics

Rebuilding a full C++ frontend would consume enormous engineering effort and
create a new source of disagreement with production compilers.

C++L's unique value lies in the formal layer, not in duplicating mature C++
semantic infrastructure without need.

---

# 117. Rejected direction: permanent compiler fork as a language premise

A fork may become architecturally justified.

It is not part of the language's identity.

The design should remain compatible with different implementation strategies as
long as they satisfy the normative semantics and trust requirements.

---

# 118. Rejected direction: mandatory second implementation language for the proof core

A language with stronger memory-safety properties can be attractive for
proof-critical implementation.

Using another implementation language also introduces new build, FFI,
serialization, packaging, and debugging boundaries.

No implementation language automatically makes a proof checker logically sound.

The important requirement is an auditable and correctly trusted proof boundary,
not a mandated implementation language.

---

# 119. Rejected direction: cleverness as proof-critical architecture

Dense metaprogramming, hidden control flow, and implicit global behavior can make
proof-critical code harder to audit.

The project therefore prefers clarity over novelty in components that determine
assurance.

This is an engineering preference, not a ban on abstraction.

---

# 120. Rejected direction: historical implementation limitations as language design

A temporary limitation such as:

```text
only one return handled
templates unsupported
signed arithmetic refused
members not modeled
```

may be an honest implementation status.

It is not automatically a reason for the language to forbid the corresponding
construct.

The correct direction is:

```text
normative semantics
    ↓
implementation catches up
```

rather than:

```text
temporary implementation
    ↓
language is narrowed to match it
```

---

# 121. Rejected direction: feature-specific verification islands

A new feature can often be implemented quickly by creating a special path:

```text
special member logic
special array logic
special pointer logic
special refinement logic
```

That tends to duplicate invariants and eventually produce contradictory behavior.

C++L prefers common semantic abstractions where different language features share
the same underlying concept.

Examples include:

```text
common place/value-version model for storage
common crossing logic for refinement introduction
common call/effect model
common proof evidence model
common erasure principles
```

This preference is especially important for agent-generated implementation work,
where local special cases are easy to create and hard to see globally.

---

# 122. Design principles

The project is guided by the following principles:

```text
Extend C++ rather than replace it.

Keep ordinary runtime behavior grounded in real C++ semantics.

Make formal intent first-class.

Keep claims, evidence, trust, runtime checking, and unsafe execution distinct.

Prefer reusable Laws and contracts over prose-only intent.

Use automation aggressively without making automation the definition of truth.

Prefer small, independently checkable proof authority where practical.

Treat source-to-proof and proof-to-runtime correspondence as first-class trust problems.

Keep proof-only information out of runtime representation.

Preserve native ABI where verification semantics do not require runtime change.

Model machine behavior rather than silently replacing it with ideal mathematics.

Use explicit mathematical domains when abstraction from runtime representation is intended.

Attach refinement facts to logical values/versions, not permanently to variable names.

Model mutation, aliasing, lifetime, and memory capabilities explicitly enough for the claims being made.

Reason structurally over existing C++ types rather than introducing a duplicate runtime data language.

Keep termination distinct from purity and induction.

Keep runtime validation ordinary runtime behavior.

Keep unsafe code distinct from trusted assumptions.

Reuse mature C++ semantic infrastructure rather than guessing C++ meaning independently.

Prefer honest incompleteness over false proof.

Do not let temporary implementation limitations redefine the language.

Design for incremental adoption in real C++ repositories.

Design proof interfaces to work across translation units without changing native ABI unnecessarily.

Design for both human-written and machine-generated implementations.

Make documentation addressable enough that agents can receive bounded semantic task contexts without weakening the canonical specification.
```

---

# 123. Summary

C++L begins from a simple observation:

```text
C++ is exceptionally good at specifying how native software executes.

It is much weaker at expressing reusable, machine-checked statements about
what must always be true of that execution.
```

The project adds that missing layer while trying to preserve the ecosystem and
runtime properties that make C++ valuable.

The resulting design is centered on:

```text
real C++ execution
+
explicit formal intent
+
composable proof evidence
+
explicit trust
+
semantics-preserving erasure
```

The most important design constraint is fidelity.

C++L should not make proof easier by quietly changing what the underlying C++
program means.

When a semantic area is difficult, the preferred choices are:

```text
model it soundly
make the boundary explicit
or fail closed
```

rather than inventing facts.

That principle connects the major choices in the language:

- refinements preserve runtime representation but require proof at semantic
  crossings;
- storage facts follow logical value versions rather than source names;
- aliasing and capability reasoning are conservative;
- structural proof reasons over actual C++ state spaces;
- mathematical abstractions are visibly distinct from machine values;
- proof-only information erases;
- runtime validation remains runtime behavior;
- trusted assumptions remain explicit;
- unsafe code does not manufacture proof;
- automation proposes evidence rather than defining truth;
- the proof kernel is important but source/runtime correspondence remains part of
  end-to-end assurance.

`SPEC.md` defines what C++L means.

`TRUST.md` defines what must be trusted for those meanings to remain credible.

`FOUNDATIONS.md` defines the formal basis.

`ARCHITECTURE.md` defines how the implementation realizes them.

`DESIGN.md` records **why this particular shape was chosen**.
