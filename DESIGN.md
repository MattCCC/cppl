# C++L Design

**C++L — C++ with Laws**

Status: Draft design rationale

This document explains the reasoning behind the major design choices of C++L.

It is **non-normative**.

It answers questions such as:

- Why is C++L a C++ superset rather than a separate language?
- Why are Laws part of the language?
- Why are proofs distinct from tests, assertions, and trust?
- Why is verification incremental?
- Why is proof machinery intended to disappear before runtime?
- Why is C++ semantic infrastructure reused rather than recreated?
- Why are particular alternatives rejected?

It does **not** define legal syntax, language semantics, verification rules, compatibility guarantees, trust propagation, or compiler topology.

Those are defined elsewhere.

## Document boundaries

| Document                             | Responsibility                                              |
| ------------------------------------ | ----------------------------------------------------------- |
| [SPEC.md](SPEC.md)                   | Normative language semantics                                |
| [GRAMMAR.md](GRAMMAR.md)             | Normative concrete syntax and grammar                       |
| [COMPATIBILITY.md](COMPATIBILITY.md) | C++ source, ABI, standard, and toolchain compatibility      |
| [TRUST.md](TRUST.md)                 | Trusted Computing Base, assumptions, and trust boundaries   |
| [ARCHITECTURE.md](ARCHITECTURE.md)   | Compiler structure, components, dependencies, and data flow |
| [FOUNDATIONS.md](FOUNDATIONS.md)     | Mathematical foundations and intellectual lineage           |
| [STATUS.md](STATUS.md)               | Actual implementation maturity                              |
| [ROADMAP.md](ROADMAP.md)             | Planned implementation sequence                             |

When this document discusses a language feature, it explains the **reason for the feature** rather than defining its exact semantics.

If this document conflicts with a normative document, the normative document takes precedence.

---

# 1. Problem

C++ provides extremely strong control over:

- native execution;
- memory representation;
- resource ownership;
- interoperability;
- performance;
- hardware;
- operating-system APIs;
- existing libraries;
- large mature codebases.

It is much weaker at expressing and mechanically establishing higher-level intent.

Important correctness requirements are commonly represented today through combinations of:

- prose;
- comments;
- tests;
- assertions;
- code review;
- static analysis;
- conventions;
- developer knowledge.

Those mechanisms remain useful, but they do not provide a general way to express a statement such as:

```text
For every valid input satisfying these assumptions,
this implementation preserves this invariant.
```

and mechanically establish that statement.

C++L exists to explore that missing layer.

---

# 2. Central design idea

The central idea is:

```text
keep C++ as the execution language
+
add a language for precise intent and proof
```

Rather than replacing the systems-programming model developers already depend on, C++L adds a formal layer around it.

Conceptually:

```text
C++ program
    +
formal intent
    +
machine-checkable evidence
```

The formal layer exists to make important claims explicit and mechanically checkable.

The executable remains grounded in the C++ program developers actually intend to ship.

---

# 3. Why extend C++ instead of creating another language

A new independent language could provide a cleaner starting point.

It would also immediately lose much of what makes C++ valuable:

```text
existing source
existing headers
existing templates
existing libraries
existing build systems
existing ABI
existing tooling
existing platform integration
```

C++L is aimed particularly at systems where rewriting the world is unrealistic.

The design therefore starts with:

```text
preserve the C++ ecosystem
```

and asks:

```text
how much formal reasoning can be added without abandoning it?
```

The exact compatibility commitment is defined in `COMPATIBILITY.md`.

---

# 4. Why C++L is a language rather than only a verifier

An external verification tool could attach specifications to C++ without changing the language.

That approach has advantages, but it tends to create two separate artifacts:

```text
program
```

and:

```text
specification about the program
```

C++L instead treats formal intent as part of the source language.

The motivation is proximity.

A function, its contract, the Laws concerning it, and proofs about it can evolve together.

This also gives tools a common representation of:

```text
what executes
what is claimed
what has been established
```

rather than requiring conventions connecting separate languages.

---

# 5. Compatibility as a design pressure

C++L is intended to be useful in existing C++ repositories.

That makes source compatibility one of the strongest pressures on the language design.

Features that would require broad mechanical rewrites are viewed skeptically.

Examples include designs requiring developers to:

```text
rename every source file
replace ordinary functions
replace the standard library
replace native pointers everywhere
rewrite code into a functional language
change native calling conventions
verify every dependency before adoption
```

The preferred adoption model is incremental.

The normative compatibility rules belong in `COMPATIBILITY.md`.

---

# 6. Why verification is incremental

Real C++ systems are rarely uniform.

A single application may contain:

```text
new code
twenty-year-old code
third-party libraries
generated code
assembly
platform APIs
hardware interaction
external binaries
```

Demanding equivalent formal assurance everywhere before any verification becomes useful would make adoption impractical.

The design therefore favors a progression such as:

```text
ordinary C++
    ↓
specified code
    ↓
partially verified code
    ↓
stronger verified regions
```

