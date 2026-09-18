# C++L Design

This document describes the intended language and compiler architecture for C++L.

## Design goal

C++L is not a verifier bolted onto C++.

It is a genuine source-compatible C++ superset with additional proof/type syntax and semantics:

```text
C++ ⊂ C++L
```

C++L should preserve ordinary C++ while adding first-class constructs such as:

```cpp
law
proves
proof
pure
verified
ghost
unsafe
trusted
data
match
where
expects
ensures
decreases
forall
exists
```

The goal is not compatibility with another theorem language's implementation.

The goal is to provide the capabilities necessary for humans or AIs to express unambiguous intent and mechanically prove that an implementation satisfies it.

## Compiler architecture

Preferred architecture:

```text
.cppl
   ↓
C++L extension parser
   ↓
elaborator
   ↓
formal type/proof system
   ↓
verification IR
   │
   ├── normalization
   ├── termination
   ├── induction
   ├── refinement solving
   ├── proof construction
   └── SMT automation
   ↓
trusted proof kernel
   ↓
PROVEN
   ↓
proof / ghost / type erasure
   ↓
ordinary C++
   ↓
stock Clang / LLVM
```

Do not fork Clang unless technically necessary.

Do not reimplement the full C++ parser.

C++L should parse only its own extensions and delegate ordinary C++ syntax, semantics, overload resolution, templates, ABI, optimization, and code generation to Clang wherever practical.

## Laws

A Law is a universal proof obligation.

```cpp
law identity<T>(T x)
    proves id(x) == x;
```

Conceptually:

```text
∀ T,
∀ x : T,
    id(x) = x
```

A Law is not a runtime Boolean and must not be reduced to testing.

If the implementation cannot establish the Law, verified compilation fails.

## Propositions and proofs

C++L treats propositions as formal types or provides an equivalently sound proof calculus.

```text
proposition ≈ type
proof       ≈ valid evidence of that proposition
```

Example:

```cpp
Proof<x == x> reflexivity(x);
```

False propositions must not be constructible inside verified C++L.

Proof evidence must not be forgeable through:

- casts
- undefined behavior
- FFI
- arbitrary memory writes
- nontermination
- unchecked solver output

## Universal quantification

C++L must support:

```text
∀ x : T,
    P(x)
```

A Law with an argument naturally represents universal quantification.

The theorem must hold for every value represented by the type, not merely values selected by tests.

## Existential quantification

C++L must support:

```text
∃ x : T,
    P(x)
```

An existential proof contains:

```text
witness
+
proof that the witness satisfies P
```

## Dependent types

Types may depend on values.

```cpp
Vector<T, n>
Matrix<T, rows, columns>
Fin<n>
```

Example:

```cpp
T get<T, n>(
    const Vector<T, n>& xs,
    Fin<n> index
);
```

The type carries the relationship between `index` and the vector length.

Dependent types must represent genuine value/type relationships and must not merely be cosmetic wrappers around runtime assertions.

## Refinement types

C++L supports practical constrained types.

```cpp
type Percentage =
    int where self >= 0 && self <= 100;

type NonZeroInt =
    int where self != 0;

type PositiveMoney =
    Money where self.cents >= 0;
```

Construction and return of refined values create proof obligations.

## Equality

C++L must support proof-relevant equality including:

- reflexivity
- symmetry
- transitivity
- substitution
- rewriting
- congruence
- transport across dependent types
- definitional equality

Example:

```cpp
proof reflexive;
```

may close a goal when both sides reduce to the same normal form.

## Definitional equality and normalization

Pure proof-relevant computation may be reduced during checking.

```text
add(Zero, x)
```

may normalize to:

```text
x
```

so:

```text
add(Zero, x) == x
```

can be established directly.

Normalization must be deterministic.

The checker must distinguish definitional equality from equality that requires explicit proof.

## Induction

C++L supports symbolic proofs over recursive structures.

```cpp
law list_append_identity<T>(List<T> xs)
    proves append(xs, Nil) == xs
{
    match xs {
        Nil => {
            proof reflexive;
        }

        Cons(x, rest) => {
            proof use list_append_identity(rest);
            proof reflexive;
        }
    }
}
```

One recursive proof establishes the property for every finite list.

Induction must operate over the structure of values rather than enumerating concrete inputs.

## Termination

Proof-producing code must not establish false propositions through infinite recursion.

Verified recursive code must prove termination.

C++L should support:

```cpp
decreases expression;
```

Verified functions may use:

- structural descent
- well-founded recursion
- explicit termination measures

Potentially nonterminating code must cross an explicit non-verified boundary.

Nontermination must never be usable to construct arbitrary proofs.

## Algebraic data types

C++L should provide proof-friendly algebraic data types where ordinary C++ facilities are insufficient.

Example:

```cpp
data Nat {
    Zero;
    Succ(Nat);
}
```

