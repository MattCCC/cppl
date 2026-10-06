# RFC 0022: The V1 verified C++ subset

## Status

Accepted. Records the scope of V1: which C++ constructs a verified body may use,
and three scope decisions -- arithmetic, induction and dependent reasoning. It
changes no normative rule: each refusal it records is one `SPEC.md` already
permits or requires. Implementation status: `docs/STATUS.md` ("Verified C++
subset"); the matrix itself: `tests/fixtures/subset/manifest.tsv`, checked by
`tests/e2e/safety_subset.sh`.

## Summary

`STATUS.md` ("V1 target") allows V1 to verify only a defined subset of C++, and
requires that subset to be explicit. This RFC makes it explicit and executable.
Every construct `SPEC.md` Annex X lists (`CONSTRUCT-001` to `CONSTRUCT-151`) has
one row in a manifest and one fixture per outcome:

- **verified** -- a verified body may use the construct and it is modeled: a
  fixture using it is proven with nothing unresolved and runs, and its twin, the
  same program with one thing false, is refused, which shows the construct is
  modeled rather than passed over;
- **refused** -- a verified body using the construct is refused with a
  diagnostic naming it, and no object is written.

Nothing is a third thing. A construct outside the subset stays ordinary C++,
executable outside verified bodies and inside `unsafe` blocks; it is never
verified silently.

The three decisions:

1. **Arithmetic.** V1 verifies `+`, `-`, `*`, unary `-`, `/`, `%`, the six
   comparisons and conversions between integer types, at every width from 1 to
   64 bits (RFC 0006, RFC 0019). Shifts, bitwise operators, conversions to or
   from `bool`, enumerations and floating point, the character types other than
   `char`, `signed char` and `unsigned char`, and bit-fields are refused in
   verified bodies.
2. **Induction.** V1 implements induction over unsigned machine integers
   (INDUCT-002, INDUCT-003) and nothing else. Signed integers are ill-formed
   subjects by `SPEC.md` 21.2; `@N` belongs to the proof-only domains, which V1
   does not implement.
3. **Dependent and indexed relationships.** V1 provides them through indexed
   refinements, contracts and propositions that depend on parameters, and
   indexed observation (RFC 0016). The kernel has no type families: an indexed
   refinement is applied by Clang's template substitution before the kernel
   sees it.

## Motivation

Annex X states, for each construct, the obligations a complete implementation
must preserve; it does not say which constructs an implementation verifies
today, and it should not. Before this RFC that answer was spread over
`STATUS.md` prose, the diagnostics of `clang/src/bridge.cpp` and the negative
tests, and no test failed when a construct changed sides. A construct accepted
without being modeled -- the one failure `STATUS.md` forbids outright -- would
have gone unnoticed as long as no contract depended on it.

The manifest closes that gap three ways. It names every construct, so none is
left unclassified. Its verified rows each carry a refused twin, so acceptance
alone never counts as support. And it is compared against Annex X on every run,
so a construct the specification adds is refused until it is classified.

## Goals

- One row per Annex X construct, each with the fixtures that show its outcome.
- For each verified construct, evidence that it is modeled: a refused twin.
- For each refused construct, a written-out refused fixture and the diagnostic
  it must produce.
- The three scope decisions above, each consistent with `SPEC.md`, `STATUS.md`
  and `ROADMAP.md`.

## Non-goals

- Widening the subset. Every outcome here is the implementation's today.
- Verifying programs that use a construct only outside verified bodies. That is
  ordinary C++ and is compiled as such (`SPEC.md` 4).
- Soundness of what a trusted law, a library model or an `unsafe` block
  asserts; those are trust boundaries (`TRUST.md`).

## The subset

84 constructs are verified and 67 are refused. The manifest is the normative list; this table is it at acceptance.

| Construct | Name | V1 | Refused with |
| --- | --- | --- | --- |
| CONSTRUCT-001 | integer literal | verified | -- |
| CONSTRUCT-002 | floating literal | refused | local 'scale' has type 'double', which is not modeled |
| CONSTRUCT-003 | character literal | verified | -- |
| CONSTRUCT-004 | string literal | refused | local 'text' has type 'const char *', which is not modeled |
| CONSTRUCT-005 | boolean literal | verified | -- |
| CONSTRUCT-006 | nullptr literal | verified | -- |
| CONSTRUCT-007 | identifier expression | verified | -- |
| CONSTRUCT-008 | qualified-id | verified | -- |
| CONSTRUCT-009 | `this` expression | verified | -- |
| CONSTRUCT-010 | parenthesized expression | verified | -- |
| CONSTRUCT-011 | lvalue-to-rvalue conversion | verified | -- |
| CONSTRUCT-012 | array-to-pointer conversion | refused | local 'first' has type 'const unsigned int *', which is not modeled |
| CONSTRUCT-013 | function-to-pointer conversion | refused | local 'function' has type 'unsigned int (*)(unsigned int)', which is not modeled |
| CONSTRUCT-014 | integral promotion | verified | -- |
| CONSTRUCT-015 | usual arithmetic conversion | verified | -- |
| CONSTRUCT-016 | qualification conversion | verified | -- |
| CONSTRUCT-017 | temporary materialization | verified | -- |
| CONSTRUCT-018 | unary plus | verified | -- |
| CONSTRUCT-019 | unary minus | verified | -- |
| CONSTRUCT-020 | logical not | verified | -- |
| CONSTRUCT-021 | bitwise complement | refused | operator '~' is not modeled |
| CONSTRUCT-022 | address-of | refused | local 'where' has type 'unsigned int *', which is not modeled |
| CONSTRUCT-023 | dereference | verified | -- |
| CONSTRUCT-024 | prefix increment | verified | -- |
| CONSTRUCT-025 | postfix increment | verified | -- |
| CONSTRUCT-026 | prefix decrement | verified | -- |
| CONSTRUCT-027 | postfix decrement | verified | -- |
| CONSTRUCT-028 | addition | verified | -- |
| CONSTRUCT-029 | subtraction | verified | -- |
| CONSTRUCT-030 | multiplication | verified | -- |
| CONSTRUCT-031 | division | verified | -- |
| CONSTRUCT-032 | remainder | verified | -- |
| CONSTRUCT-033 | left shift | refused | operator '<<' is not modeled |
| CONSTRUCT-034 | right shift | refused | operator '>>' is not modeled |
| CONSTRUCT-035 | bitwise and | refused | operator '&' is not modeled |
| CONSTRUCT-036 | bitwise or | refused | operator '\|' is not modeled |
| CONSTRUCT-037 | bitwise xor | refused | operator '^' is not modeled |
| CONSTRUCT-038 | less-than | verified | -- |
| CONSTRUCT-039 | less-or-equal | verified | -- |
| CONSTRUCT-040 | greater-than | verified | -- |
| CONSTRUCT-041 | greater-or-equal | verified | -- |
| CONSTRUCT-042 | equality operator | verified | -- |
| CONSTRUCT-043 | inequality operator | verified | -- |
| CONSTRUCT-044 | three-way comparison | refused | operator '<=>' is not modeled |
| CONSTRUCT-045 | logical and runtime | verified | -- |
| CONSTRUCT-046 | logical or runtime | verified | -- |
| CONSTRUCT-047 | logical conjunction formal | verified | -- |
| CONSTRUCT-048 | logical disjunction formal | verified | -- |
| CONSTRUCT-049 | conditional operator | verified | -- |
| CONSTRUCT-050 | assignment | verified | -- |
| CONSTRUCT-051 | compound assignment | verified | -- |
| CONSTRUCT-052 | comma operator | refused | the comma operator inside an expression is not modeled |
| CONSTRUCT-053 | member access dot | verified | -- |
| CONSTRUCT-054 | member access arrow | verified | -- |
| CONSTRUCT-055 | built-in subscript | verified | -- |
| CONSTRUCT-056 | overloaded subscript | refused | call does not resolve to an ordinary function or to a member function named on i |
| CONSTRUCT-057 | direct function call | verified | -- |
| CONSTRUCT-058 | member function call | verified | -- |
| CONSTRUCT-059 | virtual call | refused | a virtual call dispatches on the object's dynamic type, and override substitutab |
| CONSTRUCT-060 | function pointer call | refused | call does not resolve to an ordinary function |
| CONSTRUCT-061 | callable object invocation | refused | call does not resolve to an ordinary function or to a member function named on i |
| CONSTRUCT-062 | constructor call | refused | local 'meter' of type 'Meter' is not initialized by an aggregate initializer, so |
| CONSTRUCT-063 | conversion function call | refused | call does not resolve to an ordinary function |
| CONSTRUCT-064 | static_cast | verified | -- |
| CONSTRUCT-065 | dynamic_cast | refused | an explicit conversion is not modeled |
| CONSTRUCT-066 | const_cast | refused | local 'writable_view' has type 'unsigned int *', which is not modeled |
| CONSTRUCT-067 | reinterpret_cast | refused | local 'view' has type 'const int *', which is not modeled |
| CONSTRUCT-068 | C-style cast | verified | -- |
| CONSTRUCT-069 | functional cast | verified | -- |
| CONSTRUCT-070 | sizeof | verified | -- |
| CONSTRUCT-071 | alignof | verified | -- |
| CONSTRUCT-072 | decltype | verified | -- |
| CONSTRUCT-073 | noexcept expression | verified | -- |
| CONSTRUCT-074 | typeid | refused | local 'type' has type 'const std::type_info', which is not modeled |
| CONSTRUCT-075 | new expression | refused | local 'cell' has type 'unsigned int *', which is not modeled |
| CONSTRUCT-076 | delete expression | refused | only if/else, while and for loops, blocks, local declarations, assignments, and  |
| CONSTRUCT-077 | placement construction | refused | local 'storage' of type 'unsigned char[N]' is not initialized by an aggregate in |
| CONSTRUCT-078 | lambda expression | refused | local 'same' has type '(lambda at  |
| CONSTRUCT-079 | fold expression | refused | error [elaboration]: the postcondition of verified function 'probe' was not resolved |
| CONSTRUCT-080 | requires expression | verified | -- |
| CONSTRUCT-081 | pack expansion | refused | error [elaboration]: the postcondition of verified function 'probe' was not resolved |
| CONSTRUCT-082 | co_await | refused | error [verification-interface]: verified function 'probe' is declared but not defined in this translation unit |
| CONSTRUCT-083 | co_yield | refused | error [verification-interface]: verified function 'probe' is declared but not defined in this translation unit |
| CONSTRUCT-084 | co_return | refused | error [verification-interface]: verified function 'probe' is declared but not defined in this translation unit |
| CONSTRUCT-085 | expression statement | verified | -- |
| CONSTRUCT-086 | null statement | verified | -- |
| CONSTRUCT-087 | compound statement | verified | -- |
| CONSTRUCT-088 | declaration statement | verified | -- |
| CONSTRUCT-089 | if statement | verified | -- |
| CONSTRUCT-090 | if constexpr | verified | -- |
| CONSTRUCT-091 | switch statement | verified | -- |
| CONSTRUCT-092 | while statement | verified | -- |
| CONSTRUCT-093 | classic for statement | verified | -- |
| CONSTRUCT-094 | range-for statement | refused | range-based for loops are not modeled |
| CONSTRUCT-095 | do-while statement | verified | -- |
| CONSTRUCT-096 | break statement | verified | -- |
| CONSTRUCT-097 | continue statement | verified | -- |
| CONSTRUCT-098 | return statement | verified | -- |
| CONSTRUCT-099 | goto statement | refused | only if/else, while and for loops, blocks, local declarations, assignments, and  |
| CONSTRUCT-100 | label statement | verified | -- |
| CONSTRUCT-101 | try block | refused | only if/else, while and for loops, blocks, local declarations, assignments, and  |
| CONSTRUCT-102 | catch handler | refused | only if/else, while and for loops, blocks, local declarations, assignments, and  |
| CONSTRUCT-103 | throw expression | refused | only if/else, while and for loops, blocks, local declarations, assignments, and  |
| CONSTRUCT-104 | inline asm | refused | only if/else, while and for loops, blocks, local declarations, assignments, and  |
| CONSTRUCT-105 | local variable declaration | verified | -- |
| CONSTRUCT-106 | static local declaration | refused | local 'calls' does not have automatic storage |
| CONSTRUCT-107 | thread_local declaration | refused | thread-local 'calls' is not modeled |
| CONSTRUCT-108 | global variable declaration | refused | 'configured' is not a parameter or local of the enclosing declaration |
| CONSTRUCT-109 | reference declaration | verified | -- |
| CONSTRUCT-110 | pointer variable declaration | refused | local 'none' has type 'const unsigned int *', which is not modeled |
| CONSTRUCT-111 | array declaration | verified | -- |
| CONSTRUCT-112 | structured binding | refused | only variable declarations are modeled inside a verified body; found 'UnexposedD |
| CONSTRUCT-113 | namespace declaration | verified | -- |
| CONSTRUCT-114 | namespace alias | verified | -- |
| CONSTRUCT-115 | using declaration | verified | -- |
| CONSTRUCT-116 | using directive | verified | -- |
| CONSTRUCT-117 | typedef declaration | verified | -- |
| CONSTRUCT-118 | using type alias | verified | -- |
| CONSTRUCT-119 | enum declaration | verified | -- |
| CONSTRUCT-120 | class declaration | verified | -- |
| CONSTRUCT-121 | union declaration | refused | error [unsupported-semantics]: verified member function 'Word::get' is not verified by this implementation: it |
| CONSTRUCT-122 | bit-field declaration | refused | bit-field 'ready' is not modeled: its values and its promotion follow its width, |
| CONSTRUCT-123 | function declaration | verified | -- |
| CONSTRUCT-124 | function definition | verified | -- |
| CONSTRUCT-125 | defaulted function | verified | -- |
| CONSTRUCT-126 | deleted function | verified | -- |
| CONSTRUCT-127 | friend declaration | verified | -- |
| CONSTRUCT-128 | static_assert | verified | -- |
| CONSTRUCT-129 | attribute specifier | verified | -- |
| CONSTRUCT-130 | alignas specifier | verified | -- |
| CONSTRUCT-131 | extern linkage declaration | verified | -- |
| CONSTRUCT-132 | template declaration | verified | -- |
| CONSTRUCT-133 | template specialization | verified | -- |
| CONSTRUCT-134 | explicit instantiation | verified | -- |
| CONSTRUCT-135 | concept declaration | verified | -- |
| CONSTRUCT-136 | deduction guide | verified | -- |
| CONSTRUCT-137 | module declaration | refused | error: unknown type name 'verified' |
| CONSTRUCT-138 | module import | refused | fatal error: module 'probe_module' not found |
| CONSTRUCT-139 | module export | refused | error [unsupported-semantics]: 'verified' is applied outside namespace scope |
| CONSTRUCT-140 | constructor | refused | error [unsupported-semantics]: a verified constructor is not verified by this implementation |
| CONSTRUCT-141 | destructor | refused | error [unsupported-semantics]: a verified destructor is not verified by this implementation |
| CONSTRUCT-142 | copy constructor | verified | -- |
| CONSTRUCT-143 | move constructor | verified | -- |
| CONSTRUCT-144 | copy assignment | refused | call does not resolve to an ordinary function or to a member function named on i |
| CONSTRUCT-145 | move assignment | refused | call does not resolve to an ordinary function or to a member function named on i |
| CONSTRUCT-146 | virtual function | refused | error [unsupported-semantics]: a verified virtual function is not verified by this implementation |
| CONSTRUCT-147 | pure virtual function | refused | error [unsupported-semantics]: a verified virtual function is not verified by this implementation |
| CONSTRUCT-148 | base class conversion | refused | local 'base' has type 'const Base', which is not modeled |
| CONSTRUCT-149 | downcast | refused | local 'derived' has type 'const Derived', which is not modeled |
| CONSTRUCT-150 | multiple inheritance | refused | error [unsupported-semantics]: verified member function 'Both::get_left' is not verified by this implementatio |
| CONSTRUCT-151 | virtual inheritance | refused | error [unsupported-semantics]: verified member function 'Middle::get_root' is not verified by this implementat |

## Arithmetic

What V1 verifies is RFC 0006 and RFC 0019 as implemented: the operators above,
in the type C++ computes them in, each owing its C++ definedness condition on
the path that evaluates it (ARITH-003 to ARITH-013).

What it refuses, and why that conforms:

| Construct                                   | In a verified body | Rule it conforms to |
| ------------------------------------------- | ------------------ | ------------------- |
| `<<`, `>>`, `<<=`, `>>=`                    | refused            | ARITH-005 asks shift definedness where shifts are verified; none is |
| `&`, `\|`, `^`, `~` and their assignments   | refused            | none requires them; a refusal claims nothing |
| conversions to or from `bool`               | refused            | ARITH-008: never modeled as integral |
| conversions to or from an enumeration, except a scoped enumeration cast to its exact underlying type | refused | ARITH-008 |
| conversions to or from floating point       | refused            | ARITH-008, and `SPEC.md` 30 |
| `char8_t`, `char16_t`, `char32_t`, `wchar_t` | refused where named | their promotion is not modeled (ARITH-008 forbids deriving it anew) |
| bit-fields                                  | refused where named | their promotion is not modeled |
| compound assignment and increment of a promoted type | refused   | ARITH-013 requires the refusal |

Shifts and bitwise operators are not in V1 because verifying them needs either
bit-level reasoning or new kernel primitives with their own normalization and
linear-arithmetic translation. Either enlarges the trusted computing base
(`TRUST.md` TCB-CORE-001), and V1 does not need them to demonstrate its workflow.
They remain ordinary C++ everywhere else. A later RFC adds them, with the
kernel change and its mutation-tested rules.

## Induction

`SPEC.md` 21 defines two principles: over `@N` (21.1) and over an unsigned
machine integer type (INDUCT-002). V1 implements the second, as the kernel's
fifteenth rule, with the range premise `pred < max(T)` in the successor arm so
that no step relies on wraparound (INDUCT-003). Every other subject is refused:

- a signed integer, by 21.2, which makes such an induction ill-formed;
- an enumeration, a refinement, a pointer, a record or a character type, by
  INDUCT-004, since the specification defines no principle for them;
- `@N`, because V1 does not implement the proof-only domains of `SPEC.md` 19.1
  at all ("Big integer proof domain" is `SPECIFIED` in `STATUS.md`).

Structural induction over pointer structures, recursive objects or `@` domains
is not in V1.

## Dependent and indexed relationships

`SPEC.md` 18 provides dependent meaning through indexed refinements and formal
propositions (DEP-001, DEP-003). V1 provides:

- indexed refinements, `type R(T n) = B where (P(n))`, applied at a C++
  constant template argument, whose index is stable by construction (DEP-002);
- contracts and propositions whose meaning depends on parameters, including
  universally quantified and implicational ones (the dependent-function reading
  of DEP-003);
- indexed observation, `Element(subject, index)`, with the bound a separate
  obligation (RFC 0016).

The kernel implements no type families ("Dependent application of type
families" is `NOT STARTED`): an indexed refinement reaches the kernel already
applied, as the predicate of its instance. Proof-only indices over `@` domains
(DEP-005) come with the domains, after V1.

## Runtime semantics

None. The manifest and its fixtures change nothing the compiler emits.

## C++ interoperability

A refused construct is refused only inside a verified body, or wherever the row
says it is refused; the same construct in ordinary C++ is compiled by Clang as
it always was.

## Safety

The matrix is the safety argument's inventory: every construct is either
modeled, with a refused twin that fails if the model is dropped, or refused,
with a fixture that fails if the refusal is dropped.

## Trust impact

None. No construct becomes trusted, and no rule is added to the kernel.

## Erasure

Unchanged. The verified fixtures run, which exercises their erased form.

## Diagnostics

Each refused row pins its diagnostic, and each verified row's twin is refused
with the proof failure its one change causes. Most refusals are in the category
`unsupported-semantics` and name the construct as the bridge sees it: the
operator, the local's type, the statement kind. Four are weaker than they
should be, and are recorded as they are so that improving one is a visible
change:

- a coroutine (`co_await`, `co_yield`, `co_return`) is reported as a verified
  function declared but not defined, because its body is not read as one;
- a fold expression or a pack expansion in a verified template is reported as a
  postcondition that was not resolved;
- `extern "C"` around a verified function, and `export` in a module, are
  reported as `verified` applied outside namespace scope;
- a module declaration or import fails in Clang, before C++L sees the unit,
  because the driver does not build module interfaces.

None of them is ever accepted.

## Alternatives considered

- **A list in `STATUS.md` only.** Prose drifts from the implementation, and no
  test fails when it does.
- **Verifying shifts and bitwise operators before V1.** It would add kernel
  primitives and rules for constructs V1 does not need to show its workflow.
- **Accepting unmodeled constructs whose values no contract mentions.** A value
  a contract does not mention can still decide a path, a store or a call, so
  "unmentioned" is not "irrelevant"; `STATUS.md` forbids the silent acceptance.

## Drawbacks

Each Annex X construct needs a fixture and each verified one a twin. That is the
point: the cost of claiming a construct is showing it.

## Testing strategy

`tests/e2e/safety_subset.sh` checks, on every run, that the manifest classifies
each Annex X construct exactly once, that every fixture belongs to a row, that
each verified fixture is proven with nothing unresolved, links and runs, and
that each refused fixture is refused with its diagnostic and writes no object.

## Compatibility

None: a program the compiler accepted before is accepted, and one it refused is
refused.

## Unresolved questions

- Which of the refused constructs V2 verifies first. Candidates by demand are
  shifts and bitwise operators, `switch`, and range-based `for` over arrays.