The objective is to let the formally understood portion of a system grow over time.

This is an adoption strategy, not a claim that all assurance levels are equivalent.

---

# 7. Why Laws exist

Function contracts are useful, but not every important property belongs naturally to one function.

Examples include:

```text
relationships between operations
algebraic properties
conservation properties
type invariants
cross-function consistency
domain rules
protocol rules
```

A named Law gives such intent a first-class identity.

This is particularly useful when an implementation changes while the intended property remains stable.

The exact semantics of Laws belong in `SPEC.md`.

Their concrete syntax belongs in `GRAMMAR.md`.

---

# 8. Why specifications and implementations are separated conceptually

A dangerous development pattern is:

```text
implementation behaves this way
therefore specification should say this
```

C++L is intended to support the opposite direction when appropriate:

```text
this is the required property
therefore implementation must satisfy it
```

This distinction becomes especially important when AI generates implementation code.

The specification should represent intent rather than merely document whatever implementation currently happens to do.

---

# 9. Why declaring intent is different from establishing it

Formal systems lose much of their value if stating a proposition automatically grants it authority.

C++L therefore treats these as different concepts:

```text
claim
evidence
assumption
```

The motivation is auditability.

A reviewer should be able to distinguish:

```text
what the program claims
```

from:

```text
why the system accepts that claim
```

The normative proof and status semantics are defined in `SPEC.md` and `TRUST.md`.

---

# 10. Why proofs are explicit concepts

Proof evidence creates a useful separation between:

```text
finding a proof
```

and:

```text
checking a proof
```

That separation allows many different producers:

```text
human-written reasoning
compiler automation
rewriting systems
decision procedures
SMT solvers
specialized tactics
AI agents
```

without requiring every producer to become part of the ultimate correctness authority.

This is one of the most important design choices in C++L.

---

# 11. Why proof search and proof checking are different

Proof search can be large, heuristic, probabilistic, expensive, and rapidly evolving.

Proof checking can be much smaller and more deterministic.

That suggests a desirable shape:

```text
powerful search
      ↓
evidence
      ↓
small checker
```

rather than:

```text
powerful search
      =
truth authority
```

The exact trusted boundary is documented in `TRUST.md`.

The implementation structure belongs in `ARCHITECTURE.md`.

---

# 12. Why AI is deliberately outside the authority boundary

AI is expected to be useful for:

```text
generating implementations
generating specifications
suggesting invariants
finding counterexamples
constructing proofs
repairing failed proofs
explaining diagnostics
```

But AI output should be treated as candidate work.

A desirable workflow is:

```text
human or system defines intent
        ↓
AI generates implementation
        ↓
AI generates candidate reasoning
        ↓
independent verification checks it
```

This lets C++L benefit from increasingly capable models without making model reliability a foundation of program correctness.

---

# 13. Why contextual syntax was chosen

C++ has decades of existing identifiers.

Short words useful to C++L may already exist in ordinary programs:

```cpp
law
proof
ghost
pure
verified
```

Globally reserving such words would create avoidable migration problems.

Context-sensitive extension points were therefore chosen as the preferred direction.

The exact lexical and disambiguation rules belong in `GRAMMAR.md`.

---

# 14. Why existing C++ keywords are not reused casually

Modern C++ already gives precise meanings to terms such as:

```text
requires
concept
constexpr
consteval
```

Overloading them with unrelated proof-language meanings would create confusion and grammar pressure.

The design preference is therefore:

```text
reuse C++ syntax when the concept really is the C++ concept

introduce distinct syntax when C++L introduces a different concept
```

For example, contract terminology can remain distinct from C++ constraints.

The exact syntax belongs in `GRAMMAR.md`.

---

# 15. Why C++ declarators are treated as an existing boundary

C++ declarators already encode a large amount of language complexity:

```text
types
templates
cv qualification
reference qualification
noexcept
attributes
constraints
trailing return types
override
final
```

Inserting a second language into the middle of that machinery would create unnecessary ambiguity.

C++L syntax is therefore designed with a strong preference for leaving the ordinary declarator structurally recognizable.

The concrete placement rules are defined in `GRAMMAR.md`.

---

# 16. Why contracts and Laws are both useful

Contracts and Laws address different scales of intent.

A contract is naturally associated with a declaration or operation.

A Law can express a property with a broader conceptual identity.

Keeping both concepts avoids forcing every specification into:

```text
function precondition/postcondition
```

and avoids forcing every local API rule into a standalone theorem.

Their exact relationship is defined normatively in `SPEC.md`.

---

# 17. Why purity is explicit

Formal reasoning becomes dramatically simpler when a computation behaves like a mathematical function.

But C++ is intentionally full of effects.

Examples include:

```text
mutation
I/O
volatile access
atomics
external state
resource ownership
```

Rather than pretending all functions are mathematical functions, C++L makes purity an explicit concept.

This allows stronger reasoning where appropriate without imposing functional programming on ordinary C++.