```cpp
data List<T> {
    Nil;
    Cons(T, List<T>);
}
```

Runtime lowering should remain efficient C++.

## Pattern matching

C++L provides proof-aware exhaustive pattern matching.

```cpp
match value {
    ...
}
```

Pattern matching may:

- prove exhaustiveness
- reject unreachable branches
- refine types
- introduce constructor fields
- refine the proof context
- eliminate impossible states

## Impossible states

Types should allow impossible states to be represented as uninhabited.

Example:

```cpp
Fin<0> index;
```

`Fin<0>` has no valid inhabitants.

A proof branch requiring one can therefore be eliminated.

## Compile-time contracts

C++L supports formal preconditions and postconditions.

```cpp
pure int divide(int a, int b)
    expects b != 0
{
    return a / b;
}
```

Callers in verified code must establish the precondition.

```cpp
pure int abs(int x)
    expects x != INT_MIN
    ensures result >= 0
{
    return x < 0 ? -x : x;
}
```

Postconditions become reusable facts for callers.

Contracts are proof interfaces, not runtime assertions.

## Pure code and effects

C++L distinguishes at least:

```text
pure
verified
ghost
unsafe
trusted
```

A `pure` function must be suitable for mathematical reasoning and normalization.

It must not silently depend on uncontrolled external state.

The exact effect system does not need to copy another language's implementation, but it must preserve proof soundness.

## Ghost data

Ghost values exist for verification but disappear before runtime code generation.

```cpp
ghost int expected_size;
```

Ghost data may influence proofs.

Ghost data must not influence runtime behavior after erasure.

## Proof erasure

Proof information must normally have zero runtime cost.

```text
C++L:

algorithm
+
proofs
+
ghost state
+
formal types

        ↓ erasure

C++:

algorithm
```

Erasure must preserve executable semantics.

## No dedicated theorem runtime

The theorem system is primarily compile-time.

The native application should not require:

- a theorem VM
- a proof evaluator
- a runtime proof checker
- a mandatory C++L garbage collector
- a C++L execution engine

After erasure:

```text
C++L
    ↓
ordinary C++
    ↓
Clang / LLVM
    ↓
native executable
```

## External runtime data

Compile-time proofs cannot know arbitrary future external values.

External values therefore begin as untrusted data.

```text
network
camera
file
user input
FFI
    ↓
untrusted value
    ↓ validate / prove
verified value
    ↓
verified computation
```

Example:

```cpp
int raw = read_network();

Percentage p = checked<Percentage>(raw);
```

If `raw` cannot be proven valid statically, the boundary performs an explicit runtime validation.

That is data validation, not a theorem runtime.

## Verified and ordinary C++

All supported ordinary C++ should remain valid C++L source.

But ordinary C++ is not automatically proof-safe.

```text
C++L
├── ordinary C++
│   └── executable, no automatic theorem guarantees
│
└── verified C++L
    └── formal guarantees apply
```

This distinction allows C++L to remain compatible with the existing ecosystem without pretending that arbitrary legacy C++ satisfies formal guarantees.

## Unsafe code

Some systems operations require an explicit unsafe boundary.

Examples:

- `reinterpret_cast`
- arbitrary raw pointer arithmetic
- unchecked FFI
- inline assembly
- volatile hardware access
- placement construction
- externally mutated memory

C++L must support:

```cpp
unsafe {
    ...
}
```

Unsafe code may produce runtime data.

Unsafe code must not silently manufacture proof evidence.

## Trusted assumptions

Some external behavior may need to be assumed.

Such assumptions must be explicit:

```cpp
trusted ...
```

The compiler should report them:

```text
Verification complete

Laws proven:            241
Trusted assumptions:      2
Unsafe boundaries:        5
Unresolved obligations:   0
```

There must be no hidden trust.

## C++ undefined behavior

Formal guarantees are meaningless if generated C++ can invalidate them through UB.

Verified C++L must therefore prove or explicitly reject relevant cases including:

- signed overflow
- division by zero
- invalid shifts
- out-of-bounds access
- null dereference
- invalid pointer arithmetic
- lifetime violations
- use-after-free
- uninitialized reads
- invalid references
- invalid casts
- data races where concurrency reasoning applies

## Machine arithmetic

C++L must distinguish mathematical integers from machine integers.

Possible verified types:

```cpp
i8
i16
i32
i64

u8
u16
u32
u64

Checked<T>
Wrapping<T>
Saturating<T>
BigInt
```

Proof semantics must match runtime semantics.

C++L must never prove a property under unbounded mathematical arithmetic and silently execute different overflowing machine arithmetic.

## C++ memory semantics

The proof model must account for:

- object lifetime
- references
- pointer provenance
- aliasing
- mutation
- move semantics
- ownership
- concurrency where applicable

C++L does not need to copy another language's affine or ownership calculus.

It does need a sound model sufficient to prevent executable C++ semantics from invalidating verified propositions.