The exact purity rules belong in `SPEC.md`.

---

# 18. Why purity and verification are separate ideas

Purity and correctness answer different questions.

A pure function can still compute the wrong result.

An imperative function can still satisfy a meaningful verified property.

Keeping the concepts separate avoids forcing unrelated concerns into one modifier or one assurance category.

---

# 19. Why termination is separate from purity

A computation can have no side effects and still fail to terminate.

That matters because unrestricted non-termination inside proof-relevant computation can undermine logical reasoning.

At the same time, requiring every systems program to terminate is unrealistic:

```text
servers
event loops
kernels
embedded controllers
```

The design therefore distinguishes ordinary runtime divergence from contexts where termination becomes logically important.

The exact semantic rules belong in `SPEC.md`.

---

# 20. Why refinements are useful

Many C++ programs use primitive types for values with much narrower valid domains.

For example:

```cpp
int percentage;
int port;
std::size_t index;
int balance;
```

The compiler knows the machine type but not necessarily the domain meaning.

Refinement types provide a way to attach such meaning directly to values.

This can reduce repeated defensive reasoning and make invariants available to later verification.

Their exact construction and conversion semantics belong in `SPEC.md`.

---

# 21. Why dependent types are part of the long-term design

Some correctness properties depend directly on values:

```text
index bounded by a particular collection
buffer sized for a particular payload
matrix dimensions
protocol phase
array length
resource count
```

Dependent typing offers a principled way to represent such relationships.

C++L includes this direction because a verification language limited to value-independent types would eventually hit significant expressive limits.

The design goal is progressive use rather than requiring dependent typing throughout normal C++ code.

---

# 22. Why inductive data and structural reasoning are useful

Closed data forms provide the verifier with information that ordinary open-ended representation patterns may obscure:

```text
complete constructor set
case distinction
structural recursion
induction principle
exhaustiveness
```

These capabilities are valuable for formal reasoning over:

```text
trees
syntax
state machines
results
recursive structures
```

C++L therefore explores explicit inductive forms alongside ordinary C++ types.

Their normative semantics are defined in `SPEC.md`.

---

# 23. Why impossible states are valuable

Repeatedly proving that invalid combinations never occur is often weaker than choosing a representation where they cannot be constructed naturally.

For example:

```text
Success(value)
or
Failure(error)
```

communicates more structural information than several loosely related flags and optionals.

C++L therefore favors designs where useful invariants can move into types or constructors rather than remaining perpetual proof obligations.

This is a design preference, not a requirement that ordinary C++ code be rewritten.

---

# 24. Why ghost state exists

Proofs sometimes need concepts that have no reason to exist at runtime:

```text
initial value
logical history
abstract resource token
previous state
induction witness
```

Representing those concepts explicitly can make proofs easier to understand.

Making them runtime objects would impose unnecessary cost.

Ghost state exists to support that proof vocabulary while keeping it conceptually separate from executable state.

The exact erasure rules belong in `SPEC.md`.

---

# 25. Why proof information is designed to erase

C++L is aimed at systems programming.

Requiring proof objects to remain in production binaries would introduce costs such as:

```text
extra memory
extra execution
different layouts
different calling conventions
additional runtime dependencies
```

for information whose primary purpose is compile-time verification.

The design therefore strongly favors proof erasure.

The normative erasure semantics belong in `SPEC.md`.

The implementation of erasure belongs in `ARCHITECTURE.md`.

---

# 26. Why there is no theorem runtime in the basic model

C++L is not intended to turn native C++ executables into theorem interpreters.

The desired conceptual lifecycle is:

```text
reason before execution
        ↓
accept or reject the program
        ↓
ship ordinary native code
```

A mandatory proof VM or theorem garbage collector would move C++L away from its systems-programming goals.

This does not prohibit runtime libraries for ordinary application purposes.

It means proof itself is not intended to require one.

---

# 27. Why runtime validation remains necessary

Some information simply does not exist until execution:

```text
user input
network packets
database rows
files
sensor readings
operating-system responses
```

No amount of compile-time reasoning can predict arbitrary future input.

The design therefore includes an explicit boundary where dynamic information can be checked and then used with stronger knowledge afterward.

This preserves a clear distinction between:

```text
universal compile-time reasoning
```

and:

```text
facts discovered during one execution
```

The normative distinction belongs in `SPEC.md`.

---

# 28. Why failed proof and runtime checking are not automatically interchangeable

Automatically turning an unproved claim into a runtime assertion may appear convenient.

It also changes both:

```text
assurance
```

and:

```text
runtime behavior
```

without the developer explicitly choosing that trade.

C++L therefore treats static verification and dynamic validation as separate tools.

This makes the assurance model visible rather than silently degrading it.

---

# 29. Why explicit unsafe regions are useful

Systems programming inevitably reaches operations that may be difficult or impossible for a verifier to model immediately:

```text
assembly
hardware registers
platform intrinsics
foreign runtimes
low-level allocation
special pointer manipulation
```

Rejecting every such program would make the language impractical.

Pretending the operations are proven would make it unsound.

An explicit unsafe boundary gives developers a way to acknowledge that gap.

The exact semantics belong in `SPEC.md`.

---

# 30. Why unsafe and trusted are different concepts

Two fundamentally different situations occur at system boundaries:

```text
we are not proving this operation
```

and:

```text
we are accepting this proposition as an assumption
```

Combining them would make low-level code an easy path for injecting arbitrary logical facts.

C++L therefore keeps these ideas conceptually separate.

The normative distinction and propagation rules belong in `SPEC.md` and `TRUST.md`.

---

# 31. Why explicit trust exists

No practical system proves reality from first principles.

Eventually a system depends on something such as:

```text
hardware
compiler behavior
operating-system behavior
foreign libraries
cryptographic assumptions
environmental contracts
formal models
```

Hiding those assumptions creates false confidence.

Explicit trust exists so that assumptions can be inspected, reviewed, and traced.

`TRUST.md` defines the actual trust model.

---

# 32. Why assurance states should remain distinguishable

A useful verification environment needs to communicate more than:

```text
green
red
```

Different results may come from:

```text
proof
explicit assumption
dynamic validation
lack of verification
unsupported reasoning
unsafe execution
```

Conflating these would make audit and CI information much less useful.

The exact status set and semantics are normative matters and belong in `SPEC.md`.

---

# 33. Why C++ machine semantics matter

It is tempting for a verifier to simplify C++ arithmetic into ordinary mathematics.

That can prove properties of a program that does not actually exist.

For example, machine arithmetic includes issues such as:

```text
bounded representation
overflow behavior
bit operations
shifts
conversion rules
```

C++L is designed around semantic fidelity.

Mathematical domains are useful, but they should be explicit abstractions rather than silent replacements for executable C++ semantics.

The normative arithmetic model belongs in `SPEC.md`.

---

# 34. Why floating point cannot be treated as real arithmetic

The same reasoning applies more strongly to floating point.

Floating-point computation involves behavior such as:

```text
rounding
NaN
infinity
signed zero
finite precision
target-specific details
```

Treating those values as exact real numbers may produce elegant but irrelevant proofs.

C++L therefore favors reasoning about the execution model that actually runs.

---

# 35. Why undefined behavior is important to proof

Formal verification depends on there being a meaningful execution semantics to reason about.

Undefined behavior can remove that foundation.

A verification system that proves properties while ignoring reachable undefined behavior risks proving statements about an idealized program rather than the executable.

This is why modeling defined behavior is a central design concern.

The exact verification requirements belong in `SPEC.md`.

---

# 36. Why the C++ object model cannot be abstracted away casually

For many systems properties, correctness depends on:

```text
lifetime
ownership
aliasing
references
moves
destruction
pointer validity
object identity
```

A verifier that replaces those concepts with a much simpler imaginary machine may become easier to build but less relevant to real C++.

C++L therefore favors progressively modeling actual C++ semantics instead of pretending they do not matter.

---

# 37. Why existing C++ semantic infrastructure should be reused

Reimplementing C++ semantics would require reproducing enormous complexity:

```text
name lookup
overload resolution
templates
conversions
constexpr
object lifetime
language modes
extensions
layout
ABI rules
```

It would also create a risk that:

```text
the verifier understands one program
```

while:

```text
the native compiler executes another
```

The design therefore favors reusing mature C++ semantic infrastructure.

The current implementation strategy is documented in `ARCHITECTURE.md`.

---

# 38. Why Clang is a natural semantic foundation

Clang already provides mature machinery for understanding real C++ programs and integrates directly with LLVM.

Using that machinery reduces duplication and keeps C++L close to mainstream compiler semantics.

The motivation is not loyalty to one implementation.

It is to avoid building and maintaining a second complete C++ frontend without a compelling reason.

The exact dependency and integration model belongs in `ARCHITECTURE.md`.

---

# 39. Why a permanent compiler fork is undesirable by default

Forking a major compiler can unlock deep customization.

It also creates continuing costs:

```text
upstream merges
security updates
new language standards
platform support
toolchain drift
release maintenance
```

C++L therefore prefers external integration where it provides sufficient control.

A fork remains a possible engineering response if a hard requirement eventually justifies its cost.

That decision would be architectural rather than semantic.

---

# 40. Why C++L needs a formal representation distinct from the C++ AST

A C++ AST is designed to represent C++.

Verification needs additional concepts such as:

```text
propositions
proof terms
logical assumptions
formal equality
refinement facts
verification obligations
ghost information
```

Using the raw C++ AST as the entire proof language would couple proof reasoning too tightly to frontend representation details.

C++L therefore benefits from a verification-oriented representation derived from resolved C++ meaning.

The key design constraint is that this representation should describe the same program rather than inventing a competing interpretation.

Its concrete form belongs in `ARCHITECTURE.md`.

---