## Trusted proof kernel

C++L must have a deliberately small trusted proof kernel.

```text
Law
    ↓
elaboration
    ↓
automation / proof construction
    ↓
proof term / certificate
    ↓
small trusted kernel
    ↓
accepted / rejected
```

The kernel should validate core rules such as:

- typing
- proposition formation
- equality
- substitution
- function application
- dependent application
- universal quantification
- existential quantification
- induction
- refinement introduction/elimination
- normalization
- proof composition

The trusted computing base should be small and auditable.

## Automation

C++L should be easier to use than a traditional proof assistant.

Automation may include:

```cpp
proof auto;
proof simplify;
proof arithmetic;
proof rewrite theorem;
proof use theorem(args);
proof induction value;
proof contradiction;
```

Automation may use:

- cvc5
- Z3
- rewriting engines
- arithmetic solvers
- simplifiers
- proof search

Automation must not silently redefine what counts as truth.

Where practical, automation should produce evidence or certificates checked by the C++L kernel.

Any solver that must temporarily remain inside the trusted computing base must be reported explicitly.

## Counterexamples

When a proposition is false and a model can be found, C++L should show a concrete counterexample.

```text
error: law 'settlement_closes' is false

counterexample:

items        = 10.00
discount     = -2.00
deposit      = 1.00
printedTotal = 10.00

derived total = 9.00
```

## Diagnostics

A failed proof should expose:

- current theorem
- assumptions
- local proof context
- remaining goal
- relevant source location
- attempted reductions
- solver counterexample if available

Bad:

```text
verification failed
```

Good:

```text
error: law 'append_identity' not proven

context:
  xs = Cons(head, tail)

known:
  append(tail, Nil) == tail

goal:
  Cons(head, append(tail, Nil))
    ==
  Cons(head, tail)

hint:
  rewrite using append_identity(tail)
```

## AI-oriented design

C++L is intended to work well with AI-generated software.

Specifications should be separable from implementation.

```cpp
law money_conservation(Transaction const& tx)
    proves
        incoming(tx)
        ==
        outgoing(tx) + retained(tx);
```

An AI may implement the system.

C++L independently determines whether the implementation satisfies the Law.

```text
human defines intent
        ↓
AI implements
        ↓
compiler proves
        ↓
human need not trust implementation blindly
```

The compiler should expose proof obligations in structured form so AI systems can iteratively repair implementations or construct missing proofs.

## Laws as specifications

Implementation may evolve freely.

A Law should change only when intended behavior changes.

Never weaken a Law merely because an implementation cannot satisfy it.

## Correctness outcomes over implementation parity

C++L does not need to reproduce another language's internal design.

It does not inherently require:

- a special runtime
- interaction-net execution
- automatic CPU/GPU scheduling
- another language's memory representation
- another language's exact affine quantity calculus
- another language's exact kind hierarchy
- another language's exact live/dead implementation
- `Type : Type`
- negative recursive types
- another language's template model
- another language's IO system
- another language's syntax

Any of these ideas may be adopted if they independently improve C++L.

The requirement is the correctness outcome.

## Suggested repository layout

```text
cppl/
├── frontend/
│   ├── lexer/
│   ├── extension_parser/
│   ├── syntax/
│   ├── source_map/
│   └── lowering/
│
├── clang/
│   ├── ast_bridge/
│   ├── type_bridge/
│   ├── templates/
│   └── diagnostics/
│
├── elaborator/
│
├── type_system/
│   ├── dependent/
│   ├── refinement/
│   ├── effects/
│   └── conversion/
│
├── logic/
│   ├── propositions/
│   ├── equality/
│   ├── forall/
│   └── exists/
│
├── normalization/
├── termination/
│
├── proof/
│   ├── terms/
│   ├── tactics/
│   ├── kernel/
│   └── certificates/
│
├── vir/
│
├── automation/
│   ├── cvc5/
│   ├── z3/
│   └── simplifier/
│
├── safety/
│   ├── arithmetic/
│   ├── bounds/
│   ├── lifetime/
│   ├── pointers/
│   ├── aliasing/
│   └── ub/
│
├── erase/
├── diagnostics/
├── stdl/
│
├── tools/
│   ├── cppl/
│   ├── cppl-check/
│   ├── cppl-prove/
│   └── cppl-explain/
│
└── tests/
```

## CLI

Target developer experience:

```bash
cppl build
cppl check
cppl prove
cppl explain <law>
cppl trust-report
```

Example:

```text
Parsing...................... ✓
C++ semantics................ ✓
Dependent types.............. ✓
Refinements.................. ✓
Normalization................ ✓
Termination.................. ✓
Safety obligations........... ✓
Laws.......................... ✓
Proof kernel.................. ✓

247 laws proven
0 unresolved obligations
0 trusted assumptions
4 explicit unsafe boundaries

Proof information erased.

clang++ → application
```