# 41. Why semantic drift between verification and execution is dangerous

One of the largest risks in verification tooling is:

```text
prove model A
execute implementation B
```

If A and B diverge, the proof can remain internally correct while becoming irrelevant to the shipped program.

C++L design therefore places strong importance on maintaining a defensible relationship between:

```text
formal meaning
```

and:

```text
runtime meaning
```

The mechanisms used to maintain that relationship belong in `ARCHITECTURE.md` and `TRUST.md`.

---

# 42. Why the compiler implementation can use a newer C++ standard

The language used to build a compiler and the languages accepted by that compiler are separate concerns.

Using a modern implementation standard gives the C++L codebase access to better implementation tools without requiring user projects to adopt the same source standard.

This separation is particularly valuable for C++L because supporting existing code is a central project goal.

The supported user language modes are defined in `COMPATIBILITY.md`.

---

# 43. Why C++23 is the initial implementation choice

C++23 keeps the implementation in the same ecosystem as Clang while providing useful modern facilities for compiler engineering.

Examples include:

```text
strong value-oriented programming
std::variant
std::optional
std::expected
concepts
modern constexpr
improved standard-library facilities
RAII
```

Using C++ also avoids introducing a large FFI boundary directly through the Clang-facing portion of the compiler.

This is an implementation choice, not part of the C++L language definition.

---

# 44. Why proof-critical implementation should be conservative

Proof-critical code has a different optimization target from ordinary product code.

Its primary qualities are:

```text
auditability
predictability
small surface area
determinism
clear ownership
clear control flow
```

Clever abstractions can make such code harder to inspect.

The design therefore favors deliberately boring implementation techniques in the most sensitive parts of the verifier.

Exact coding and dependency rules belong in `AGENTS.md`, `TRUST.md`, and `ARCHITECTURE.md`.

---

# 45. Why self-hosted verification is a long-term goal

A language intended to improve confidence in C++ software provides an especially interesting test case for itself:

```text
its own compiler
```

As C++L matures, compiler invariants can increasingly be expressed using C++L itself.

That creates a gradual path:

```text
compiler written in C++
        ↓
C++L becomes capable
        ↓
critical invariants gain Laws
        ↓
more of the compiler verifies itself
```

This is preferable to requiring the initial implementation to bootstrap from an immature language.

---

# 46. Why existing libraries remain part of the model

Real C++ programs depend on enormous ecosystems.

A useful C++ verification language should coexist with:

```text
standard libraries
Boost
platform SDKs
C libraries
C++ libraries
binary dependencies
proprietary libraries
```

Requiring replacement equivalents would undermine the purpose of extending C++.

Different dependencies may provide different levels of formal information.

C++L's design therefore focuses on making boundaries visible rather than pretending the external ecosystem is already verified.

---

# 47. Why whole-world verification is not the starting point

Proving:

```text
an application
its libraries
its operating system
its hardware
```

before any local property becomes useful is unrealistic.

C++L instead aims to support meaningful local guarantees whose assumptions and boundaries remain visible.

This is a practical compromise in scope, not a claim that external assumptions disappear.

---

# 48. Why proof failure should be conservative

Verification has an asymmetric failure cost.

A false negative means:

```text
something true was not proven
```

A false positive means:

```text
something unestablished was presented as proven
```

For a verification system, the latter is substantially more damaging.

C++L therefore favors conservative failure when required reasoning is unavailable.

The exact acceptance behavior belongs in `SPEC.md`.

---

# 49. Why unsupported verification should not automatically reject ordinary C++

Conservative proof behavior and C++ compatibility pull in different directions.

The useful distinction is between:

```text
the verifier cannot establish this property
```

and:

```text
the underlying C++ program is invalid
```

Keeping those concepts separate allows C++L to remain useful as a C++ toolchain even while formal coverage grows incrementally.

The normative behavior belongs in `SPEC.md` and `COMPATIBILITY.md`.

---

# 50. Why tests remain important

Formal proof addresses properties represented in the formal model.

Software systems still interact with:

```text
real operating systems
real files
real networks
performance constraints
user interfaces
external services
hardware
```

Tests remain useful for those dimensions and for ordinary regression detection.

C++L is therefore designed to complement testing rather than replace it.

What it rejects conceptually is treating a collection of successful examples as equivalent to a universal proof.

---

# 51. Why counterexamples are useful but asymmetric

Finding one valid counterexample can destroy a universal claim.

Failing to find one cannot generally establish the claim.

This asymmetry makes counterexample generation extremely useful for:

```text
diagnostics
debugging
AI feedback
proof development
```

without making it a replacement for proof.

---

# 52. Why determinism matters

Compiler verification results need to be useful in:

```text
CI
code review
reproducible builds
distributed development
security analysis
caching
```

Results that depend accidentally on:

```text
pointer addresses
thread schedules
hash iteration order
random proof-search order
```

would make those workflows difficult to trust.

C++L therefore values deterministic semantic results even when search strategies themselves may use heuristics.

The concrete mechanisms belong in `ARCHITECTURE.md`.

---

# 53. Why incremental verification matters

Large C++ repositories may contain millions of lines of code.

Rechecking every theorem after every edit would make verification impractical.

The design therefore favors semantic dependency tracking so that a change invalidates the reasoning actually affected by that change.

Conceptually:

```text
source change
    ↓
semantic change
    ↓
affected obligations
```

rather than:

```text
source change
    ↓
prove entire repository again
```

The implementation strategy belongs in `ARCHITECTURE.md`.

---

# 54. Why semantic identity matters more than file identity

Proof validity depends on semantic inputs.

It does not fundamentally depend on:

```text
file timestamp
editor session
temporary path
memory address
```

This motivates designing future proof artifacts and caches around stable semantic identity.

That supports:

```text
reproducibility
incremental checking
remote caches
distributed CI
```

without making caching part of the language semantics.

---

# 55. Why native ABI compatibility matters

C++ is frequently chosen precisely because it can interact directly with existing native binaries and platform interfaces.

Formal features that unnecessarily alter calling conventions or runtime representations would make adoption much harder.

This creates strong design pressure toward compile-time-only representations for compile-time-only information.

The actual ABI guarantees belong in `COMPATIBILITY.md`.

---

# 56. Why headers remain important

C++ does not place all important semantics in `.cpp` files.

Headers frequently contain:

```text
public interfaces
templates
inline functions
types
concepts
constants
library contracts
```

A verification system designed around implementation files alone would not fit the language ecosystem.

C++L therefore treats headers as first-class places for formal interfaces.

The compatibility details belong in `COMPATIBILITY.md`.

---

# 57. Why a dedicated file extension is optional

A dedicated extension can communicate:

```text
this source intentionally uses C++L syntax
```

and may be convenient for tools.

Requiring one for every existing source file would create migration work with little semantic value.

The design therefore favors language capability over filename identity.

Supported extensions belong in `COMPATIBILITY.md`.

---

# 58. Why grammar is separate from semantics

Concrete syntax and semantic meaning evolve differently.

For example, the project might eventually improve the spelling of a construct without changing its formal meaning.

Separating:

```text
GRAMMAR.md
```

from:

```text
SPEC.md
```

keeps both documents easier to reason about.

`DESIGN.md` explains why a particular syntax direction was selected but does not define what syntax is legal.

---

# 59. Why design rationale is separate from specification

A specification answers:

```text
What does the language mean?
```

Design rationale answers:

```text
Why was that meaning chosen?
```

Mixing them makes it difficult to distinguish:

```text
normative requirement
```

from:

```text
motivation
historical reason
tradeoff
preference
```

C++L deliberately separates the two.

---

# 60. Why trust policy is separate from general design

Trust deserves focused treatment.

Questions such as:

```text
What is inside the TCB?
What assumptions can enter a proof?
How are assumptions reported?
What happens when trust propagates?
```

need more precision than a general rationale document should provide.

`DESIGN.md` explains why explicit and minimal trust is desirable.

`TRUST.md` defines the actual trust model.

---

# 61. Why implementation architecture is separate from design rationale

Language and proof concepts should survive normal compiler evolution.

For example, these implementation changes should not inherently redefine C++L:

```text
splitting a compiler pass
changing an internal data structure
moving the LSP to another process
changing a cache format
replacing one solver adapter
parallelizing verification
```

`ARCHITECTURE.md` therefore owns the implementation structure.

`DESIGN.md` records only the reasons behind major architectural directions where those reasons are useful for future decisions.

---

# 62. Rejected direction: mandatory whole-program verification

The project does not start from the assumption that every dependency must be proved before useful verification can occur.

That direction was rejected because it would make adoption impractical for most existing C++ systems.

Explicit boundaries and incremental assurance provide a more realistic path.

---

# 63. Rejected direction: tests as proofs

Tests observe selected executions.

Laws may describe properties over unbounded classes of executions or values.

Treating enough tests as equivalent to proof would blur an important distinction and provide misleading assurance.

Testing remains complementary.

---

# 64. Rejected direction: runtime assertions as proofs

A runtime assertion can establish something about a particular execution path at runtime.

It does not by itself provide a universal compile-time theorem.

Conflating the two would obscure the difference between dynamic checking and static reasoning.

---

# 65. Rejected direction: silent runtime fallback after proof failure

A system could respond to failed proof by automatically inserting a runtime check.

That is convenient, but it silently changes:

```text
the assurance model
```

and potentially:

```text
program behavior
```

The design instead favors making such a transition explicit.

---

# 66. Rejected direction: solver as unconditional truth authority

Modern solvers are powerful and extremely valuable.

Making their implementation part of the unquestioned logical authority by default would substantially enlarge the trusted base.

C++L therefore prefers architectures where solver intelligence can be separated from final evidence checking where practical.

The precise trust model belongs in `TRUST.md`.

---

# 67. Rejected direction: global reservation of C++L vocabulary

Globally reserving all new C++L terms would unnecessarily invalidate existing source.

Contextual syntax provides a better compatibility path.

The exact disambiguation rules are defined in `GRAMMAR.md`.

---

# 68. Rejected direction: repurposing unrelated C++ keywords

Giving existing C++ syntax a second unrelated formal meaning would create unnecessary confusion and parser complexity.

C++L therefore favors distinct formal vocabulary where the underlying concept is genuinely new.

---

# 69. Rejected direction: unsafe as a proof escape hatch

If low-level code automatically implied trusted facts, any unsafe operation could bypass the verifier.

That would make formal guarantees difficult to interpret.

The separation between lack of verification and explicit assumption is therefore intentional.

---

# 70. Rejected direction: silently replacing machine integers with mathematics

Reasoning over ideal mathematical integers is often easier.

Doing it silently for executable C++ values would risk proving properties that fail under machine semantics.

C++L instead favors explicit abstraction when mathematical domains are desired.

---

# 71. Rejected direction: proving a rewritten shadow implementation

Maintaining:

```text
one implementation for proof
```

and:

```text
another implementation for execution
```

creates a dangerous semantic synchronization problem.

C++L is designed around verifying meaning connected to the program that actually executes.

---

# 72. Rejected direction: mandatory proof runtime

Keeping proof machinery alive in every executable would add cost and deployment requirements unrelated to ordinary program execution.

That conflicts with the project's systems-programming goals.

Compile-time reasoning with runtime erasure is the preferred direction.

---

# 73. Rejected direction: reimplement all C++ semantics

A new C++ semantic frontend would be a massive undertaking and another source of disagreement with mainstream toolchains.

C++L gains more by concentrating engineering effort on:

```text
formal specification
verification
proof
diagnostics
```

than by recreating mature C++ semantics unnecessarily.

---

# 74. Rejected direction: permanent Clang fork as the default

A fork can solve integration problems but creates continuous maintenance cost.

Starting with a permanent fork would commit the project to that cost before demonstrating that it is necessary.

External integration is therefore the preferred initial direction.

---

# 75. Rejected direction: mandatory second implementation language for the proof core

Languages with stronger memory-safety guarantees can be attractive for trusted components.

Using another language also introduces:

```text
FFI boundaries
serialization
build complexity
packaging complexity
debugging complexity
```

and does not automatically establish logical soundness.

The initial preference is to keep the implementation coherent while minimizing and auditing proof-critical code.

This choice can be reconsidered if evidence later favors another approach.

---

# 76. Rejected direction: clever proof-critical code

Proof-critical implementation benefits more from being understandable than from showcasing advanced language techniques.

Abstraction remains useful, but obscuring proof rules behind metaprogramming or implicit behavior would make review harder.

The project therefore values boring implementation in the most sensitive regions.

---

# 77. Design pressure: compatibility versus proof power

Supporting real C++ introduces semantics that are substantially harder to verify than a deliberately small language.

Weakening C++ compatibility would make verification easier.

Weakening proof fidelity would make verification less meaningful.

C++L deliberately accepts this tension.

The preferred response to unsupported semantics is gradual modeling rather than pretending complexity does not exist.

---

# 78. Design pressure: automation versus transparency

Manual proof everywhere would be unusable.

Opaque automation everywhere would be difficult to audit.

C++L aims for:

```text
strong automation
+
explicit specifications
+
inspectable proof boundaries
```

The user should benefit from automation without needing to treat it as magic.

---

# 79. Design pressure: proof strength versus verification cost

Precise reasoning can be expensive.

The project does not want compile-time performance problems solved by quietly weakening proof meaning.

Instead, the preferred engineering directions include:

```text
incrementality
dependency analysis
proof reuse
parallel search
semantic caching
specialized decision procedures
```

These are architectural optimizations around stable semantics.

---

# 80. Design pressure: formal expressiveness versus usability

A language capable of advanced formal reasoning can easily become inaccessible to ordinary C++ developers.

C++L therefore favors progressive disclosure.

A developer should be able to start with simple contracts and Laws before needing concepts such as:

```text
dependent types
induction
advanced proof terms
complex formal abstractions
```

Advanced power remains available without becoming the entry price for basic verification.

---

# 81. Design pressure: C++ familiarity versus formal clarity

Making every new construct look exactly like existing C++ can reduce initial visual novelty.

It can also hide important semantic distinctions.

The preferred balance is:

```text
retain C++ where the concept is genuinely C++

use explicit formal syntax where a genuinely new concept exists
```

This keeps C++L recognizable without pretending proof is just another ordinary C++ expression.

---

# 82. Design pressure: zero runtime cost versus dynamic reality

Proof-only information can often disappear entirely.

Dynamic validation cannot.

The design therefore distinguishes:

```text
information needed only to establish compilation
```

from:

```text
checks inherently required because the value is unknown until runtime
```

This lets C++L pursue zero-cost proof abstractions without making impossible promises about external input.

---

# 83. Design pressure: local reasoning versus whole-system truth

Developers need useful local guarantees.

Those guarantees inevitably depend on assumptions about surrounding components.

C++L therefore emphasizes making dependency boundaries explicit rather than claiming local proof somehow establishes every property of the entire machine.

---

# 84. Design pressure: language evolution versus stability

C++L will evolve as difficult areas become better understood:

```text
memory semantics
concurrency
standard-library modeling
templates
dependent typing
proof automation
```

The project should avoid prematurely locking large amounts of syntax around poorly understood semantics.

Small, principled additions are preferable to speculative surface area.

---

# 85. Initial implementation philosophy

The first implementation should prove something small **for real**.

A narrow vertical slice with:

```text
real semantic analysis
real formal obligation
real proof checking
real rejection of a false case
real native output
```

is more valuable than a broad collection of syntax whose verification is mostly placeholder behavior.

This principle guides the implementation roadmap but does not define language semantics.

---

# 86. Growth philosophy

New C++L capabilities should generally follow this order:

```text
understand the property
        ↓
define semantics
        ↓
define syntax
        ↓
define trust implications
        ↓
design implementation
        ↓
add conformance tests
        ↓
ship
```

Implementation convenience should not become the source of language meaning.

---

# 87. Self-verification direction

Over time, C++L itself should become one of the strongest real-world users of C++L.

Compiler components provide valuable targets for Laws concerning:

```text
determinism
representation invariants
proof checking
erasure
semantic preservation
serialization
dependency handling
```

This creates useful pressure for the language to solve real systems problems rather than only academic examples.

---

# 88. AI-oriented design direction

AI changes the economics of code production.

Generating code is becoming cheaper.

Establishing that generated code satisfies human intent remains difficult.

C++L is designed around the idea that formal specifications can become a durable interface between:

```text
human intent
```

and:

```text
machine-generated implementation
```

A model can attempt many implementations.

The Law remains the thing they are expected to satisfy.

---

# 89. Criteria for future language proposals

When evaluating a proposed feature, useful design questions include:

```text
What problem does this solve?

Why does it belong in the language rather than tooling?

Does it describe actual C++ execution?

Does it create a second interpretation of C++?

Does it make existing-code adoption harder?

Can proof-only information remain compile-time-only?

Does it make assumptions more explicit or less explicit?

Does it unnecessarily enlarge the trusted base?

Can unsupported cases remain honest rather than guessed?

Can it be introduced incrementally?

Does the syntax collide with ordinary C++?

Can an AI or automated tool exploit it to bypass rather than satisfy verification?

Is the feature understandable without knowing compiler internals?
```

These questions are guidance, not normative conformance rules.

---

# 90. Criteria for future implementation decisions

Implementation proposals should be judged differently from language proposals.

Useful questions include:

```text
Does this implementation preserve the semantics already specified?

Does it reuse existing C++ semantic infrastructure where appropriate?

Does it introduce duplicate semantic authorities?

Does it increase the trusted base unnecessarily?

Can it remain deterministic?

Can failures remain diagnosable?

Can the component evolve independently?

Does it create permanent architecture debt?

Does it scale to large C++ repositories?
```

The actual implementation invariants are documented in `ARCHITECTURE.md`, `TRUST.md`, and `AGENTS.md`.

---

# 91. Design principles

The project is guided by these design principles:

```text
Extend C++ rather than replace it.

Keep runtime behavior grounded in real C++ semantics.

Make verification incremental.

Treat formal intent as first-class source.

Keep claims separate from evidence.

Keep assumptions visible.

Use automation aggressively without making automation the definition of truth.

Prefer small proof authority over large proof authority.

Keep proof-only information out of runtime where practical.

Distinguish static proof from dynamic validation.

Model machine behavior rather than silently replacing it with ideal mathematics.

Reuse mature C++ semantics rather than rebuilding C++ unnecessarily.

Prefer honest incompleteness over false proof.

Keep verification connected to the program that actually executes.

Preserve interoperability as a core design pressure.

Make advanced formal power progressively accessible.

Design for both human-written and AI-generated implementations.
```

---

# 92. Summary

C++L is motivated by a simple gap:

```text
C++ is excellent at saying how a machine should execute.

It is much less expressive at saying, in a machine-checkable way,
what must always be true about that execution.
```

The project explores adding that missing layer without discarding the C++ ecosystem.

The intended relationship is:

```text
existing C++ execution model
        +
precise formal intent
        +
machine-checkable evidence
```

The resulting design favors:

```text
incremental adoption
explicit specifications
explicit assumptions
proof-producing automation
small proof authority
semantic fidelity to C++
proof erasure
native execution
existing-library interoperability
AI-compatible verification workflows
```

`DESIGN.md` records **why** those directions were chosen.

The authoritative definition of what C++L actually means remains in `SPEC.md`, `GRAMMAR.md`, `COMPATIBILITY.md`, and `TRUST.md`.
