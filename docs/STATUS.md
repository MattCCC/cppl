# C++L Status

**Project status:** Early implementation; the V1 release gates are not all met (see [V1 closure](#v1-closure))  
**Stability:** Experimental  
**Production ready:** No  
**Language specification frozen:** No  
**Proof system frozen:** No  
**ABI guarantees:** No

C++L is currently being designed as a source-compatible C++ superset with first-class Laws, machine-checked proofs, dependent/refinement types, proof erasure, and ordinary native C++ output through Clang/LLVM.

This document exists to distinguish:

```text
what C++L intends to provide
```

from:

```text
what the current implementation actually provides
```

The README, specification, design documents, examples, and roadmap describe the intended language unless this file explicitly marks a capability as implemented.

---

# Status meanings

C++L uses the following status categories.

| Status        | Meaning                                                                                                                  |
| ------------- | ------------------------------------------------------------------------------------------------------------------------ |
| `SPECIFIED`   | Semantics or architecture have been documented, but no conforming implementation exists yet.                             |
| `PROTOTYPE`   | Experimental implementation exists, but semantics or implementation may change substantially.                            |
| `PARTIAL`     | Implementation exists, but important required behavior is still missing.                                                 |
| `IMPLEMENTED` | Intended behavior is implemented and covered by relevant tests.                                                          |
| `VERIFIED`    | Implementation exists and additionally satisfies the project's required formal or equivalence checks for that component. |
| `BLOCKED`     | Work cannot safely proceed until another semantic or architectural issue is resolved.                                    |
| `NOT STARTED` | No implementation work exists yet.                                                                                       |

`IMPLEMENTED` does not automatically mean production-stable.

`VERIFIED` must not be used casually. It means the relevant verification requirement has itself been satisfied.

---

# What the current implementation does

The pipeline works end to end. Concretely:

```text
cppl -std=c++17|c++20|c++23 main.cpp -o main
```

compiles ordinary supported C++ with no source changes, and for a unit that
declares Laws it:

1. preprocesses with Clang and recognizes `law`, `proof`, `pure`, and `verified`
   contextually;
2. projects the unit into an analysis text and a runtime text in one pass;
3. resolves the C++ semantics of the analysis text through libclang;
4. elaborates the resolved semantics into typed VIR;
5. lowers VIR into core definitions and a universally quantified goal - an
   equality, or an implication from the Law's precondition to it - and lowers
   each written proof into a kernel proof term; verified functions generate
   postcondition obligations by substituting their elaborated return term, plus
   precondition obligations for verified calls and separate obligations for each
   return path through supported `if` statements;
6. submits the author's evidence, or its own when none was written, to the
   trusted kernel;
7. reports `PROVEN` only on kernel acceptance, and fails the build otherwise;
8. checks that erasure blanked all proof-only text and nothing else, and lowered
   each runtime-bearing declaration to exactly the C++ it means, and hands the
   text it checked to Clang as the runtime program.

The verified fragment is deliberately small: a Law states a modeled proposition
over built-in integer expressions, optionally under one `expects`
precondition, universally quantified over its parameters, over
functions declared `pure` whose bodies are a single `return` of a modeled
expression. A proof declaration claims a Law at arguments of its choosing or states a modeled
proposition directly, including explicit `Eq<T>(a, b)`,
and its body is a sequence of `refl`, `exact`, `apply`, `assume` and `rewrite`
statements;
`exact` and `apply` may instantiate the evidence they name at terms, as in
`exact q(41u);`. A proof discharges the Law itself when what it claims is the
Law's own proposition; otherwise it proves one instance, which other proofs may
use. Direct propositions have their own written-proof obligations and are counted
separately from Laws. Explicit `Eq<T>` currently supports modeled integer and
Boolean types as a complete Law, proof, precondition, postcondition, or `assume`
proposition. Clang resolves its type and arguments; an unmodeled conversion is
refused.
`exact` and `apply` on equality goals can bridge definitionally equal operands
using explicit equality-substitution and reflexivity evidence.

A proposition may also state explicit universal quantification,
`forall (T x, ...) { P }`, and implication, `P -> Q`, in any of those same
places. The binders are ordinary C++ parameters Clang resolves, and they name the
innermost variables: a binder that shadows a parameter denotes the binder. A
statement written under such a binder means what it would anywhere else, so
`assume` and `rewrite` reach a goal however deeply it quantifies. A binder is not
a name a statement can use, so evidence that stays quantified cannot be
instantiated at one. `forall` and `exists` are formal only in that complete form,
and an `->` outside all brackets is implication, so a program that spells its own
`forall` or dereferences inside an expression keeps its own meaning. Existential
quantification, quantifiers in loop invariants, and formal forms nested inside an
ordinary C++ expression are refused. See `SPEC.md` 8, 8.1-8.2 and 9. Both forms
lower onto the quantifier and implication the kernel already had, and added no
kernel rule.
A binder of a refinement type, and a Law's or proof's parameter of one, ranges
over that refinement's values only (FORALL-001): `forall (Small s) { P }` is the
kernel's `forall u32. s < 10 -> P`, each refined binder's membership stated after
its group of binders, and an unrefined binder states none. Instantiating such
evidence at a term therefore leaves the term's membership to be proven before
the conclusion is used (REFINE-003): `apply h(n);` leaves it as the next goal,
and `exact h(n)` or `contradiction h(10u)` at a term not shown to be a member is
refused. The predicate is stated by the membership a refined function parameter
already uses, so a refinement of a refinement and an indexed refinement range
over every predicate that applies. `Eq<R>(a, b)` at a refinement type, or at a
type with a refined component, is refused: its operands are not shown to be
values of `R`. A claim in a verified body, `contradiction p(x);`, naming a proof
over a refined parameter is refused, because nothing there establishes the
membership the instantiation owes: a completeness limit, which fails closed.
Until this was so, a binder ranged over the whole base type, so a quantified
premise was stronger than written and a trusted law over a refined parameter
was assumed of every base value; both proved false claims (`TRUST.md` 36.3).
Evidence: `e2e_refined_quantifiers` and its refused twin,
`negative_quantified_propositions`, `unit_quantified_propositions_test`.
Conjunction of supported Boolean predicates is now `PROTOTYPE`: nested `&&`
works in Laws, direct proofs, preconditions, postconditions and `assume`, under
quantifiers and implications. Introduction proves both sides; elimination exposes
either side of a checked premise. Written `refl`, `exact`, `apply`, and `rewrite`
compose with conjunctive goals, and arithmetic automation uses explicit evidence
for conjunctive facts. This adds two kernel rules (core/kernel 0.4.0), no
assumptions or axioms. Formal propositions now also compose as `&&` operands;
logical equivalence `<->` lowers to both implications, using those same rules.
Disjunction is `PROTOTYPE` on two further kernel rules (core/kernel 0.5.0), again
with no assumptions or axioms: `||` is introduced from one side and used by a
case analysis over both, automation shapes both and the kernel checks them, and
nothing grants `P || not P`. In a verified `if` or loop condition, `&&`, `||`
and `!` are elaborated into the routes they select between (SPEC.md 12.7). A loop
invariant states `&&` and `||` as a contract does, the conjunction and the
disjunction of its operands, each specified on its own and nested to any depth
(EXPR-016, SPECEXPR-002; `e2e/conditions.sh`, `negative/conditions.sh`).
Automation takes cases on a disjunction inside a conjunctive premise, such as
a callee's `r <= a && r <= b && (r == a || r == b)`; on the order a disjunctive
goal's first side states, machine order being total, so `b0 > 0 || (a == a0 &&
b == 0)` holds where `b0 <= 0` gives the rest, while `x == 0 || x != 0` is
still not built; on a selection in a disjunctive goal; and, where nothing else
closes a goal, on a selection a premise states something about, such as the
route fact of a Boolean local holding `c ? true : d`. Where nothing else closes
a goal, it also uses an implication in scope whose premise it proves rather
than finds as stated, such as a callee's `(s == 0 && e != button) -> r == 0`
called with `e` the timeout. Such a local read in a
later condition is split where that condition stands, as the composed
contract nests it. Each case is a kernel rule checked on its own; none adds
an assumption. A value a body computes with `&&` or `||`, in a declaration, an
assignment or a call's argument, is the `?:` C++ evaluates, `a ? b : false` and
`a ? true : b`, so an operation in the second operand owes its conditions only
where it is evaluated (VERIFIED-021, EXPR-015); a returned `?:`, `&&` or `||`
is a return of its own on each route its condition selects. In a pure
function's definition and in a contract's term, such as `result == (a && b)`
or `(a || b) ? 1 : 0`, they are the same conditional values, each operand
owing its conditions without the other's guard, so a definition dividing under
a first operand that guards the divisor is refused rather than admitted.
Everything else is reported as unsupported and produces no obligation. See
`docs/ARCHITECTURE.md` 95 for the implemented structure and `TRUST.md` 4 for what
must be trusted.

A precondition is supposed, never granted: `expects (P) ensures (Q)` states
`P -> Q`, and the premise reaches a proof only through implication
introduction. `assume` names that premise and is an error where the goal
supposes none. `rewrite` then uses an equality to transform the goal, so a
conditional Law whose conclusion needs its premise to be _used_ is provable.

Verified functions support pure return expressions, `if`/`else`, nested blocks,
integer parameters and results, one `ensures` comparison, and any number of
`expects` comparisons, which conjoin (`SPEC.md` 11.5): the body supposes each in
turn, and a verified caller proves each separately. Comparisons are `==`, `!=`,
`<`, `<=`, `>`, `>=`, with logical negation. The generated single-return
goal is `forall parameters. P1 -> ... -> Pn -> Q[R/result]`. A Law still
accepts at most one `expects` clause. Automatic evidence first tries
definitional equality. If needed, it introduces binders, uses an identical
hypothesis or rewrites once per available equality in either direction, newest first, then offers
reflexivity; the kernel
checks every step. Written `refl` retains its
definitional-equality semantics. `result` is erased specification syntax.
`old(e)` in a postcondition, the entry value of `e` (`SPEC.md` 11.4), is
`SPECIFIED` and refused by name wherever the postcondition writes it, whatever
C++ entity named `old` is visible: the formal form takes precedence there
(WORD-001, WORD-007), so it is never read as a call. Only `::old(...)` or
`x.old(...)` names something else. Read as a call to a visible `pure` function
`old`, a contract stating that a body leaves its reference parameter unchanged
was PROVEN while the body changed it (`TRUST.md` 36.3). Outside a postcondition
`old` stays an ordinary name: in code that runs, in a precondition and in a Law
(`conformance_contextual_identifiers`, `negative_erasure`).
Verified calls instantiate their callee's contract at the resolved arguments.
Each precondition must be kernel-proven before its postcondition is available.
Caller reasoning uses abstract call results and proven summaries; kernel-checked
evidence connects that reasoning to the executable return term. Nested calls,
overloads, and forward declarations are supported within a translation unit,
including headers; recursion is verified only with a measure, as described
below. Ordinary runtime calls remain unchanged. See `SPEC.md`
12.5–12.8. Each return path additionally supposes its branch conditions. Calls in
guards prove their preconditions before that guard can be used. The kernel
combines the checked paths into the complete function theorem.

Unsigned `+`, `-` and `*` are the ring of integers modulo `2^width`: every
commutative-ring identity is definitional, comparisons are normalized only as
the machine type allows, and order consequences (`i < n` gives `i + 1 <= n`)
are proven by a linear-arithmetic rule whose certificates the kernel checks
against a constraint system it states itself, wrapping included. When
definitional equality and premise rewriting do not close a goal, automation
tries linear arithmetic over the premises, then rewriting with premise
equalities and with equalities between variables that arithmetic establishes.
A contradictory path is proven from its contradiction. See `SPEC.md` 7.1.1, 7.5
and 29.2.

Signed `+`, `-`, `*` and unary `-`, `/` and `%`, and integral conversions are
verified with their exact C++ semantics (`SPEC.md` 29, RFC 0019). Each
operation C++ defines only under a condition owes it on every path that
evaluates the operation: the exact result is representable, the divisor is not
zero, the operands are not the least value and `-1`, a value converted to a
signed type fits it (in every C++ mode). The obligation supposes the path's
guards, the `?:`, `&&` and `||` outcomes that select the operation, and only
the postconditions of calls C++ sequences before it, never the operation's own
result; afterwards the path supposes what was proven. Promotions and the usual
arithmetic conversions are the ones Clang recorded, and each operation is
performed in the common type they select, so two `unsigned char` values add as
a signed `int`; explicit integral casts are the conversions they name.
`char8_t`, `char16_t`, `char32_t`, `wchar_t` and bit-fields are refused. A specification's C++ condition holds only where its operations are
defined, and a `pure` function, a total definition, refuses them. Quotient and
remainder by a constant are exact in linear arithmetic; by an unknown divisor
the remainder is bounded and the quotient is not. A product of two unknowns is
decided only where the types the operands were widened from bound it: two values
promoted from 8- or 16-bit types, except two `unsigned short` values, or two
`int` values each converted to `long long` before they are multiplied
(`fixtures/integration/ledger.cpp`, `Ledger::line_amount`); refinements and
preconditions do not bound it. Shifts, bitwise operators and conversions to or
from `bool` are rejected.

A body may also declare locals and assign to them. Each write is a logical
version of the declaration Clang resolved, a read denotes the version current
where it stands, and what follows a branch is verified once per arm under that
arm's versions. A call bound to a local is proven where the body makes it, under
the conditions in force there, on every path that reaches it. `+=`, `-=`, `*=`,
`/=`, `%=`, increment and decrement are the assignments they abbreviate, for
locals not promoted before arithmetic. Uninitialized, `static`, `thread_local`,
reference, pointer and `volatile` declarations, other compound assignments,
assignment to a parameter, self-initialization, and initializer conversions
other than integral ones are rejected. Every value is modeled where it is written, read or not. Each read repeats its
local's value, so bodies whose stated terms exceed a fixed size are rejected
too. Locals add no kernel rule and no runtime change.

`while`, `do`/`while` and `for` loops with a block body, a `for` without a
condition included, may state `invariant (...)` and `decreases (...)` clauses
(`SPEC.md` 24). Each local the loop writes is carried: at the head it is a
fresh value of which only the invariants and the condition are known. Every
invariant is proven on entry, before the body first runs for a `do` loop, and
at the end of every iteration path, including `continue` and the `for` step;
what follows the loop, and any `break`, is verified from what those paths
suppose. Calls in the condition and body prove their preconditions where the
loop makes them. A measure is an unsigned value or a lexicographic list of
them, and every path that continues to another iteration owes a strictly
smaller one, compared component by component in the machine's non-wrapping
order (`SPEC.md` 22.3, 22.5, LOOP-006).

A range-based `for` is verified over a `std::vector` or `std::string` the body
names, a `std::span` local or by-value parameter, an array local or `std::array`
local, and a built-in array a parameter designates, when its elements are
integers, enumerations or Booleans (`SPEC.md` LOOP-001, LOOP-004, STMT-005,
STDMODEL-019, `e2e_range_for`, `negative_range_for`, `range_for` in
`e2e_erasure_equivalence`). It is the one loop lowering above, iterated by a
position the body names nowhere: 0 before the loop, compared with the range's
length at every head, one more at each iteration's end. Each iteration forms
the element at the position as a subscript would, owing its bound and, over a
span parameter, its capability; a loop variable by value is initialized from
it, owing a refinement its type names, and one by reference or `const`
reference is bound to the element place, so a write through it is a write to
that element, owing the element type's refinement. Invariants hold at the head,
before the loop variable is initialized, and one naming it is refused. Without
a written `decreases`, the loop is given the measure of the positions left,
`length - position`, which the kernel checks as any loop measure, so such a
loop is total when that descent is proven; a written measure is checked in its
place. The position is named by no clause, so an invariant cannot speak of how
far the loop has come: a claim about a prefix needs an index loop. An iteration
that may have replaced the range's storage, by a mutator, a call handed it by
mutable reference or an unsafe block reaching it, and goes on to another
iteration is refused naming the operation, since C++ leaves that undefined; one
that leaves by `break` or `return` is verified. A range that is not a name, a
range of any other type (an iterator pair, a user class with `begin`/`end`, a
temporary), an initialization statement before the loop variable, a structured
binding, and an element that is not a scalar are refused by name.

A `switch` is verified with C++'s semantics (C++ [stmt.switch]). Its condition,
a call in it with its effects included, is evaluated once and bound to one
value, which each case value Clang evaluated is compared with in the order the
labels appear; the first that equals it, or else `default:`, or else what
follows the switch, is where control enters the body, which then runs to its
end through every later label, so fall-through is a path like any other. A
`break` leaves the innermost loop or switch enclosing it, and a `continue` in a
switch continues the loop around it. A condition variable is a local the
condition initializes, and `[[fallthrough]];` is an empty statement. Refused by
name: a `case` or `default` label anywhere but directly in its switch's body
(Duff's device), a statement before the first label, a GNU case range, and an
init-statement, which libclang does not expose as a part of the switch at all
(not as a child, nor at its position) and which would otherwise be dropped. A
comma operator as a statement, `a = 1, b = 2;`, or as a `for` increment,
`++i, --j`, runs its operands in order as statements of their own; one inside
another expression is refused by name (`e2e/switch_statements.sh`,
`negative/switch_statements.sh`).

An `if` may have an init-statement and a condition variable (C++ [stmt.if]):
the init-statement runs first, in a scope around the whole statement, so what
it declares is visible in the condition and both branches; a condition
variable is a local the condition reads. libclang lists the init-statement
where the condition otherwise stands, so which part is which is read from the
tokens of the head: the parts written inside its parentheses, split at the `;`
that ends the init-statement, and the branches after them. `if constexpr` runs
only the branch its constant condition selects, as Clang evaluates it, which in
a template specialization is the only one instantiated. `if consteval` is
refused by name: which branch runs depends on whether the evaluation is a
constant one (`e2e/if_statements.sh`, `negative/if_statements.sh`).

A verified function may state `decreases (...)` over its parameters, one
measure or a lexicographic list, to ask that it terminate (TERMINATION-004).
Recursion is verified only that way: functions that call each other, directly
or through one another, form a recursion group; every member states a measure
of one length, every call within the group owes a callee measure strictly
smaller than the caller's, and each member's conditions suppose the others'
contracts as the induction hypothesis those descents justify. The group is
established only when every member's conditions and descents are proven, and a
recursive function without a measure is refused (TERMINATION-007). A function
template's measure is refused, since nothing yet forces it at each
specialization.

A contract is **total** when every loop its paths enter states a measure, it
passes through no unsafe block, and every contract it calls is total, its
recursion group's included; otherwise it is **partial correctness**: what
holds if the function returns (`SPEC.md` 23). The trust report counts both,
names each partial-correctness contract, and counts loop measures and recursive
call measures proven apart from invariants. The compiler also warns at each
partial-correctness contract it proves, on the command line and in the editor,
naming the function and every reason its termination is not established: each
loop that states no `decreases`, with where it stands, each unsafe block, and
each callee whose own contract is partial. The warning, category
`partial-correctness`, reads the same totality decision the report counts and
names exactly the contracts the report lists; it changes no verdict, report or
exit status, `-Werror` does not make it an error, as it makes none of C++L's
warnings one, and `-w` silences it (`e2e/partial_correctness.sh`). A function
that states `decreases` and is not total is refused, for the first loop, unsafe
block or callee that stops it (TERMINATION-006), and is not warned about too. A function with a loop, or calling one, is never
admitted as a core definition, and neither is a recursive one, so no Law or
specification can unfold it and nontermination cannot reach the kernel. Measures
erase with the rest of the contract; no counter or runtime check is added
(`fixtures/equivalence/termination.cpp`). Loops and recursion add no kernel
rule; the loop and recursion rules are correspondence trust (`TRUST.md` 12.1).

Refinement types are `PROTOTYPE`: `type R = T where (P);` and its indexed form
declare a verification-level type over an ordinary C++ base type, lower to the
alias the program keeps, and make membership an obligation at every site a value
enters the type. Refined parameters supply their predicate to the body and refined
results are proven on every return. The boundary is stated under
[Refinement status](#refinement-status).

`contradiction e;` is `PROTOTYPE`: a proof statement that closes the goal from
evidence that the context where it is written cannot occur (`GRAMMAR.md` 5.6,
`SPEC.md` `CASE-011`, `CASE-013`). The named evidence and every premise standing
there are first refuted into `False` by linear arithmetic, which the goal takes
no part in, and only then is the goal closed from that by falsity elimination,
so a goal that merely follows from the premises establishes nothing. The kernel
checks the certificate against constraints it states itself. Certificates are
found by the same bounded refutation search automation uses
(`compiler/refutation`), which is untrusted: when it finds nothing, the claim is
unproven, never an impossibility (`CASE-015`). The goal may have any shape,
an equality of structured values such as two records included, because falsity
elimination looks only at the evidence for `False`. Automation closes a goal
from contradictory premises the same way, so a path whose premises cannot hold
is proven whatever its goal equates. No axiom or trusted mechanism is added.

Omitting a case is `PROTOTYPE`: `omit label by contradiction e;` inside a
`cases` statement accounts for a case without an arm (`GRAMMAR.md` 5.7,
`SPEC.md` `CASE-004`) and is the only way a case goes uncovered. An absent arm
with no omission stays non-exhaustive, and the engine never searches the
surrounding context to decide that a missing arm was meant (`CASE-005`), so an
accidental omission and a proved impossibility stay distinguishable. The
evidence is checked under the omitted case's own discriminator premise, residual
cases included. Each omission is an obligation of its own (`CASE-012`,
`CASE-016`): origin `OmittedCase`, an identity that includes that origin, a goal
stated apart from the proof it is written in, and evidence the kernel checks
against that goal. The trust report counts them as `Omitted cases proven`, apart
from the laws they occur in.

Claiming a runtime path impossible is `PROTOTYPE`: `contradiction e;` written as
a statement of a verified body claims that no execution reaches it (`GRAMMAR.md`
5.6, `SPEC.md` `VERIFIED-023`, `VERIFIED-045`). The claim is checked where it
stands: the named proof, instantiated at arguments read at the versions current
there, and every fact of the path - preconditions, branch conditions, loop
invariants, callee postconditions - are refuted into `False` by the same
mechanism a proof uses. The path ends at the claim, so what follows it owes
nothing. Each claim is an obligation of its own (`CASE-012`, `CASE-016`): origin
`ImpossiblePath`, an identity that includes that origin, the path's facts closed
over `False` as its goal, and evidence built only once the proof it names has
been admitted. It is never proven any other way, and one resting on a callee's
postcondition waits for that callee to be proven. The trust report counts them
as `Impossible paths proven`. A function with a claim has partial-correctness
conditions, since a path ending in one returns no value. C++ comes first
(`WORD-011`): where the translation unit gives `contradiction` any other
meaning, the statement stays ordinary C++ and a warning says so. In a function
that is not verified it is refused. The statement erases to an empty statement,
so an unbraced `if` whose body it was keeps one (`ERASE-016`). The evidence is a
proof declaration; a law without a written proof cannot be named, exactly as in
a proof body, and neither can a trusted law, which a proof body may name
(`TRUSTED-006`). A claim naming a proof that rests on a trusted law rests on it
too, and so do its function's contract and every caller's.

Proof-side `cases` is `IMPLEMENTED` as a representation-independent engine:
subject analysis, arm matching, binders and scope, nesting, exhaustiveness,
evidence construction, dependency checking, diagnostics and erasure are shared
by every representation and produce evidence for existing kernel rules. What
states a value has comes from a decomposition provider for its resolved C++
type.

Six representation families are `IMPLEMENTED`:

| Representation      | States                                                                                       |
| ------------------- | -------------------------------------------------------------------------------------------- |
| enumerations        | one case per distinct enumerator value, residual `unnamed`                                   |
| `std::variant`      | `alternative<i>` per index, residual `valueless`                                             |
| `std::optional`     | `some(value)`, residual `none`                                                               |
| `std::expected`     | `value(payload)`, residual `error(reason)`                                                   |
| pointers            | `null`, residual `non_null`                                                                  |
| products            | one `components(...)` arm: records, `std::pair`, `std::tuple`, `std::array`, built-in arrays |

Tagged sums share one mechanism and products share another, so these are two
provider implementations rather than six. Nesting composes generically in both
directions. `std::expected` is gated on the C++23 library. Representations with
no provider are still refused at the provider boundary by name, and arm syntax
does not make a class a sum. A statement is read with at most 64 arms, omissions
included, and arms nest at most 32 deep. See `SPEC.md` 20.1 and 20.4 for the
boundary, and `TRUST.md` 19 for what each provider does and does not state.

`cases` and `decompose` are also `IMPLEMENTED` as statements of a verified
function's body (`SPEC.md` 20.7, `CASE-017` to `CASE-020`), over values that can
change. The subject is read at the versions current where the split is written;
each arm continues the path with its case's discriminator as a fact, and the rest
of the body is verified once per arm, exactly as the proof-side split supposes
each case. An arm holds only nested splits and a `contradiction` claim that ends
its path; an omitted case is an omitted-case obligation checked against the
path's facts. A case fact is about one version: after a write, a write through a
reference that may alias it, a call that may change it, or at the head of a loop
that writes it, the storage has a new version that no earlier fact describes.
Invalidation is the storage model's own, not a mechanism of the case engine. The
path walk re-checks that every state has exactly one arm, so no state's path can
be dropped. A split erases to an empty statement.

What a split can reach is what verified bodies model. A local aggregate is
tracked member by member and has no single value, so it is split through its
members. Verified code cannot write a `std::optional`, `std::variant` or
`std::expected` or reassign a pointer local, so splits over those read values
that do not change within the body. A split's binders are declared once for the
statement as written, so in a function template whose specializations bind
values of different types, the specializations that disagree are refused.

A verified body may declare ghost state (`SPEC.md` 25): `ghost T name = value;`,
a local of integer or Boolean type that records a value for the proof. Runtime
values may be copied into it; loop clauses, a claim's evidence and another
ghost's initializer may read it. Its initializer is read like a specification
expression, so it may have no effect and call only a `pure` function. Nothing
that runs may use it: a returned value, a branch, an index, an argument, an
initializer, a write, a loop bound or a lambda capture naming a ghost is refused
where it stands, reached or not (GHOST-002). The whole declaration then leaves
the program (ERASE-011), which compiles to the same code as the program written
without it (`fixtures/equivalence/ghost_state.cpp`). A ghost is declared only
directly in a block of a verified body, outside every unsafe block; a class,
pointer, reference, array or volatile ghost, a static one, one without a value,
a global and a member are refused by name (`negative_ghost_state`). Where the
unit uses `ghost` for anything else, a declaration led by it is ordinary C++,
with a warning only in a verified body (`WORD-018`). In any unit
with C++L syntax, a name beginning with `__cppl_`, the prefix of every
declaration C++L generates, is refused, since the body lowering reads a
declaration so named as generated.

This slice implements induction over unsigned machine integers only (see
[Case analysis and induction status](#case-analysis-and-induction-status)). It
does **not** implement induction over signed integers, structures, pointer
structures or the `@` domains, structural recursion without a measure, proof
`let`, solvers, proof caching, or a general model of C++ memory:
what exists is the storage model of places, versions and capabilities
(`SPEC.md` 12.10) and the storage generations of the standard container subset
(RFC 0020), described below. The rest remains `SPECIFIED`. `trusted law` and `unsafe` are
implemented and described under
[Unsafe and trusted boundary status](#unsafe-and-trusted-boundary-status).

---

# Current project state

Outside that slice, C++L should be considered primarily a **language and verification-system specification**.

The following documents define the intended direction:

```text
README.md
SPEC.md
DESIGN.md
FOUNDATIONS.md
TRUST.md
COMPATIBILITY.md
ROADMAP.md
SECURITY.md
ACKNOWLEDGEMENTS.md
STATUS.md
```

Unless implementation evidence says otherwise, documented features should be treated as:

```text
SPECIFIED
```

rather than:

```text
IMPLEMENTED
```

---

# Current milestone

The current milestone brings the verified fragment to the shape of parser and
accounting code built from several translation units, as four `PROTOTYPE` or
`PARTIAL` slices that compose: contracts cross translation units through
verification interfaces (RFC 0017), non-virtual member functions are verified
over their implicit object's storage (RFC 0018), signed arithmetic, division
and integral conversions carry their defined-behavior obligations (RFC 0019),
and `std::array`, `std::vector`, `std::string` and dynamic-extent `std::span`
are used through trusted library summaries with proved bounds and storage
generations (RFC 0020). `e2e_integration_ledger` verifies one program of three
units that uses all four, with every trusted law, unsafe block and library model
named across the units, compiles it to the same code as its hand erasure, and
its refused twins fail closed (`negative_integration_ledger`,
`e2e_cross_feature`, `negative_cross_feature`). The kernel gained the six
primitives of RFC 0019 and no rule or axiom (core/kernel 0.8.0). What each
slice leaves out is stated in its own section below. Memory reasoning beyond the
storage model and the container subset, and SMT automation, come later.

An earlier milestone completed the imperative foundation: verified functions
verify every return path through `if`/`else`, including
compositional calls in guards and returns, locals, assignments and their
updates, and `while`/`for` loops against explicit invariants (partial
correctness). All six integer comparisons are
represented structurally. The original function/call slices use seven rules;
path composition adds one conditional-elimination rule, and machine arithmetic
adds one linear-arithmetic rule; conjunction adds introduction and elimination,
bringing the core to eleven at that point, with zero logical
assumptions and zero runtime checks. Locals and loops add none. Unsigned arithmetic is
normalized as a ring modulo `2^width`, and order consequences are kernel-checked
(ROADMAP large slice 1). Propositions, written proofs, refinement and indexed
refinement types, termination and, over unsigned machine integers, induction
followed, as stated below.

Original target, for reference:

```text
formal core
    ↓
trusted kernel
    ↓
verification IR
    ↓
Clang semantic bridge
    ↓
C++L syntax
    ↓
proof erasure
    ↓
ordinary C++
```

The project should not claim broad language implementation before the proof semantics and trust model are sufficiently defined.

---

# Core language status

| Capability                    | Status        |
| ----------------------------- | ------------- |
| C++L language mission         | `SPECIFIED`   |
| Genuine C++ superset model    | `PROTOTYPE`   |
| `law` declarations            | `PROTOTYPE`   |
| `ensures` clauses on laws     | `PROTOTYPE`   |
| `proves` clauses              | `PROTOTYPE`   |
| proof declarations            | `PROTOTYPE`   |
| `refl` / `exact` / `apply`    | `PROTOTYPE`   |
| proof instantiation `q(t)`    | `PROTOTYPE`   |
| `expects` clauses on laws     | `PROTOTYPE`   |
| `assume`                      | `PROTOTYPE`   |
| `rewrite`                     | `PROTOTYPE`   |
| `contradiction`               | `PROTOTYPE`   |
| multi-statement proof bodies  | `PROTOTYPE`   |
| proof `let`                   | `SPECIFIED`   |
| proof case analysis `cases`   | `PROTOTYPE`   |
| case splits in verified code  | `PROTOTYPE`   |
| proposition types             | `PROTOTYPE`   |
| explicit `Eq<T>` propositions | `PROTOTYPE`   |
| direct proposition proofs     | `PROTOTYPE`   |
| universal quantification      | `PROTOTYPE`   |
| implication                   | `PROTOTYPE`   |
| conjunction                   | `PROTOTYPE`   |
| logical equivalence           | `PROTOTYPE`   |
| disjunction                   | `PROTOTYPE`   |
| existential quantification    | `SPECIFIED`   |
| dependent types               | `PROTOTYPE`   |
| refinement types              | `PROTOTYPE`   |
| algebraic data types          | `NOT PLANNED` |
| runtime pattern matching      | `NOT PLANNED` |
| impossible-state elimination  | `PROTOTYPE`   |
| impossible runtime paths      | `PROTOTYPE`   |
| definitional equality         | `PROTOTYPE`   |
| propositional equality        | `PROTOTYPE`   |
| normalization                 | `PROTOTYPE`   |
| `induction`                   | `PROTOTYPE`   |
| well-founded recursion        | `IMPLEMENTED` |
| termination checking          | `IMPLEMENTED` |
| `expects` on functions        | `PROTOTYPE`   |
| `ensures` on functions        | `PROTOTYPE`   |
| `pure`                        | `PROTOTYPE`   |
| `verified`                    | `PROTOTYPE`   |
| verified-call composition     | `PROTOTYPE`   |
| path-sensitive `if`/`else`    | `PROTOTYPE`   |
| integer comparison predicates | `PROTOTYPE`   |
| locals and assignments        | `PROTOTYPE`   |
| `invariant` on loops          | `PROTOTYPE`   |
| partial-correctness contracts | `PROTOTYPE`   |
| `ghost`                       | `IMPLEMENTED` |
| `unsafe`                      | `IMPLEMENTED` |
| `trusted`                     | `IMPLEMENTED` |
| `decreases`                   | `IMPLEMENTED` |
| proof erasure                 | `PROTOTYPE`   |

---

# Proof system status

| Capability                           | Status        |
| ------------------------------------ | ------------- |
| Core proof calculus                  | `SPECIFIED`   |
| Propositions-as-types model          | `SPECIFIED`   |
| Trusted proof kernel architecture    | `PROTOTYPE`   |
| Kernel implementation                | `PROTOTYPE`   |
| Proof-term representation            | `PROTOTYPE`   |
| Proof-term binary/serialized format  | `NOT STARTED` |
| Equality checking                    | `PROTOTYPE`   |
| Substitution                         | `PROTOTYPE`   |
| Dependent application of type families | `NOT STARTED` |
| Universal introduction               | `PROTOTYPE`   |
| Universal elimination                | `PROTOTYPE`   |
| Implication introduction             | `PROTOTYPE`   |
| Implication elimination              | `PROTOTYPE`   |
| Hypothesis context                   | `PROTOTYPE`   |
| Equality substitution (rewriting)    | `PROTOTYPE`   |
| Conditional elimination              | `PROTOTYPE`   |
| Machine-arithmetic normal form       | `PROTOTYPE`   |
| Linear arithmetic (certificates)     | `PROTOTYPE`   |
| Conjunction introduction/elimination | `PROTOTYPE`   |
| Disjunction introduction/elimination | `PROTOTYPE`   |
| Falsity elimination                  | `PROTOTYPE`   |
| Existential introduction/elimination | `NOT STARTED` |
| Induction checking                   | `PARTIAL`     |
| Dedicated refinement kernel rules    | `NOT PLANNED` |
| Normalization engine                 | `PROTOTYPE`   |
| Kernel termination checker (recursive kernel definitions) | `NOT STARTED` |
| Serialized proof certificate format  | `NOT STARTED` |
| Kernel fuzzing (in-suite, structural) | `PROTOTYPE`  |
| Kernel fuzzing (persistent fuzz target) | `PROTOTYPE` |
| Kernel property testing              | `PARTIAL`     |
| Kernel rejection tests               | `PROTOTYPE`   |
| Mechanized core calculus             | `PARTIAL`     |
| Meta-theory / soundness proofs       | `PARTIAL`     |

The kernel implements fifteen rules:

```text
1. Reflexivity
2. Equality substitution
3. Universal introduction
4. Universal elimination
5. Implication introduction
6. Implication elimination
7. Hypothesis use
8. Conditional elimination
9. Linear arithmetic
10. Conjunction introduction
11. Conjunction elimination (left or right)
12. Disjunction introduction (left or right)
13. Disjunction elimination (a case for each side)
14. Falsity elimination (any goal, from evidence for False)
15. Unsigned induction (a universal over an unsigned machine integer type,
    from P(0) and the step below the type's maximum, both stated by the kernel)
```

`KERNEL.md` states each rule with its side conditions exactly, and how far they
are mechanized. The two mechanization rows are `PARTIAL`: a Coq model of the
checking judgment (`formal/coq`, KERNEL.md 17) proves that whatever evidence it
accepts establishes a true proposition, relative to two stated premises,
normalization preserving meaning and the arithmetic translation being exact;
discharges both for models of the kernel's normalization, its arithmetic
translation and its certificate checker (`check_sound_closed`), leaving only
conditions on the interpretation and the definitions; and proves, with no
premise, that rules 2 to 8 and 10 to 15 cannot establish `False`. It rests on
no axiom. The C++ kernel is not proven to implement the model, so it stays in
the logical TCB (`TRUST.md` 41).

They act over propositions built from equality, universal quantification,
implication, conjunction, disjunction and `False`. `False` has no introduction
rule (`TRUST.md` TCB-CORE-017): evidence for it comes from a hypothesis, from an
elimination, or from linear arithmetic refuting its facts with no goal taking
part (core/kernel 0.8.0). The kernel's terms are variables,
machine-integer literals, applications of admitted definitions, observations of
an abstract value (at a constant position, or at an index that is itself a
term), and primitives: wrapping addition, subtraction and multiplication, the
six comparisons, boolean negation and selection, and those of RFC 0019:
representability of an exact sum, difference or product, truncating quotient
and remainder, and integer conversion. It admits no recursion, which
is why it needs no termination checker of its own yet (`SPEC.md` 22.2, 22.4): a
recursive verified function terminates by its measure's descent obligations,
which the kernel decides as ordinary propositions, and is never admitted as a
definition it could unfold.

Reflexivity decides definitional equality by normalization, which puts machine
arithmetic in polynomial normal form modulo `2^width` and comparisons in
canonical form (`SPEC.md` 7.1.1). Linear arithmetic concludes an equality or
comparison from facts whose evidence it checks, by checking a certificate
against the integer constraint system it states for them (`SPEC.md` 7.5). It
concludes `False` when the certificate refutes the facts alone, and falsity
elimination then closes any goal from that (`FOUNDATIONS.md` 26).
Property testing checks every acceptance against an independent finite model
of the core (`tests/support/kernel`, `tests/kernel/model_oracle_test.cpp`):
derivations built rule by rule with deliberate defects, arithmetic certificates
proposed by the untrusted refutation search, and automation's evidence, each
accepted goal evaluated at every assignment of small types and at sampled
values of wide ones; and normalization, substitution and shifting, each checked
to keep a term's value. It is `PARTIAL` because the model's carriers are finite
and wide types are sampled, so a finding is a defect while its absence proves
nothing.

Universal elimination instantiates quantified evidence at a term. The kernel
checks the evidence against the proposition it is eliminated from, derives the
argument's type itself, and obtains the resulting proposition by its own
capture-safe substitution. Several arguments are several eliminations; there is
no multi-argument rule.

Implication introduction supposes a premise and puts it in the kernel's own
hypothesis context; implication elimination discharges one against evidence for
it. A hypothesis is usable only where an enclosing introduction placed it, and
is restated for the binders it is used beneath. No rule anywhere admits a
premise on its own.

Equality substitution transports evidence through a proposition context. It is
a genuinely new capability rather than sugar over the others: without it,
evidence for `a = b` closes a goal that already is `a = b` and can do nothing
else. The kernel performs the substitution itself - the context is given to it,
its hole type-checked against the type the equality is stated at, and the
resulting proposition derived rather than accepted. Symmetry, and rewriting in
the opposite direction, are this rule at another context; neither is primitive
and neither is inferred.

Written proof declarations added no rule of their own: `refl`, `exact`,
`apply`, `assume`, `rewrite` and `contradiction`, and the arms and omissions of
`cases` and `decompose`, elaborate into terms built from these rules.

Several rows of this table name the kernel layer only, and a capability of the
same name elsewhere in this file is a different layer:

```text
row here                               layer        where the capability itself is
Dependent application of type families kernel       indexed refinements are applied by
                                                    Clang's template substitution before
                                                    the kernel sees them (Refinement
                                                    status); applying a proof of a
                                                    universal to a term is universal
                                                    elimination above
Dedicated refinement kernel rules      kernel       refinement introduction and
                                                    elimination are ordinary propositions
                                                    the kernel checks, with no rule of
                                                    their own (RFC 0012: zero kernel
                                                    rules); see Refinement status
Kernel termination checker             kernel       the kernel admits no recursive
                                                    definition, so it has none to check;
                                                    verified functions' termination is
                                                    checked by descent obligations the
                                                    kernel decides (Termination status,
                                                    IMPLEMENTED)
Serialized proof certificate format    artifact     linear-arithmetic certificates exist
                                                    in memory and are checked by the
                                                    kernel (Automation status); none is
                                                    written to or read from a file
Kernel fuzzing                         test         tests/kernel/proof_fuzz_test.cpp
                                                    fuzzes every proof constructor from
                                                    a fixed seed inside the suite; the
                                                    libFuzzer targets kernel_proof,
                                                    kernel_arithmetic, kernel_terms and
                                                    kernel_certificate (tests/fuzz) put
                                                    the same model oracle to inputs no
                                                    one chose, replayed over their
                                                    corpora in every build
```

---

# Mathematical foundation status

| Area                            | Status        |
| ------------------------------- | ------------- |
| Curry–Howard foundation         | `SPECIFIED`   |
| Dependent type theory direction | `SPECIFIED`   |
| Inductive reasoning             | `SPECIFIED`   |
| Equality model                  | `SPECIFIED`   |
| Hoare-style contracts           | `PROTOTYPE`   |
| Weakest-precondition reasoning  | `PROTOTYPE`   |
| Refinement typing               | `SPECIFIED`   |
| SMT-assisted reasoning          | `SPECIFIED`   |
| Exact core calculus             | `SPECIFIED`   |
| Formal typing rules             | `SPECIFIED`   |
| Formal reduction rules          | `SPECIFIED`   |
| Formal substitution rules       | `SPECIFIED`   |
| Formal erasure theorem          | `NOT STARTED` |
| Mechanized soundness model      | `PARTIAL`     |

This table records the mathematical theory as written down in `FOUNDATIONS.md`,
`SPEC.md` and `KERNEL.md`, not the implementation: `Refinement typing` and
`Inductive reasoning` here are the formal accounts, and the implemented
capabilities of the same names are under Refinement status and Case analysis
and induction status. The four exact-calculus rows are `SPECIFIED` for the core
the kernel implements, `cppl-core-0.9.0`: `KERNEL.md` states its types, terms,
typing, substitution and shifting, normalization, rules, arithmetic translation
and certificate checking exactly. The parts of `FOUNDATIONS.md` that core does
not implement, such as existential quantification and the `@` domains, are
stated mathematically but not as exact rules. The mechanized soundness model
is `PARTIAL`: the checking judgment, normalization, the arithmetic translation
and certificate checking are mechanized, and the checker is proven sound with
no premise about normalization or arithmetic (`KERNEL.md` 17); the C++ kernel
is not proven to implement the model, and the reference checker and
re-checkable evidence (M5, M6) are not started.

---

# C++ integration status

| Capability                               | Status      |
| ---------------------------------------- | ----------- |
| Clang-based C++ semantic integration     | `PROTOTYPE` |
| Clang AST bridge                         | `PROTOTYPE` |
| Source mapping                           | `PROTOTYPE` |
| C++ name lookup reuse                    | `PROTOTYPE` |
| C++ overload-resolution reuse            | `PROTOTYPE` |
| C++ template interoperability            | `PROTOTYPE` |
| C++ `constexpr` interoperability         | `SPECIFIED` |
| C++ exceptions model                     | `SPECIFIED` |
| C++ RTTI model                           | `SPECIFIED` |
| C++ ABI preservation                     | `SPECIFIED` |
| libc++ interoperability                  | `SPECIFIED` |
| Existing native library interoperability | `SPECIFIED` |
| C interoperability                       | `SPECIFIED` |
| Objective-C++ interoperability           | `SPECIFIED` |
| JNI interoperability                     | `SPECIFIED` |
| N-API interoperability                   | `SPECIFIED` |
| WASM target compatibility                | `SPECIFIED` |

---

# C++ standard compatibility

Current design target:

```text
initial implementation:
C++17+

preferred primary modes:
C++20+
C++23+
```

The proof language should remain as independent as practical from the selected underlying C++ version.

Conceptually:

```bash
cppl -std=c++17
cppl -std=c++20
cppl -std=c++23
```

Current status:

| C++ mode                        | Status        |
| ------------------------------- | ------------- |
| C++98/03                        | `NOT STARTED` |
| C++11                           | `NOT STARTED` |
| C++14                           | `NOT STARTED` |
| C++17                           | `PROTOTYPE`   |
| C++20                           | `PROTOTYPE`   |
| C++23                           | `PROTOTYPE`   |
| Newer Clang-supported standards | `SPECIFIED`   |

No C++ compatibility level should be claimed as implemented until compatibility tests exist.

The conformance suite covers each of C++17, C++20 and C++23 with an ordinary
program and with a program that uses C++L words as ordinary identifiers, and
checks that the runtime program emitted for a C++17 target compiles as C++17 on
its own. That is enough for `PROTOTYPE`, not for `IMPLEMENTED`: templates,
modules, concepts and ABI-sensitive constructs are not yet covered. What a
module declares is not read, so a unit that imports one keeps every statement
led by `contradiction`, `cases`, `decompose`, `validate`, `unsafe` or `ghost`
ordinary C++, since the module may declare the word (`WORD-019`); a unit using a
module's entities so named builds as Clang builds it
(`conformance_words_as_cpp`). Verification across modules is not implemented.

---

# Verification IR status

| Capability                     | Status        |
| ------------------------------ | ------------- |
| Verification IR architecture   | `PROTOTYPE`   |
| VIR type representation        | `PROTOTYPE`   |
| VIR expression representation  | `PROTOTYPE`   |
| VIR provenance                 | `PROTOTYPE`   |
| VIR proposition representation | `NOT STARTED` |
| VIR control-flow model         | `NOT STARTED` |
| VIR state model                | `NOT STARTED` |
| VIR contract model             | `PROTOTYPE`   |
| VIR proof obligations          | `PROTOTYPE`   |
| VIR unsafe/trust annotations   | `PROTOTYPE`   |
| VIR serialization              | `NOT STARTED` |
| VIR deterministic hashing      | `NOT STARTED` |

Obligation identities are content-derived today, but they are computed from the
core representation rather than from VIR, so VIR hashing has no consumer yet and
is not implemented. VIR marks a trusted law as trusted and an unsafe block as an
`UnsafeRegion` node on the path that passes through it; both are read by
obligation generation and the trust closure, never by the kernel.

---

# Contracts status

| Capability                                        | Status        |
| ------------------------------------------------- | ------------- |
| Preconditions (supported fragment)                | `PROTOTYPE`   |
| Postconditions (supported fragment)               | `PROTOTYPE`   |
| Function invariants                               | `SPECIFIED`   |
| Loop invariants                                   | `PROTOTYPE`   |
| Loop termination (`decreases`)                    | `IMPLEMENTED` |
| Function termination and recursion (`decreases`)  | `IMPLEMENTED` |
| Verification-condition generation (returns/paths) | `PROTOTYPE`   |
| Verification-condition generation (loops)         | `PROTOTYPE`   |
| Local versioning (declarations/assignments)       | `PROTOTYPE`   |
| Weakest-precondition engine                       | `NOT STARTED` |
| Contract composition                              | `PROTOTYPE`   |
| Member-function contracts (non-virtual)           | `PROTOTYPE`   |
| Virtual-function contracts and overrides          | `SPECIFIED`   |
| Contract reuse across translation units           | `PROTOTYPE`   |

A verified function declared in one translation unit and defined in another is
verified where it is defined, and used where it is only declared through a
verification interface (`SPEC.md` Annex L.2.1, TUBOUND-002 to TUBOUND-014, RFC
0017). `cppl --cppl-emit-interface=<file>` writes one for a unit that verified
and produced its object: every contract it proved for a function with external
linkage, by Clang's USR, with a canonical identity of what the contract states,
whether it is total, and, each category apart, the trusted laws,
standard-library models, unsafe blocks, runtime validation sites and contracts
of other units its proof rests on, with a verification-result identity covering
all of it (interface format version 3; an interface of version 1 or 2 is
refused). `--cppl-import-interface=<file>`, repeatable, makes a
unit's contracts available to another. The consumer never reads a proposition
from the file: it states the contract from its own declaration and uses a record
only when the statement identities agree, so a stronger postcondition, a weaker
precondition, another overload, another specialization or another request to
terminate is refused, and so is a pure function the contract reaches, at any
depth, defined otherwise; a declaration's measure crosses only as the request to
terminate, so another measure proving the same total contract is the same
contract there. A record is named by those resting on it through an identity of
its result by meaning, which rewording its name or description does not change.
An interface written by another compiler release, under another verification
semantics, identified by a declared version and by a digest the build computes
from the verifier's semantic sources, or under another kernel, core, Clang,
language mode or target, one naming a library model this compiler does not have
or a model under another name, or one whose unit's files changed in content since it
was written, is refused whole, while one from another build of the same release
and sources, one whose sources were only touched, and a copy at another path
are used; so is a malformed, truncated or integrity-failing one, two interfaces
recording different verification-result identities for one function, and a
record whose own dependencies are not imported with the identities they were
proven with. A claim resting on an
imported contract is PROVEN relative to that record and reported with it and
with everything its proof rested on, transitively, every category complete and
mutation-checked at each step (`TRUST.md` TCB-XTU-011); it is never listed as
assumption-free, however little its records rest on. Totality crosses as recorded, and a cycle of verified contracts
that crosses a unit is refused wherever it lies. Nothing here re-checks another
unit's proof: the interface and its provenance are artifact and reuse TCB
(`TRUST.md` 31.1), the integrity digest is unauthenticated, and a deliberately
edited interface whose digest is recomputed is not detected. What binds the object linked to the
interface imported is left to the build. `cppl-lsp` imports the interfaces a
document's `compile_commands.json` entry names, read and checked by the same
code as the CLI's, and names the imported contract beside the verdict of every
claim resting on one (`lsp_interfaces_test`).

A function's header declaration and its definition may both be marked
`verified` and state its contract, as a definition whose body states loop
clauses must be; the two must state the same contract, compared by meaning, or
the function is refused (`SPEC.md` TU-003).

A call relying on a default argument of a verified or pure function is
verified with the default evaluated where the call stands, exactly as if the
caller had written it there (`SPEC.md` R.16, CONTRACTCOMP-002, EDGECASE-038,
`TRUST.md` 13): the callee's contract is
instantiated at the default's value, its precondition and each refined
parameter's predicate are owed for that value at the call, a call the default
makes owes its own precondition there, a recursive call relying on one descends
by its value, and a contract clause relying on one states the clause at it. The
default is no part of the contract. The declarations a verified function's
clauses are read back from restate its parameter list without the defaults,
every comment, attribute and name kept, and elaboration reads a clause only from
one whose parameters are the function's own; the program and the analysis text
keep the function's declaration as written. A default is evaluated only by a
call relying on it, so one that violates the precondition or the refinement is
refused at the verified call relying on it, never at the declaration, and an
unverified caller relying on it is as unverified as one writing the value out.
Across units a caller relies on its own declaration's default: a contract's
identity in a verification interface depends on no default, so a declaration
stating one matches a definition stating none. Refused, each by name: a default
the bridge cannot evaluate at the call, such as one reading a mutable global, naming the
parameter and the function whose default it is; a default calling a function
that is not pure, naming that function and the default; a reference parameter's
default, which binds storage the call does not name; a default whose end
depends on lookup Clang has not done when the declarations are written, a comma
after a `<` it leaves open as in `first<1, 2>()`, until it is parenthesized; and
a ghost initializer relying on a default with an effect or a call that is not
pure (`e2e_default_arguments`, `negative_default_arguments`,
`unit_default_arguments_test`, `default_arguments` in `e2e_erasure_equivalence`).

---

# Refinement status

| Capability                              | Status        |
| --------------------------------------- | ------------- |
| Predicate refinements                   | `PROTOTYPE`   |
| Static refinement construction          | `PROTOTYPE`   |
| Runtime checked refinement construction | `IMPLEMENTED` |
| Refinement elimination                  | `PROTOTYPE`   |
| Refinement subtyping                    | `PROTOTYPE`   |
| Indexed refinements                     | `PROTOTYPE`   |
| Arithmetic refinement solving           | `NOT STARTED` |
| Bitvector refinements                   | `NOT STARTED` |
| User-defined refinement predicates      | `SPECIFIED`   |

A refinement declaration lowers to the alias it means and adds no runtime
representation. Membership is an obligation at every modeled flow into the type - a
local declaration, an assignment or update, a verified call's argument, a return -
closed under the path conditions where the value enters, so a branch fact discharges
it. Subtyping is the implication between predicates and carries no runtime check in
either direction (`SPEC.md` 17.4).

Runtime-checked refinement construction is `IMPLEMENTED` (`SPEC.md` 28,
RUNTIMECHECK-001 to RUNTIMECHECK-021, WORD-013, RFC 0021). An unknown runtime
value comes to satisfy a refinement in one of two ways, kept apart. An ordinary
C++ condition selects a path, and a crossing on it is proven from the path's
facts: that is static proof, `PROVEN`, with no site and no runtime code of
C++L's (RUNTIMECHECK-010) -- in every crossing form a verified body has, a
local, a member, an element, an element pushed into a refined `vector` local, a
refined result, a verified callee's refined parameter, a call's post-state, and
through a checked helper returning `bool` whose contract relates its result to
the predicate (RUNTIMECHECK-006; a verified call a condition makes has its
postcondition supposed where it is made). A validation expression,
`validate<R>(e)`, asks the program to test the value against `R`'s predicate:
it lowers to a validator the refinement's declaration lowers to beside its
alias, erasure keeps it, and what its success establishes is `RUNTIME-CHECKED`
at that site (RUNTIMECHECK-011 to RUNTIMECHECK-021). Every crossing owes its
predicate under its whole path, validations passed included, and one the kernel
does not accept is refused, never made a site (RUNTIMECHECK-013): the failure
path, a check or validation too late, too weak or of another value, one a
write, call or unsafe block made stale, a disjunction's route, and a validation
outside a verified body, in a contract or loop clause, in an unsafe block, of an
unknown, indexed or layered refinement or of a predicate that is formal or only
partially defined (`negative_runtime_validation`). The trust report lists each
site with its location, refinement, predicate and function, and every claim
resting on one -- in its own body, through verified calls to a fixed point, or
through an imported contract -- names it. Such a claim stays `PROVEN` and is not
reported as resting on an assumption; that the executable performs each
validation as its lowering states is correspondence TCB (`TRUST.md`
TCB-RUNTIMECHK-006). A verification interface records sites as a category of
their own (format 3), part of the record's result identity, since verification
semantics `cppl-verification-3`. `e2e_runtime_validation` pins the whole site
and claim lists, that no path-fact crossing is a site, the program's output on
valid, invalid and extreme input, the validator and each call of it in the
runtime text, identical assembly against the program erased by hand at `-O0`
and `-O2` in three standards, and three units carrying a site across two
interfaces. Validating an indexed refinement, or one whose base type is itself
a refinement, is refused, not supported (RFC 0021, unresolved questions). Both the text report and the JSON document list every site (Unsafe
and trusted boundary status).

Scalar reference parameters (`T&`, `const T&`, `T&&`), local references to modeled
parameters, and verified void functions now use storage versions and post-state
contracts (SPEC.md 12.9). Direct writes and verified calls invalidate possible
aliases, including const references. Callee postconditions can establish facts
about new versions; refined actual storage still owes membership. Repeated actual
arguments share state. Branches and loop invariants use the same version model.
This remains `PROTOTYPE`, not production-complete refinement flow.

Refined data members are implemented over the generic place model. A member is a
place of its own, reached by a path of projections out of the object it belongs
to, so `s`, `s.x` and `s.x.y` are three places and `s.x` and `s.y` are never one.
Construction and every later write cross into the member's own declared type
through the one write path, and the obligation is owed where the value enters
the member rather than deferred to a read (`SPEC.md` 17.6, Annex I
REFINEOBL-007). A write through a reference to a member is a write to that
place, and distinct members do not disturb one another.

Semantic validity is recursive (`SPEC.md` 17.2.1): a record is valid when its
refinement-bearing subobjects are, stated over the projections that name them.
A verified parameter therefore supplies the validity of its refined subobjects
as an entry premise exactly as a refined scalar parameter does (`SPEC.md`
17.2.2), and no proof of the historical construction path is required to use it.
`S{-5}` in a verified body is rejected by the construction obligation, and a
record built outside a verified body is still refused at that boundary, because
ordinary C++ establishes its members without proof. Permanent regression tests
pin both directions, and the erasure test shows a refined member lowering to a
plain member with identical generated code.

Refined array elements use the same place model, at constant and at symbolic
indices alike. General casts, lambdas, virtual member functions, constructors
and destructors, alias-return lifetimes, `old` over mutable state, and dependent
object flows remain unimplemented.

A record is decomposed from its resolved type rather than from the cursors of
its definition, so an instantiated class template is decomposed like any other
record and a refined member of `Box<int>` is the refined storage the template
declared (`SPEC.md` 17.6, 42 TEMPLATE-001). Reading, writing, sibling
preservation, nesting and indexed refinements behave in a specialization exactly
as in an ordinary record, and matched pairs pin that the predicate, the index
argument and the component order are each really read.

Asking the type is also what makes a base subobject visible. A record with a
base is not decomposed by its own members -- the base carries state no member
names -- so the member is refused by name instead of read out of a
decomposition that left part of the object out. A union and an inaccessible
member are refused as before.

A refinement erases to its base type, so it is that type as a template argument:
`Box<Positive>` and `Box<int>` are one specialization with one member type, and
no predicate travels with the argument. Formal identity is semantic rather than
spelling-only (`SPEC.md` 43), so such a program fails to prove rather than
quietly reading a predicate that is not there. The one place such a spelling is
read is the element type of a `vector` local, where it states a content
invariant of that local's storage and nothing of the type (`SPEC.md`
STDMODEL-020); a parameter, a result, a span or a `std::array` whose element type
is written as a refinement is refused rather than read as the base type.

A member that is itself an aggregate is the places its own members are, not one
value: an aggregate local is tracked as one version per scalar leaf, reached by
a path of field and element steps. `o.i.v` and `h.items[0]` are places exactly
as `o.a` is, so a write reaches the leaf written and leaves a sibling at depth
alone, and a refined leaf owes its predicate where the value enters it. Nesting
is bounded at eight levels and 256 leaves per declaration, and construction must
stay fully visible at every level: default initialization and a union member are
each refused by name. An aggregate initializer that leaves trailing members out
(`std::array<unsigned, 10> counts{};`, `unsigned a[4] = {4u};`, `Stack s{};`)
value-initializes them, as C++ does: each scalar left out starts at zero, owing
its member's refinement of it, and an aggregate left out is the same rule member
by member. Where C++ does something else it is refused by name: a member with a
default member initializer, a class declaring a constructor, a bit-field, a
designated element and a scalar given a braced list beside members left out
(`TRUST.md` TCB-OBJ-010, `e2e_member_storage`, `negative_member_storage`).

A member of a by-value aggregate *parameter* the body writes is tracked as a
place, starting from the value the parameter arrived with; a member it never
writes is read as that value's projection. This is the same for an ordinary
record and for a specialization.

A struct also flows whole (`TRUST.md` TCB-AGGREGATE-001, TCB-AGGREGATE-002). A
struct a body tracks member by member -- a local, a by-value parameter, the
implicit object (`*this`) or a member of any of them, nested records and arrays
included -- may be returned, passed by value, handed to a reference parameter,
copied into a local (`Config d = c;`, `Config d = make(7u);`) and assigned
whole (`d = c;`, `d = std::move(e);`). A value flowing out is assembled from
the leaves the body tracks and bound where it is evaluated as a fresh value
whose leaf projections are those leaves; a value flowing in is taken member by
member, each leaf bound at its declared type, so a refined member owes its
predicate there. A copy or a move is read as the value it copies only where
C++ defines it memberwise: a copy or move constructor or assignment operator
that the class, or the class of any member, provides is refused by name, and a
defaulted one is accepted. A struct handed to a reference the callee may write,
or to any reference when the callee may run unsafe code, leaves one post-state
value of which only the callee's `ensures` is known, and each of its leaves is
rebound to its own member of that value; every leaf must be tracked there, or
the call is refused. Copy and move assignment are verified constructs of the
subset (CONSTRUCT-144, CONSTRUCT-145). Still refused: a conditional expression
choosing between two structs; a reference local of struct type, one bound to a
temporary included; a struct-typed global; a constructor other than a copy or a
move; a class with a base, a union, an unmodeled member or a user-provided
destructor; a library type such as `std::pair` or `std::optional` (a
`std::array` member is modeled); assigning a struct while an element of it is
selected at a term; `a.operator=(b)` spelled as a member call; and a `pure`
function whose body assembles a struct value, which is not a definition a
contract may unfold. `e2e_struct_values` runs each accepted flow and
`negative_struct_values` refuses each false claim through one.

A type with a member this implementation does not model -- a floating-point
value, a union, a base subobject -- is never tracked as places, at any depth.
Its representation leaves that member out of its components, so the components
no longer stand at the positions an access numbers members by, and tracking the
object anyway once put the place of `s.b` where an access to `s.a` resolved: a
claim false at run time was proven (`negative/member_numbering_gap.cpp`). Such
an object's modeled members are still read by name, as projections of the value
it arrived with (`fixtures/untracked_members.cpp`).

A contract may name types a template supplies, including dependent names
spelled through one, because each clause is projected under the header its
declaration stands under. A contract on a template itself is parameterized by
the template's own parameters and means what it means after substitution, so it
is checked per specialization (`SPEC.md` 42 TEMPLATE-001).

Clang performs the substitution and selects the specialization; what is checked
is each specialization it produced. A specialization is reached from the uses
that instantiated it, because an implicit instantiation is not a declaration of
the translation unit. Each carries its own instantiated contract: the clause
probes are declared under the same header, and the body names them at its own
template arguments, so C++ instantiates a function's contract alongside the
function at exactly the arguments Clang substituted. A non-type parameter inside
a clause is the value the specialization was instantiated at, read back from
Clang rather than substituted here.

Proof identity separates the specializations structurally rather than by a rule
of its own: obligations are keyed by Clang's USR, which already distinguishes
`f<4>` from `f<5>`, so evidence for one specialization cannot discharge another
(TEMPLATE-003). A contract that is true at one argument and false at another
fails only where it is false. A template nothing instantiates has no
specialization to check, so it is refused rather than reported as verified: an
uninstantiated contract states nothing this unit discharged.

A C++ constraint remains a C++ constraint. It controls which specialization
Clang selects and never becomes a formal premise (TEMPLATE-002).

An explicit specialization is one concrete function, not an instantiation of
the primary. It states its own contract at its own declaration, so it is
checked directly: its arguments are already fixed, its contract probes are
ordinary functions rather than templates, and nothing has to be instantiated to
reach them. The primary's proof never covers it, so a specialization that
replaces the body with one its contract does not describe is refused by name,
with the goal stated at its own arguments (TEMPLATE-003).

An explicit instantiation, `template unsigned f<4u>(unsigned);`, instantiates
the body in this unit, so the specialization it names is checked here. libclang
exposes no cursor for the instantiation itself, so the specialization is reached
the way every other one is -- from a reference to it -- which the projector
emits into the analysis text alone. The runtime text keeps the instantiation the
author wrote, and the reference reaches no object file. Each instantiation is
its own specialization: a contract true at one argument and false at another is
refused whichever order they are written in. `extern template` is an
instantiation declaration rather than a definition, so it instantiates nothing
here and is still reported as uninstantiated (TEMPLATE-001).

Verified function templates are `PROTOTYPE`. An explicit specialization declared
with its own contract crosses translation units through a verification interface
like any function (TUBOUND-004); `f<4>`'s record never serves `f<5>`, even when
the two state the same contract. A specialization of a template a unit only
declares is not instantiated there, so its contract cannot be stated to compare
with a record, and it is refused rather than guessed.

A lambda is a closure object with its own call operator, and it is refused on
every route into a verified body: bound to a local, called without ever
becoming one, and written inside a clause. A by-reference capture is why this
has to be closed rather than merely unimplemented, since one can write a
refined local after its fact was established; regression tests pin each route.
A lambda spelled to resemble the text a capability probe is projected to grants
no capability either, because a capability comes from the recognized
`readable`/`writable` form and never from what the projection happens to emit.

A value crossing from one refinement into another owes the target's predicate
like any other crossing, and the implication is proved rather than read off the
names: a stronger refinement enters a weaker one, the converse is refused with
the failing goal named, and two spellings of one predicate cross while two
arithmetically related predicates cross only because the kernel relates them.
Verification identity is what a refinement means, not how it is spelled.

A call that takes a pointer to non-const may write through it, so what the
caller knew about the pointee does not survive the call. A pointer is passed by
value, so the parameter keeps its own version while the storage it designates
goes stale; that distinction is what separates this from the by-reference case,
and missing it once let a contract promising a positive result verify while
returning zero. A pointee reached only through a pointer to const survives,
because writing through one is not something the callee may do.

A non-virtual member function is verified with its implicit object
(`SPEC.md` CLASS-008 to CLASS-015, `docs/rfcs/0018-verified-member-functions.md`).
The object is a receiver place, and its modeled scalar members, members of
members and elements of member arrays are places projected from it; this
implementation lowers them to reference parameters before the written ones, so
a member function is the same verified callable a function is and no part of
the verifier is specific to it. A `const` member function binds the places
`const` except through a `mutable` member; a ref-qualifier constrains only which
objects the call may be made on, so `std::move(o).f()` is a call on `o`. The
places relate to every other access path by the common alias model, with no
type-based argument: a write to a member keeps a sibling scalar member's facts,
and a write through a reference or pointer, a call's write and an unsafe block
take away what they may reach. A call gives a post-call version to each place
the callee may write and to each it only reads that may be one it writes, of
which only the callee's `ensures` is known; a place no write of the call can
reach keeps its version. A refined member is charged its predicate where each
value enters it -- an assignment, a write through a reference that may be it, a
call's effect -- and not again at the return; a version no route charged, such
as one an unsafe block left, is charged where the function returns
(`SPEC.md` REFINE-060 to REFINE-062, `TRUST.md` TCB-OBJ-009). This applies to a
refined reference parameter of any function alike. The object a pointer
parameter designates is a receiver, reached under `readable(p)`, and
`writable(p)` for a call that may write. Recursion, loops, ghost state and case
splits work in a member function as in a function; a static member function is
a function, and a `static pure` one is a definition a contract may use.
`fixtures/verified_methods.cpp` pins each accepted form with the refused half
of its matched pair beside it (`negative_verified_methods`), and
`fixtures/equivalence/methods.cpp` pins that a class with verified member
functions compiles to the same code and the same symbols as the class written
without them.

Virtual functions stay refused at the declaration, however the declaration says
it is virtual and when it overrides without saying so, and a call to one from a
verified body is refused, which is what closes virtual dispatch rather than
leaving it open: the dynamic type decides which body runs, so a contract proved
from a base's body would not cover an override that replaces it (CLASS-014).
Constructors, destructors, member function templates, members of class
templates, volatile member functions, member functions of a union or of a class
with a base, calls through pointers to member and `this` as a value are refused
by name (CLASS-015), and so are members whose storage may overlap another place
or change unseen: reference members, bit-fields, members of anonymous unions and
structs, and volatile members. These receivers are refused as limitations of
this implementation, not of `SPEC.md`: a call on an element of an array of
class type selected at a term, whose members get no place; and a call through a
pointer that is not a parameter, which no contract names a capability for. A
member array of the implicit object, `std::array` or built in, is one place per
element and is subscripted at a term against the extent its type states, as a
local array is; one whose `std::array` element type is written as a refinement
refuses the member function (STDMODEL-020). A member access whose element
place sits in another member than a write's is apart from it, wherever either
selects an element at a term (`e2e_member_storage`, `negative_member_storage`). A
contract is stated on the declaration in the class, and an out-of-line
definition inherits it. A clause cannot call a member function, which is not a
definition the formal core unfolds, and without `old(...)` a postcondition
states the post-state only, so a mutating member function states in `ensures`
every member its callers rely on afterwards. An out-of-line definition cannot
restate the contract, so one whose body needs loop clauses is refused; such a
body is defined in the class. For the same reason a claim that a path cannot
occur (`contradiction e;`) is refused in an out-of-line definition, which is not
marked `verified`; the body calls a verified function that makes the claim
instead (`negative/integration_out_of_line_claim.cpp`,
`fixtures/integration/ledger.cpp`). A member of container type is not tracked
storage, so a member function of a class holding one is refused naming the
member; the container is passed as a parameter instead
(`negative/cross_feature_container_member.cpp`). No disjointness of an object and
a reference argument is assumed, from their types or otherwise, so after a call
that may write through a reference argument only the callee's `ensures` is known
of the object. The accounting of refined places (`TRUST.md` TCB-OBJ-009) is
checked across the slices: the effect of another unit's contract on a refined
member is charged at the call, and a refined member or reference parameter is
charged at the return after a loop, after an unsafe write that a `break` or a
`return` inside the loop leaves with, and after a `push_back` through a
reference argument that may reach the object; each such route has a refused
twin that could leave a value outside the refinement, and counting the unsafe
block's or the loop head's version valid is mutation-checked
(`e2e_cross_feature`, `negative_cross_feature`). A member function defined in another translation
unit crosses through a verification interface as a function does: the unit
defining it records the contract its class declares, with the implicit object's
places among the parameters, and a caller uses it only when its own declaration
states the same contract, qualifiers and `mutable` members included
(`fixtures/methods_cross_tu/`, `e2e_verified_methods`, `negative_verified_methods`).

An object a parameter designates by reference is caller storage whose places
any write that may alias them gives a new version, so a member read after such a
write is of a value nothing states, and the object's post-state is what it holds
at return (`SPEC.md` VERIFIED-030, VERIFIED-031). A record or an array whose
every member is modeled and whose type states no refinement of the whole is
followed member by member, as the implicit object is: a member of it is written
in place, a member call on it and a call handed it take each member's effect,
and a normal return hands back the value its members assemble (`TRUST.md`
TCB-AGGREGATE-003, `e2e_member_storage`). Any other is tracked as one place,
which is read and never written. Before this, the member was
read as the value it arrived with even after a write through another reference,
and a contract false at run time was proven (`negative/methods_reference_object_stale.cpp`,
`negative_verified_storage`).

A return whose value is a call states the post-state of each reference
parameter in the scope the call's result is in (`SPEC.md` VERIFIED-031). The
post-state was once stated before the call's result was in scope, so it named
the binder standing at its position then: `ensures (r == 5u)` was proven of a
function that never wrote `r` and returned a call whose result was 5, a
contract false at run time (`negative/post_state_after_returned_call.cpp`,
`fixtures/post_state_calls.cpp`, `negative_verified_storage`).

A refinement does not cross a translation unit on a declaration's word
either. An ordinary function's refined return is refused as evidence at the
boundary, including through a header, so the only way a refined value enters is
where its predicate was proved: in the unit itself, or in another unit whose
verification interface records the verified function's contract (TUBOUND-004).

Binding a conditional to a local splits the route on its condition, so each arm
is proved under what its own path supposes rather than as one opaque `select`
term. An arm that is itself a conditional splits again, and a refinement
crossing may be discharged arm by arm. This adds proof power and no fact: a
single failing arm still rejects the binding, and a guarded arm supposes only
what its condition states.

Which conditional a route splits on follows what the bound value denotes, not
how it is written. A read denotes the value its version was given, so resolution
follows reads transitively to any depth: a conditional reached through any
number of intervening locals splits as a directly written one does, and a
conditional whose arm reads an earlier conditional local resolves through it.
Resolution is bounded without a fixed hop limit, because a version's value reads
only versions established before it, and it does not cross a version boundary.
A conditional local the returned value reads beneath another operation, as
`return !both;` reads `both = p && q`, splits the same way, and the composition
of the path proofs takes that `select` apart where the value has it, the motive
abstracting it there and nowhere else (`assemble` in
`compiler/automation/src/composition.cpp`; automation proposes, and the kernel's
conditional elimination is what accepts it).

`&&`, `||` and `!` are modeled in a verified condition. They are not lowered as
values — a proposition is not a value, and outside a condition they stay refused
— but elaborated into the routes they select between, recursively, so nesting
works to any depth. This models C++ short-circuit evaluation exactly rather than
approximating it: an operand appears only on the routes where C++ evaluates it.
The route where `A && B` fails is the union of `!A` and `A && !B`, never one
route supposing both sides false, and the route where `A || B` holds is likewise
a union that establishes neither side alone. `e2e_refinement_flow` pins the
proven crossings together with the guards showing the added power creates no
fact; `negative_verified_paths` pins the false-route behavior.

Nested effectful expressions without represented C++ sequencing are rejected.
Route splitting and proof composition derive branch structure separately and
must agree, so a body whose splits cannot be kept in step with the `select`
nesting of its lowered value is refused rather than proven. These are
implementation gaps, not completed capability.

Pointer dereference resolves to a place and reads and writes through the common
machinery. `*p`, `*p = e`, `p->m` and `p[i]` all form a `Deref` place rooted in
the pointer and the version whose value they dereference, so `*p` before and
after a write to `p` are different places. Forming one requires a capability,
which the contract states as `readable(p)` or `writable(p, n)` and nothing else
supplies: `p != nullptr` establishes neither, and a failed capability is a
diagnostic rather than a silent assumption (`SPEC.md` 12.10 VERIFIED-037,
VERIFIED-043). A pointer computed by arithmetic or returned by a call names no
place this implementation can identify and stays refused. Reading requires
`readable` and writing requires `writable`; neither entails the other. Each
access owes its own: a place a read formed is written again only under
`writable`, one a write formed is read again only under `readable`, and after an
unsafe block no place formed before it is reached again at all.

A verified call owes the capabilities its callee's contract states, as it owes
the callee's precondition (`SPEC.md` VERIFIED-013, VERIFIED-043, `TRUST.md`
TCB-CAP-009). The caller must pass one of its own pointer parameters and hold a
capability of the same kind on it; a pointer it holds nothing for, a capability
held for another pointer, and `readable` where `writable` is required are each
refused by name. Where either side states an element count, the callee's count
instantiated at the call's arguments must be proved no larger than the caller's,
an ordinary obligation the kernel decides; an unstated count is one object, so a
caller holding `writable(q, m)` passes `writable(q)` on only where `1 <= m` is
proved. Before this check a caller with no capability at all was reported
proven; `negative_memory_capabilities` pins the refusals and matched pairs.

`readable` and `writable` are built-in specification propositions, not calls to
user functions, and they never become runtime calls. They are recognized
contextually, so ordinary C++ that already spells a function or variable
`readable` keeps its own meaning. Because a contract states one `expects`
clause, several capabilities are written joined by `&&`, and a capability may
be conjoined with ordinary predicates there too: the clause is read apart, the
capabilities on their channel and the predicates as preconditions, and a
caller owes both (`SPEC.md` STDMODEL-016). A capability combined any other way
is refused.

A capability never reaches the proof kernel. It is a property of the execution
state rather than a computable function of any value, so encoding it as a term
would need an uninterpreted constant and adding a proposition former for it
would put memory semantics inside the trusted kernel (RFC 0014 §10). Instead the
obligation layer carries capabilities as context hypotheses, structurally
separated: `vir::Capability` is deliberately not a node of `Expr`, so there is
no path from a capability to the kernel's proposition language. Capability
tracking is a correspondence-layer responsibility and carries a stated TCB delta
(`TRUST.md` 15); it adds no kernel rule, axiom or logical assumption.

Bounds are the opposite case and are *proved*. A symbolic subscript forms a
symbolic element place and owes `index < extent`. Both sides are terms, so the
kernel checks it with the existing arithmetic rules. Two symbolic elements are
disjoint only when their indices are proved unequal: a write at `a[j]`
invalidates what was read at `a[i]` unless `i != j` is established, and a false
rejection is preferred to a stale fact. Refined elements owe their predicate at
every write, symbolic or not.

Identity is the separate question, with the opposite conservative answer, and it
is decided on the index term (`SPEC.md` STORAGE-010, `TRUST.md` TCB-ALIAS-006).
A place's path records that a step was symbolic, not which element it chose, so
two subscripts are one place only where the index terms are seen to be one term
read at one set of versions. `a[i]` and `a[j]` are therefore two places and
carry no fact between them, and an index reassigned between two accesses names
another element at the second. The comparison is structural and errs toward
difference: an index shape it does not decide gets its own place, which costs
precision and never soundness. One term, including a compound one such as
`a[i + 1]`, still names one place, so an element written is read back. Being two
places withholds a fact and does not make the storage disjoint, so a write at
either still invalidates the other, including an element selected at a constant.

Each subscript also owes the capability its own access needs, which is how a
region held only as `writable(p, n)` refuses a read of an element it just
wrote: `writable` does not entail `readable`, and writing an element first does
not earn it.

A symbolic element read now supplies the element type's refinement (`SPEC.md`
REFINE-060 to REFINE-062, `TRUST.md` TCB-REFINE-009). The value is unknown, but
it is unknown *within* the declared type: the array entered the modeled state
through an initializer that established validity for every element, and every
later write was modeled here and charged the same predicate, so the element's
current version holds a value of its type even though which element is
undecided. The predicate belongs to that version rather than to the storage, and
it is derived and not charged again — demanding it at the read would make the
body pay for one crossing twice. `a[i]` of a `Positive[3]` therefore proves
`result > 0`, and a refinement of a refinement supplies both predicates and no
third one.

The rule needs closed accounting for the writes that may reach the location, so
this implementation applies it only where it has that: a local array, entered
through an aggregate initializer, whose address it never let escape. Indirection
is not what disqualifies a location — the spec allows the rule to reach storage
behind an alias once the accounting is closed — but this implementation does not
close it there, so a pointee gets nothing, and a pointer to a refined type is
never itself the evidence. The escape test is deliberately stricter than the one
aliasing uses: the array-to-pointer decay a subscript performs on its own base is
not an escape, while a decay into a call argument or into pointer arithmetic is.
Only the pointee limit is observable today; storage a reference parameter
designates and a local whose address escapes describe bodies this implementation
already refuses for containing an unmodeled `&` or decay, so those conditions are
the contract for when those forms are modeled rather than gates that fire now.

A parameter passed by value owns a distinct parameter object, so its ordinary
contained subobjects are places of the callee and writing one is an ordinary
write (`SPEC.md` STORAGE-011). `s.x = 5; return s.x;` verifies, a sibling keeps
the value it arrived with without becoming known, a nested member is reached by a
longer path, and a refined member owes its predicate on the way in exactly as a
local's does. A write inside the parameter object does not reach the caller's
argument and does not reach another parameter.

Ownership stops at indirection, and this implementation stops well short of it:
a parameter whose type has a pointer or reference member is not tracked at all,
because such a member is not a modeled value type, so writing any member of such
a parameter is refused rather than treated as callee-owned. `*s.p` is therefore
never reached through this rule, and a fact about a pointee does not survive a
write through a pointer that may designate it. A parameter that may designate
caller storage gets none of this and is refused where it is written. A member
array of a by-value parameter is tracked only once something writes it, so
reading one at a symbolic index is still refused for an unknown extent.

The extent is a term rather than a count. A constant extent canonicalizes to a
literal, and the extent a capability states does not: `readable(a, n)` bounds a
region by a runtime value that no enumeration of elements can recover. A
subscript through a capability-held pointer therefore owes `index < n` against
that term, and the bound is proved the same way an array's is. The capability
and the bound stay on their separate channels: the capability permits reaching
the region, and the arithmetic decides which element was named.

The one-object form states no extent at all, so it bounds no element and a
subscript under it fails closed. An unstated extent is not an unbounded one.
An index and a capability's extent of different integer types are refused
rather than converted. The conversion is modeled (RFC 0019), and a container
subscript states its bound on the index converted to the size type
(`SPEC.md` STDMODEL-012), but a pointer subscript does not state it yet, and its
diagnostic still says the conversion is not modeled.

The obligation is generated wherever the index is a term, and the extent comes
from the array's resolved type rather than from whichever of its elements an
earlier access happened to form. A symbolic subscript into `const T (&a)[N]`
is therefore bounded without any prior `a[0]`, and a dependent extent states
the same obligation at each specialization's own substituted `N`: `i < 8` does
not bound an index into an array of 4.

Two routes reach an indexed array, and they answer different questions
(`ARCHITECTURE.md` 21). A C++ expression that denotes storage takes the Place
route, which owns capabilities, versions, writes and aliasing; a failure to
build that Place fails closed rather than falling back. An array already held
as a formal value takes the indexed observation of `FOUNDATIONS.md` 45, which
is read-only and has no version of its own. Both prove the same bound against
the same index term.

Observing an element establishes nothing about the index: the observation is
total, and `index < extent` is owed separately. An index Clang folds to a
constant outside the extent is refused as the decided out-of-bounds access it
is, rather than becoming an obligation that merely fails to prove. Indexed
observation admits neither injectivity nor extensionality, so equal elements
never prove equal indices or equal arrays.

A capability names the pointer whose storage it describes, `readable(p)`, as
`SPEC.md` 12.10 states it. RFC 0014 §12 writes the same capability over the
place, `readable(*p)`; that spelling is refused by name, because projecting it
as written would emit the dereference the capability exists to permit.

Pointer values and proof-side `null`/`non_null` case analysis are `IMPLEMENTED`
and unaffected. Reference capture and returned aliases remain sequenced behind
the rest of the storage model (RFC 0014 §17).

Steps 1 to 7 of that model are implemented. A place is a root - a local, the
referent a by-reference parameter designates, or the pointee a pointer
designates - and a path of projections into it, so a member of a member is an
ordinary place rather than a special case.
`PlaceVersion` and `PlaceRef` are the only version and read nodes in the VIR;
there is no parallel local-only path. Every read resolves a place to its current
version through one mechanism, and every write - a declaration, an assignment, a
compound update, an increment, a member initialization, a write through a
reference - establishes a version through one mechanism, which is where the
refinement crossing is generated.

Aliasing is conservative and proved only from what Clang resolves: distinct
locals never share storage, and within one object paths that differ at some step
select different members. A write to an object reaches the members inside it and
a write to a member reaches the object it belongs to, because they are the same
storage at different granularity. Two by-reference parameters may designate one
object, so a write through either havocs the other. A dereference is the
conservative case: two dereferences of different pointers may always alias, and
a dereference may reach any local whose address the body takes, which Clang
resolves. A local whose address is never taken cannot be a pointee and is left
alone. No type-based argument is used: strict aliasing presupposes the
undefined-behavior freedom a proof has not established.

An array local is the same model: it is a record whose members are its elements,
so a constant index names a place and a write reaches exactly that element. A
variable index names a symbolic element place instead, which is not resolved to
any particular element: it owes its bound, its value is opaque, and it is never
concluded disjoint from a sibling. Only aggregate initialization, or a copy or
a move C++ defines memberwise of a whole value of the record's own type, is
admitted for a tracked record, because any other constructor call or default
initialization would leave a tracked member holding a value the body cannot
state.

The same membership checks cover partial-correctness bodies containing loops and
their callers, including unused refined locals. Corrupt or unresolved refinement
metadata fails closed. This closes a verification gap without promoting the
overall refinement feature beyond `PROTOTYPE`.

A verified refined return can now supply the postcondition without a repeated
`ensures`. Ordinary refined-return declarations and unverified refined storage
are diagnosed explicitly. Mutable object construction remains outside the body
model; this boundary audit does not implement refined fields or aggregates.

---

# Case analysis and induction status

C++L reasons over C++ types and adds no data types of its own (`SPEC.md` §19,
RFC 0005).

| Capability                                  | Status        |
| ------------------------------------------- | ------------- |
| Generic case engine (all representations)   | `IMPLEMENTED` |
| Arm binders, scope and named premises       | `IMPLEMENTED` |
| Exhaustiveness from a provider's partition  | `IMPLEMENTED` |
| Scoped-enumeration provider                 | `IMPLEMENTED` |
| Enum residual `unnamed(value)`              | `IMPLEMENTED` |
| `std::variant` provider                     | `IMPLEMENTED` |
| `std::optional` / `std::expected` providers | `IMPLEMENTED` |
| Pointer null / non-null provider            | `IMPLEMENTED` |
| Product decomposition providers             | `IMPLEMENTED` |
| Formal value model for the above            | `IMPLEMENTED` |
| Cross-provider nested decomposition         | `IMPLEMENTED` |
| Case splits on a runtime path               | `IMPLEMENTED` |
| Case facts invalidated with their version   | `IMPLEMENTED` |
| Impossible cases (`omit ... by ...`)        | `PROTOTYPE`   |
| `induction` with explicit arms / short form | `PARTIAL`     |
| Machine-integer induction principles        | `PARTIAL`     |
| Pointer-structure induction (premised)      | `SPECIFIED`   |
| `@N` `@Z` `@Seq` `@Set` `@Map` domains      | `SPECIFIED`   |
| Machine-to-domain conversions               | `NOT STARTED` |
| Wildcard arms                               | `NOT PLANNED` |
| General-purpose algebraic data types        | `NOT PLANNED` |
| Runtime pattern matching (`match`)          | `NOT PLANNED` |

`induction` is implemented over an unsigned machine integer parameter of a
proof (`SPEC.md` INDUCT-001 to INDUCT-005): the short form, whose cases
automation must prove, and explicit `zero` and `successor(pred)` arms, where the
successor arm supposes `pred < max` and the claim at `pred`. The kernel states
both cases and checks them with its unsigned induction rule (core/kernel 0.9.0,
`TRUST.md` 5.1), so no assumption is added, and each accepted proof has a
refused twin (`e2e_induction`, `negative_induction`); it erases whole
(`fixtures/equivalence/induction.cpp`). A signed integer, an enumeration, a
refinement, a pointer and a class have no principle and are refused (INDUCT-004),
and induction over pointer structures and the `@` domains is not started, so
both rows stay `PARTIAL`.

---

# Termination status

| Capability                   | Status        |
| ---------------------------- | ------------- |
| Structural recursion         | `SPECIFIED`   |
| Explicit `decreases` clauses | `IMPLEMENTED` |
| Well-founded recursion       | `IMPLEMENTED` |
| Termination checker          | `IMPLEMENTED` |
| Mutual recursion policy      | `IMPLEMENTED` |
| Termination diagnostics      | `IMPLEMENTED` |
| Nontermination isolation     | `IMPLEMENTED` |

Proof-producing nontermination must never be accepted as proof evidence. The
checker is the one described for loops and verified functions above: every
continuing loop path and every call within a recursion group owes a strictly
smaller unsigned measure, lexicographically for a list, as an obligation the
kernel decides; recursion needs a measure in every function of it; and a
contract is total only when every loop and callee it depends on terminates.
Where a descent is not proven, the diagnostic names the measure before and
after. Where a contract is proven but its termination is not established, a
warning names why. Nontermination is isolated because a function with a loop or recursion
is never a core definition, so the kernel unfolds nothing that could diverge.
Structural recursion without a written measure is not inferred.

---

# C++ safety status

The C++ safety model is essential to C++L's eventual guarantees.

A theorem about C++ execution is not meaningful if undefined behavior or incorrect machine semantics can invalidate the assumptions used by the proof.

| Capability                            | Status        |
| ------------------------------------- | ------------- |
| Signed-overflow reasoning             | `PARTIAL`     |
| Division-by-zero reasoning            | `PARTIAL`     |
| Shift validity                        | `SPECIFIED`   |
| Bounds checking                       | `PROTOTYPE`   |
| Nullability reasoning                 | `SPECIFIED`   |
| Object lifetime model                 | `PARTIAL`     |
| Reference validity                    | `PARTIAL`     |
| Pointer arithmetic                    | `SPECIFIED`   |
| Pointer provenance                    | `SPECIFIED`   |
| Aliasing                              | `SPECIFIED`   |
| Move semantics                        | `SPECIFIED`   |
| Destruction                           | `SPECIFIED`   |
| Uninitialized reads                   | `SPECIFIED`   |
| Cast validity                         | `SPECIFIED`   |
| Data race reasoning                   | `SPECIFIED`   |
| Implementation of C++ safety analysis | `PROTOTYPE`   |

What is implemented is a set of obligations inside verified bodies, each owed
where the operation stands and proved by the kernel, and nothing outside them:
signed overflow, division by zero, the least value over `-1` and conversions a
signed target cannot hold (RFC 0019, [Machine arithmetic
status](#machine-arithmetic-status)); `index < extent` at every modeled
subscript of an array, a capability region and a standard container (RFC 0014,
RFC 0016, RFC 0020); and, for the container subset, that a span or element
reference is used only at the storage generation it was formed at and a span is
formed only over a container that outlives it. That last is the whole of
reference validity checked today. Unverified code is not analysed, shifts and
the casts the lowering does not model are refused in verified bodies, and every
other row stays `SPECIFIED`.

Unsupported behavior must eventually be:

```text
rejected
or
unsafe
or
trusted
```

never silently verified.

---

# Memory model status

| Capability                     | Status        |
| ------------------------------ | ------------- |
| Lifetime model                 | `PARTIAL`     |
| Ownership model                | `SPECIFIED`   |
| Borrowing/equivalent reasoning | `SPECIFIED`   |
| Aliasing rules                 | `SPECIFIED`   |
| Pointer provenance             | `SPECIFIED`   |
| Mutation model                 | `PROTOTYPE`   |
| Move semantics                 | `SPECIFIED`   |
| Destructor semantics           | `SPECIFIED`   |
| Standard container models      | `PARTIAL`     |
| Memory verifier                | `PARTIAL`     |

The mutation model is the storage model of `SPEC.md` 12.10 (RFC 0014): places,
versions and one write path that invalidates whatever may alias the storage
written, described under [Refinement status](#refinement-status); member
functions and the container subset use it and add no mutation rule of their
own. The memory verifier is `PARTIAL`: memory capabilities over pointer and span
parameters, bounds at every modeled subscript and the container generations are
checked; ownership, provenance, destruction and moves beyond a container's are
not.

The lifetime model is `PARTIAL`: the storage generations of the standard
container subset (see [Standard library verification
status](#standard-library-verification-status)) end every view and element
reference of a container's storage where the storage may be reallocated,
replaced or moved, and a view is formed only over a container that outlives
it. Lifetimes outside that subset are still refused or unmodeled.

The exact formal memory calculus is not yet frozen.

---

# Machine arithmetic status

| Capability                       | Status        |
| -------------------------------- | ------------- |
| Mathematical integer distinction | `SPECIFIED`   |
| Fixed-width integer semantics    | `PARTIAL`     |
| Unsigned `+` `-` `*` (modular)   | `PROTOTYPE`   |
| Order reasoning (linear)         | `PROTOTYPE`   |
| Signed `+` `-` `*` and unary `-` | `PARTIAL`     |
| Division, remainder              | `PARTIAL`     |
| Integral conversions             | `PARTIAL`     |
| Shifts, bitwise operators        | `NOT STARTED` |
| Checked arithmetic               | `SPECIFIED`   |
| Wrapping arithmetic              | `SPECIFIED`   |
| Saturating arithmetic            | `SPECIFIED`   |
| Big integer proof domain         | `SPECIFIED`   |
| Bitvector solver integration     | `NOT STARTED` |
| Overflow diagnostics             | `PROTOTYPE`   |

Fixed-width semantics are `PARTIAL`: `+`, `-`, `*` and unary `-` in an
unsigned or a signed common type (the one the integral promotions and the usual
arithmetic conversions select), `/` and `%`, integral conversions and all six
comparisons are modeled exactly at every width from 1 to 64 bits, each operation C++
defines only under a condition owing it where it is evaluated (RFC 0019).
Signed arithmetic and division are `PARTIAL` because a product of two unknowns
is decided only where their types bound it, and a quotient by an unknown divisor
is left unknown; conversions are `PARTIAL` because those to or from floating
point, and from an integer to an enumeration, are refused, as is compound
assignment of a
promoted type, and `char8_t`, `char16_t`, `char32_t`, `wchar_t` and bit-fields,
whose promotion is not modeled, are refused where they are named. Shifts and
bitwise operators are refused. A conversion between `bool` and an integer
type, implicit or written, is what C++ defines it to be: an integer converts
to `bool` as whether it is nonzero, and `bool` to an integer as 1 when true and
0 when false (`negative_boolean_conversions`). An enumeration, scoped or not, is a value of
its underlying type: an unscoped one converts implicitly to an integer type as
that value, and one without a fixed underlying type, which holds a subset of
that type's values, is read as the whole type, which asks more and never less.
A namespace-scope or static constant, `constexpr` or `const` and not
`volatile`, is read as the value Clang computes from its constant initializer;
writing one is undefined behavior, and one whose initializer Clang cannot
compute is refused by name (`negative_global_constants`). `Wrapping arithmetic`
above means the explicit `Wrapping<T>` facility of the roadmap, which is not the
same as C++ unsigned arithmetic.

---

# Verified C++ subset status

| Capability                                   | Status        |
| -------------------------------------------- | ------------- |
| Explicit V1 verified subset (RFC 0022)       | `IMPLEMENTED` |
| Annex X constructs verified in a body        | `PARTIAL`     |

The constructs a verified body may use are listed, one row for each construct
of `SPEC.md` Annex X, in `tests/fixtures/subset/manifest.tsv`, and
`e2e_safety_subset` checks every row on every run (RFC 0022). Of the 151
constructs, 96 are verified: each has a fixture that is proven with nothing
unresolved and runs, and a refused twin, the same program with one thing
changed, that shows the construct is modeled rather than passed over. The other
55 are refused wherever a verified body uses them, each with the diagnostic its
row pins, and never written to an object: among them floating point, pointers
other than parameters read under `readable`, shifts and bitwise operators,
a comma inside an expression, `goto`, exceptions, dynamic allocation, lambdas,
virtual dispatch and virtual functions, casts between class types, unions,
bit-fields, static and thread-local storage, globals, verified constructors and
destructors, coroutines, variadic verified templates and modules. The manifest
must name each construct of Annex X exactly once, so a construct the
specification adds is refused until it is classified. `PARTIAL` above is the
share of Annex X verified, not a weakness of the matrix: widening it is a later
RFC.

The rest of the test fixtures are held to the same standard. Every fixture the
compiler accepts has a refused twin, the same program with one thing false,
listed in `tests/fixtures/negative/twins/manifest.tsv` and checked by
`negative_refused_twins`: 66 are written out there and compiled beside their
fixture, units they import included; 55 are refused fixtures written out before
and run by the negative script that owns them; 18 fixtures are exempt, each
with its reason, because they state no claim (ordinary C++ clients, among them
programs using C++L words as their own names, tampered erasures, a C++ type
error). A fixture added without a row fails the test.

---

# Unsafe and trusted boundary status

| Capability                          | Status        |
| ----------------------------------- | ------------- |
| `unsafe` syntax                     | `IMPLEMENTED` |
| `trusted law`                       | `IMPLEMENTED` |
| Trusted memory propositions         | `PARTIAL`     |
| Unsafe-to-verified transition rules | `IMPLEMENTED` |
| Unsafe dependency reporting         | `IMPLEMENTED` |
| Trust propagation                   | `IMPLEMENTED` |
| Assumption closure                  | `IMPLEMENTED` |
| Trust reporting (closures, categories) | `PARTIAL`  |
| Trust report output (text and JSON) | `PARTIAL`     |

The intended verification statuses are:

```text
PROVEN
TRUSTED
RUNTIME-CHECKED
UNSAFE
UNVERIFIED
UNRESOLVED
```

`PROVEN` and `TRUSTED` are distinct implemented states, and `UNRESOLVED` is the
fail-closed default. A `trusted law` is assumed rather than proved and is named
in the trust report with its location and a content-derived identity; writing a
proof for one is refused, since the declaration would ask both to assume and to
prove it.

A proof uses a trusted law by naming it in `exact`, `apply`, `rewrite` or
`contradiction` (`SPEC.md` TRUSTED-006 to TRUSTED-009, PROOFSRC-005). The proof
is then established relative to it: its evidence is closed over the law's
proposition as a premise, and the kernel checks the claim under exactly the
premises its verdict names, so a claim cannot use an assumption it is not
reported as resting on. The law's own premise is still owed, and the law is not
a premise anywhere it is not named. A proof that uses such a proof rests on the
same laws, an omitted case rests on those of the proof it is written in, a
runtime path claimed not to occur rests on those of the proof it names, and a
verified function's contract rests on those of the obligations of its body and
of every contract it calls, closed to a fixed point over recursive calls. Each
proven law, proof declaration, proof of a law instance, contract, omitted case
and impossible path is reported with its closure, marked as named directly or
reached through what it uses, and each category is split into
`assumption-free` and `relative to trusted laws`. Trusted laws nothing rests on
are listed as unused. A dependency the report cannot attribute is an internal
error, not an omission.

A call to a function whose unsafe code may write what it is handed -- one that
holds an unsafe block, calls such a function, or is recorded by an imported
interface as resting on one -- is modeled as writing every reference, pointer
and view it hands over, whatever their constness: the callee's contract
describes each at the value the call leaves there, temporaries included, so no
caller keeps a fact about a `const` argument that `const_cast` in the block
could break (`TRUST.md` TCB-UNSAFE-004, `negative_unsafe_callees`).

A runtime path claim is the one way a trusted premise enters a verified body, so
a contract rests on a trusted law only through one, in its own body or in a
function it calls. A recursion group is closed the same way, to a fixed point
over its cycle, which `tests/unit/trust_closure_test.cpp` exercises with trusted
laws and unsafe blocks. Across translation units, a verification interface
carries each recorded contract's closure, each category apart: a claim proven
through an imported contract rests on that record, as an external verified
dependency, and on the trusted laws, library models, unsafe blocks and further
imported contracts its proof rested on, each named in the report with the
interface and record it came from (`SPEC.md` TUBOUND-002, TUBOUND-006). Such a
claim is never assumption-free, and whenever an interface was imported the
report says that interface provenance is unauthenticated (`TRUST.md`
TCB-XTU-010). A verified call to a function defined in another unit whose
interface is not imported is refused, so no claim rests on an assumption this
unit cannot list.

Both trust-report rows are `PARTIAL` for the same reasons: unverified foreign
boundaries are not analysed and are reported as such -- in the V1 subset no
verified body calls a function without a contract, so a proven claim reaches
foreign code only through an unsafe block, which is reported -- and neither form
gives the identity of a claim's evidence, since proof terms have no canonical
serialization (`TRUST.md` 36.1). The JSON document names, for each claim, the
proven claims its proof uses directly (`proof_dependencies`, `TRUST.md` Annex
C.2), so the proof-dependency graph is discoverable from it. The report is written as text by
`--cppl-trust-report` and as a JSON document for tools by
`--cppl-emit-trust-report=<file>` (`DEVELOPER_GUIDE.md` 12.2): one summary
rendered twice, each claim classified by one predicate in both, and every count
and claim list of one checked against the other (`e2e_trust_report_json`,
`unit_trust_report_test`). The document states of each claim whether its trusted
closure is empty (`TRUST.md` Annex C.5), records the build, and is written only
once a compile has verified; a compile that does not verify removes one an
earlier compile left at its path. Runtime validation sites are reported, each
`RUNTIME-CHECKED` with the claims resting on it (`SPEC.md` RUNTIMECHECK-013,
RUNTIMECHECK-014), and cross units in their own interface category.

A trusted law may admit a memory proposition, `readable(p)` or `writable(p, n)`,
under an ordinary premise (`SPEC.md` TRUSTED-003, VERIFIED-044). It is recorded
as an explicit assumption with its location and a content-derived identity,
reported `TRUSTED` with what it admits, shown so in an editor, and erased. This
is `PARTIAL` for one reason: a capability is not a proposition the kernel checks
(RFC 0014 §10), so no proof goal or premise can be one, and no statement can
name such a law as evidence. It is therefore listed among the unused trusted
laws with that reason. A proof statement naming one, and a proof or an ordinary
law whose own claim is a memory proposition, are refused by name
(`negative_trusted_dependencies`). No construct of this implementation consumes
a trusted capability; one would need a statement that names capability
evidence, which `SPEC.md` does not yet define.

Every proven claim is listed in the trust report with the content identity of
what it states (`TRUST.md` 36.1): under `Trust-dependent claims` with each
trusted law it rests on, or under `Assumption-free claims`. A law named twice,
or reached both directly and through a proof, is one dependency, marked direct;
a law reached through another law's written proof is reached through a proof it
uses (`fixtures/trust_closure.cpp`).

`unsafe` marks a runtime boundary (`SPEC.md` 26). A block `unsafe { ... }` and an
`unsafe` function declaration are recognized under the same C++-first rule as
every contextual word: in a unit that uses the word for anything else, both stay
ordinary C++, and a warning says so where an unsafe boundary could have been
meant: on a declaration, and on a block in a verified body or whose braces hold a
statement (`WORD-018`). `unsafe{};` elsewhere is a temporary, and nothing is
said about it. Both erase to the C++ they mark: the keyword
leaves, and the function, the block's braces and every statement in it stay and
run as written (`fixtures/equivalence/unsafe_boundaries.cpp`).

In a verified body a block's statements are not verified: nothing they compute
is known after the block, and they establish no fact (UNSAFE-003, UNSAFE-005).
Every place the block could have written gets a fresh value no earlier fact
describes and that keeps no refinement (`TRUST.md` TCB-UNSAFE-002,
TCB-UNSAFE-003): what a pointer designates, the storage a reference parameter
designates, and every local whose address the body takes or that any unsafe
block of the body names, since a block may keep an address and write through it
later from a block that never names it. A parameter the body does not follow
that a block may rebind is refused, and writing a member or an element of one,
taking its address, binding a reference to it or calling a member function on
it is rebinding it: a member is part of its object. No memory capability of the
contract holds
after a block, for a dereference or for a verified call. Control passes through a
block: a `return`, a `goto`, or a `break` or `continue` leaving it is refused,
and so is proof syntax inside it. A contract proven across a block is partial
correctness only, since nothing says the block terminates. What follows a block
is established only by explicit means (UNSAFE-004): a runtime check on the path
after it, or a claim that the path cannot occur, which may rest on a trusted
law.

An `unsafe` function is neither verified nor pure, however its declarations are
spelled, and states no contract; a verified body calls one only inside an unsafe
block, and a proposition cannot mention one. A `pure` function holding an unsafe
block is not pure. Unsafe functions are recognized at namespace scope; one
declared in a class is refused. Each refusal is a fixture of its own
(`negative_unsafe_boundary`).

The trust report lists every unsafe boundary a unit writes, each outermost block
with the verified function that holds it and each unsafe function once however
often it is declared, whether or not anything rests on it. Every claim that rests
on one is listed under `Unsafe-dependent claims` with each block, marked as in
its own body or reached through a verified call: a contract whose body or any
verified callee's body holds a block, and a claim that a path or a case of such a
body cannot occur. Such a claim is counted `relying on unsafe code` and is never
listed as assumption-free, whatever else about its function is proven
(`TRUST.md` TCB-REPORT-005). An unsafe block is not a trusted assumption and
adds none (`fixtures/unsafe_boundary.cpp`). An editor colors `unsafe` where the
compile of the unit recognized it.

---

# FFI status

| Boundary                     | Status        |
| ---------------------------- | ------------- |
| C                            | `SPECIFIED`   |
| Objective-C++                | `SPECIFIED`   |
| JNI                          | `SPECIFIED`   |
| N-API                        | `SPECIFIED`   |
| WASM imports/exports         | `SPECIFIED`   |
| Inline assembly              | `SPECIFIED`   |
| OS APIs                      | `SPECIFIED`   |
| Verified FFI contract format | `NOT STARTED` |
| FFI contract checker         | `NOT STARTED` |

Foreign code must not automatically count as verified.

---

# Proof erasure status

| Capability                       | Status        |
| -------------------------------- | ------------- |
| Proof erasure model              | `SPECIFIED`   |
| Ghost erasure model              | `IMPLEMENTED` |
| Dependent argument erasure       | `SPECIFIED`   |
| Erasure implementation           | `PROTOTYPE`   |
| Runtime-equivalence tests        | `PARTIAL`     |
| ABI-equivalence tests            | `PARTIAL`     |
| Formal erasure correctness proof | `NOT STARTED` |

Erasure currently removes law and trusted-law declarations, proofs, contracts,
loop invariants and measures, ghost declarations whole, the `verified`, `pure`
and `unsafe` specifiers and the `unsafe` keyword of a block, which leaves the
block's braces; reduces a
claim that a path cannot occur to the empty statement its `;` leaves; and lowers
a refinement type declaration to the alias it means. The implementation checks a
strong property rather than asserting success: the runtime program must be the
analysed program with every proof-only span blanked, keeping only its newlines,
each runtime-bearing declaration replaced by the canonical C++ recomputed from
that declaration, and nothing else changed, with line numbering unchanged
(`TRUST.md` 29). The text Clang compiles is written only after that check, from
the text checked. Equivalence is therefore established structurally for the
constructs implemented, not proven in general.

A preprocessor directive inside a C++L construct -- a `#pragma`, a `_Pragma`
operator's, a line marker after a long comment -- stays in the runtime program
where it stands, the validator refuses a runtime program that lost one, and the
analysed program is subject to it at the same point
(`tests/e2e/erasure_directives.sh`, `SPEC.md` `ERASE-017`). One inside an
expression or a statement C++L states is refused. `cppl-format` does not yet
keep a directive written inside a C++L declaration when it lays the declaration
out again, so the fixtures that hold one are excluded from the format check.

Erasure keeps every column as well as every line: a refinement's lowering is
padded to its declaration's length, a validation calls a validator whose name
fits where `validate<R>` was written, and the validator refuses a runtime
program in which code after a lowering moved; a refinement whose validator
cannot fit before code on its line is refused. The analysed program resumes
ordinary C++ after everything it generates at the line and column the program
has it at, so a position C++ observes, as a template argument included, is the
same in the program verified and the program run
(`tests/e2e/erasure_positions.sh`, `SPEC.md` `ERASE-018`). Columns are those of
the preprocessed text, which writes a run of whitespace between two tokens as
one; that is a known limitation for a unit using C++L (`COMPATIBILITY.md` 13).

The analysed text declares no Law where ordinary C++ can find it: each Law and
proof is projected into a formal namespace with a reserved name, which only
Laws and proofs look into (`SPEC.md` `LAW-008`). A Law that would otherwise be
the better overload, hide a function or satisfy a detection idiom changes
nothing the program verified, and a template is verified at the arguments the
program instantiates it at (`tests/e2e/erasure_equivalence.sh`,
`tests/negative/erasure.sh`). A Law is named only by an unqualified name in a
Law's or a proof's proposition, from the namespace it is declared in and every
namespace nested in that one, and from the namespace enclosing an unnamed or
inline one it is declared in; a qualified name such as `geo::area(x)` is
refused as naming nothing.

Nor does proof-only text instantiate what the program does not (`SPEC.md`
`ERASE-019`). A Law, a proof's statements, a contract or loop clause, a
refinement predicate, a ghost declaration, a claim or a case split that names,
calls or reads a template of the program's own, or a class with a member
template, or a standard template at one, or calls a function a template of the
program's shares a name with, is refused where it does so: a class template's
instantiation is kept for the rest of the unit, and a friend it defines changed
a detection idiom in the program verified alone. Standard templates, templates
of data alone and the program's own classes stay usable in all of them, and a
clause restates the parameters of a function it is defined with
(`tests/negative/proof_instantiation.sh`). The rule is conservative: a use of a
specialization the program has already instantiated before it is refused too.
The standard library is trusted not to change with where it is instantiated at
the program's types (`TRUST.md` `TCB-SOURCE-010`).

The check trusts the recognizer's spans. What shows a span wrong is comparison
with programs written without C++L: each construct family has a C++L fixture and
its erasure written by hand, and the two must print the same and compile to
identical assembly at `-O0` and `-O2` in `c++17`, `c++20` and `c++23`
(`tests/e2e/erasure_equivalence.sh`). ABI equivalence is tested the same way for
a library whose interface uses refinements and contracts, and by linking an
ordinary C++ client, compiled by Clang alone, against it
(`tests/e2e/abi_equivalence.sh`). Both are `PARTIAL`: they cover the constructs
this implementation accepts, on the host's ABI. Units that use one another's
contracts through verification interfaces compile to exactly the code of their
erasures written by hand, and each links with the other's plain C++
(`tests/e2e/cross_tu.sh`): an interface is never compiled and reaches no object.

C++L's intended mature pipeline is:

```text
verified C++L
    ↓
proof erasure
    ↓
ordinary C++
    ↓
Clang / LLVM
```

---

# Runtime status

C++L does not intend to introduce a mandatory theorem runtime.

| Runtime feature                  | Status        |
| -------------------------------- | ------------- |
| Dedicated proof VM               | `NOT PLANNED` |
| Runtime theorem checker          | `NOT PLANNED` |
| Mandatory C++L garbage collector | `NOT PLANNED` |
| Mandatory alternate runtime      | `NOT PLANNED` |
| Runtime refinement validation    | `IMPLEMENTED` |
| Ordinary C++ execution           | `SPECIFIED`   |
| Clang/LLVM native output         | `SPECIFIED`   |

Proofs should normally disappear before runtime.

Runtime refinement validation is ordinary C++ and nothing else. A condition the
program writes is kept byte for byte, and a crossing it selects is proven, not
checked. A validation expression the program writes lowers to a call of a
validator its refinement's declaration lowers to, in the same translation unit,
and is kept; no library, runtime support or check the program did not request
is added (`SPEC.md` RUNTIMECHECK-001, RUNTIMECHECK-009, RUNTIMECHECK-021,
ERASE-012, ERASE-013). Which claims rest on a validation is reported (Refinement
status).

---

# Automation status

| Capability                       | Status        |
| -------------------------------- | ------------- |
| Definitional-equality strategy   | `PROTOTYPE`   |
| `proof auto`                     | `SPECIFIED`   |
| Simplification                   | `SPECIFIED`   |
| Rewriting                        | `PROTOTYPE`   |
| Arithmetic automation            | `PROTOTYPE`   |
| Contradiction solving            | `PROTOTYPE`   |
| Induction tactic                 | `SPECIFIED`   |
| cvc5 integration                 | `NOT STARTED` |
| Z3 integration                   | `NOT STARTED` |
| Proof certificates               | `PROTOTYPE`   |
| Independent certificate checking | `PROTOTYPE`   |
| Counterexample extraction        | `NOT STARTED` |

Automation must not weaken the meaning of `PROVEN`.

Rewriting, arithmetic and contradiction solving are automatic evidence for
contracts and for Laws without a written proof; there is no proof statement that
invokes them yet. Proof certificates exist for linear arithmetic only: automation
finds them by Fourier-Motzkin elimination, and the kernel checks them against a
system it states itself. No external solver is involved or trusted.

---

# AI integration status

| Capability                   | Status        |
| ---------------------------- | ------------- |
| AI-oriented proof workflow   | `SPECIFIED`   |
| Structured proof goals       | `SPECIFIED`   |
| Machine-readable diagnostics | `SPECIFIED`   |
| Counterexample feedback      | `SPECIFIED`   |
| AI patch loop                | `NOT STARTED` |
| AI-generated proof checking  | `SPECIFIED`   |
| AI as trusted authority      | `NOT PLANNED` |

AI output must always be independently verified.

---

# Developer tooling status

| Tool                                 | Status        |
| ------------------------------------ | ------------- |
| Clang-compatible driver              | `PROTOTYPE`   |
| `cppl build`                         | `SPECIFIED`   |
| `cppl check`                         | `SPECIFIED`   |
| `cppl prove`                         | `SPECIFIED`   |
| `cppl explain`                       | `SPECIFIED`   |
| `cppl trust-report`                  | `PARTIAL`     |
| LSP: sync and diagnostics            | `PARTIAL`     |
| LSP/CLI: canonical clause formatting | `PROTOTYPE`   |
| LSP: case completion and hover       | `PROTOTYPE`   |
| LSP: C++ and C++L completion         | `PROTOTYPE`   |
| LSP: signature help                  | `PROTOTYPE`   |
| LSP: hover over C++ and C++L names   | `PROTOTYPE`   |
| LSP: verification status in editors  | `PROTOTYPE`   |
| LSP: code actions                    | `PROTOTYPE`   |
| LSP: semantic tokens (all names)     | `PROTOTYPE`   |
| LSP: definition and declaration      | `PROTOTYPE`   |
| LSP: references and highlights       | `PROTOTYPE`   |
| LSP: document outline                | `PROTOTYPE`   |
| LSP: folding and selection ranges    | `PROTOTYPE`   |
| LSP: build flags (compile_commands)  | `PROTOTYPE`   |
| LSP: imported verification interfaces | `PROTOTYPE`  |
| LSP: inlay hints                     | `PROTOTYPE`   |
| LSP: background compiles, cancel     | `PROTOTYPE`   |
| LSP: workspace index and symbols     | `PROTOTYPE`   |
| LSP: rename                          | `PROTOTYPE`   |
| IDE proof goals                      | `PROTOTYPE`   |
| Proof navigation                     | `PROTOTYPE`   |
| Counterexample UI                    | `NOT STARTED` |
| Structured diagnostics               | `PROTOTYPE`   |
| Installed package and archive        | `PARTIAL`     |

`cmake --install` installs `cppl`, `cppl-lsp` and `cppl-format`, relocatable
and naming no file of the source or build tree; the canonical formatting style
is built into the formatter. An installation uses the LLVM it was built against,
found by the paths recorded at build time (`docs/INSTALL.md`). The release
archive holds exactly the install, with its SHA-256 beside it.
`integration_installed_package` installs to a fresh prefix, moves it, and from
an environment with nothing but a minimal `PATH` prints the release record,
compiles ordinary C++ and verified C++L, has a false Law refused, formats a
file and drives `cppl-lsp` through `initialize`, a refused document,
`shutdown` and `exit`. It is `PARTIAL` because only Linux x86_64 with LLVM 22
has been installed and tested; no archive is produced for another platform.

The driver is Clang-compatible rather than subcommand-based: `cppl` takes the
arguments `clang++` takes. Trust reporting exists as `--cppl-trust-report`; the
subcommand forms above are not implemented. The report counts partial-
correctness contracts and loop-invariant obligations separately, lists every
proven claim with the content identity of what it states, apart by whether it
rests on trusted laws and, if so, with each law it rests on, and lists every
trusted law with its identity and the ones nothing rests on.
`--cppl-emit-trust-report=<file>` writes the same report as a JSON document
(`DEVELOPER_GUIDE.md` 12.2). Neither prints evidence hashes (`TRUST.md` 36.1).

`cppl-lsp` implements `initialize`, `shutdown`, `exit`, incrementally synced
`textDocument/didOpen`, `didChange` and `didClose`, and
`textDocument/publishDiagnostics`. Diagnostics come from the ordinary compile
pipeline over the live buffer (`driver::compile_buffer`), so the server has no
decomposition, exhaustiveness or verification engine of its own; a structural
linter adds contextual C++L checks over the same recognized syntax rather than
re-recognizing it. The buffer compile names the buffer by the document's own
path, so a diagnostic is shown where it was written, at the column its author
wrote it at, and one located in an included header is shown on the document's
`#include` that brought the header in, with the header's location as related
information. Compiles run in the background, a change's once typing pauses for
300 ms, so requests are answered while one runs. A compile of text since edited
is dropped. A request withdrawn while it waits its turn is answered as
cancelled, each compile is reported as work in progress to a client that shows
it, and after a compile the client is asked to fetch its code lenses and
semantic tokens again. Each document is compiled, and read by its editor unit,
with the
flags its build gives it in the nearest `compile_commands.json`, followed by
the server's `--clang-arg` flags, and with the verification interfaces that
entry imports, checked as the CLI checks them. A quoted `#include` is looked for beside the
document. Transport is separate from analysis, and the library is tested
without an editor. The server also advertises
`documentFormattingProvider`, `documentRangeFormattingProvider` and
`documentOnTypeFormattingProvider`, backed by one shared `compiler/formatter`
engine that also backs the standalone `cppl-format` CLI: `expects`, `ensures`,
`invariant` and `proves` clauses are relocated onto their own canonically
indented line, ordinary C++ layout is delegated to `clang-format`, and a
`check_style` pass reuses the same clause-placement rule to add style warnings
to `publishDiagnostics`. `codeActionProvider` serves the same engine's syntax
migrations as quick fixes where their edits land, and canonical formatting as
`source.fixAll.cppl`, computed only when asked for rather than as the cursor
moves.

Every diagnostic carries a category, which the CLI prints in brackets and the
editor shows as the diagnostic's code. Only Clang's own diagnostics are
`cpp-semantic`. The Clang bridge gives each refusal of its own a category too:
C++ that Clang accepted and that lies outside the modeled fragment, such as an
ordinary function returning a refined value, refined storage built outside a
verified body, a refinement written as a template argument or proof-only text
that may instantiate a template (`ERASE-019`), is `unsupported-semantics`; a
refinement whose application it could not resolve is `elaboration`; and a
failure of libclang's own queries is `internal`
(`negative/diagnostic_categories.sh`).

`completionProvider` and `hoverProvider` are advertised and serve C++L's own
syntax: inside a `cases`/`decompose` arm block, completion offers each state
the subject's provider lists that the statement has no arm for yet — the
residual state included, since it is a real semantic state and not a catch-all
— and each item inserts an arm carrying the provider's own binder names. Hover
names the subject's resolved representation, its provider, and the full
partition with written arms marked; an omitted case is marked as claimed
impossible, not as proven. Both read the states the compiler's case
engine recorded while elaborating the buffer (`elaboration::SubjectStates`),
so the server still has no decomposition or exhaustiveness engine of its own;
where the compiler has not confirmed a subject's states, they offer nothing
rather than guess. Elaboration runs on publish rather than per keystroke, so
offered labels may lag the buffer by one edit. A case split in a verified body
is served the same way, from the states the compiler recorded while elaborating
the body.

Outside a case block, completion offers what Clang would accept at the position,
matched against what has been typed and ranked by Clang's own priority, with a
call's parameters as snippet placeholders for a client that takes snippets, and
never a name the projection generated. C++L's own words are offered as snippets
where the compiler's recognizer says they may be written: declarations at
namespace scope, proof statements at a statement's start, the assumptions in
scope, trusted Laws and proofs a statement can name after `exact`, `apply`,
`rewrite` or `contradiction`, and the clauses a declaration may still take.
The recognizer reads text still being written through its draft mode, which
keeps a Law or a proof not yet written whole and reads past a statement it
cannot read; the server reads no C++L grammar of its own. A draft has no
authority (ARCH-LSP-007): nothing only a draft keeps is offered as evidence,
and elaboration refuses a syntax that holds any of it.

`signatureHelpProvider` shows, while a call's arguments are written, every
declaration Clang says the call could resolve to, with the argument being
written marked.

`documentSymbolProvider` outlines the document: every declaration Clang finds
whose name the document writes outside a function body, nested as declared,
with each Law, proof and refinement type the compiler's recognizer finds placed
among them where it is written. A declaration the projection generated is never
in it. A client that cannot nest an outline gets a flat list naming each
entry's container.

`foldingRangeProvider` folds what Clang parsed:

- each body between its braces;
- each run of `#include`s, as imports;
- each branch of a conditional directive, as a region, paired from the
  directives Clang lexed.

It also folds each C++L proof body, arm block, arm body, bodiless Law and
refinement type that the recognizer found, and each comment block or run of
whole-line comments that the frontend's lexer passed over.

`inlayHintProvider` labels each argument with the name of its parameter and
each variable declared `auto` with its deduced type, from Clang, where the text
was written. An argument that already spells the name, a default argument, an
overloaded operator's operands and a call a macro's body writes get none.

`selectionRangeProvider` grows a selection from the token under the cursor. It
goes through each construct Clang parsed and each C++L span the recognizer
recorded: clause, statement, arm, body, declaration. The server reads no
structure from the text itself.

Outside a case block, hover describes any name. For C++ it shows what Clang
reports: the declaration's kind and qualified name, the declaration without a
body, a variable's type, a constant's value where Clang evaluates it, a type's
size and alignment, the declaration's comment, and the file it is declared in.
For a name that stands for a C++L declaration -- a Law, a proof, a refinement
type, a verified function, a name `assume` binds -- it shows the declaration as
written rather than what the projection generated for it. `result` and `self`
are explained, not shown as the generated parameters they are to Clang.

`codeLensProvider` states, over each Law, proof and verified function the
document writes, what became of its obligations in the last compile, and hover
over any of them lists each obligation's status, goal, trusted premises and
evidence, or why it is not proven. The compile copies these out of the
verdicts after the kernel has decided (`driver::ObligationRecord`), so an
editor says `PROVEN` only where the trust report would. It also names, beside a
PROVEN verdict, everything the report says the claim rests on, wherever that
arrives from. That means its trusted laws, the imported contracts, the library
models, the unsafe blocks and the runtime validations, including those that
come through a proof it uses or a verified function it calls (`TRUST.md` 36.3).
A verdict is shown only for the buffer version it was computed for. This is the goal of each
obligation, not an interactive proof state: the goal a proof has reached after
each of its statements is not reported. Counterexamples are not reported
because nothing in the compiler produces one.

`semanticTokensProvider` colors every name by what it names.

- **C++ names come from Clang.** The types are namespace, type, class, struct,
  enum, type parameter, parameter, variable, field, enumerator, function,
  method and macro. The modifiers are declaration, readonly, static member,
  deprecated and system-header.
- **C++L comes from the recognizer.** Every C++L word is a keyword. The names of
  Laws, proofs and refinement types are declared, and so is each name `assume`
  binds. The names proof statements use are colored as the compile resolved
  them.

Positions are the ones recorded in the buffer as written, and a name inside a
Law's proposition is where the Law writes it. This includes the proof
statements the editors' TextMate grammar cannot tell from C++ declarations,
such as `exact h;` and `contradiction name;`. A `contradiction` statement in a
verified body is reported only when the compile
of the whole unit, headers included, also recognized claims, since a header
that names `contradiction` makes the statement ordinary C++ (WORD-002). A case
split in a verified body, with the keywords of its arms, is reported on the same
terms, when the compile of the whole unit recognized splits (WORD-012).

`definitionProvider`, `declarationProvider`, `typeDefinitionProvider` and
`implementationProvider` are answered by Clang, through libclang, over an
editor unit per document: the analysis projection the compiler makes, made from
the buffer as written so that its `#include`s stay directives, kept with a
precompiled preamble and reparsed after an edit. A header that holds C++L, and
every other open buffer, is read as its projection. The projection records
where it kept written text in place and where it copied written text into a
declaration it generated, so a name inside a Law's proposition, a Law's
parameter, a parameter named in a contract, a Law named by a proof's `proves`
clause and a refinement type all lead to what was written; a position Clang
reports in generated text that stands for nothing written is not shown.

A name a proof statement uses (`exact p;`, `apply p;`, `rewrite h;`,
`contradiction e;`, the evidence of an omitted case) is not C++. Elaboration
records what it resolved each one to, a proof, a trusted Law or an `assume`
binding, and where that is declared (`elaboration::ResolvedName`), as a
byproduct nothing in the compiler reads; the server navigates and lists
references from those records, and only where the buffer still spells the name
where it was recorded.

`referencesProvider` and `documentHighlightProvider` are answered from the same
units: every open document's unit reports where a name, identified by Clang's
USR, is written in it and in the headers it includes, and a parameter the
projection repeats in several generated declarations counts as the one the
author wrote. Highlights mark declarations, reads and writes.

A workspace index (`lsp::WorkspaceIndex`) reads every other file of the folders
the client opened, and each file a compilation database there lists. It reads
each as an open document of it would be read, several at once at a lower
priority than the editor's requests: by Clang through its projection with its
build's flags, and by the compile as far as elaboration. Files edited, added or removed on disk are read again within 2
seconds. References reach those files, and `workspace/symbol` lists every
declaration an outline would show in the open documents and the index. An open
document always answers as the editor holds it (ARCH-LSP-009), and each pass
that reads files is reported as progress.

Rename (`textDocument/prepareRename`, `textDocument/rename`) rewrites every
place references finds, declarations and a class's constructors and destructor
included, whole or not at all. It refuses, with the reason, a new name that is
no C++ identifier or is a keyword, a place in a file neither open nor indexed,
a place that no longer spells the name, a use a macro's body spells, a C++L
word written inside C++L, and any edit after which the recognizer reads a
file's C++L differently (ARCH-LSP-010). Collisions with other declarations are
not checked; the compile reports them.

---

# Standard library verification status

| Area                       | Status        |
| -------------------------- | ------------- |
| Primitive types            | `NOT STARTED` |
| `std::array`               | `PARTIAL`     |
| `std::span`                | `PARTIAL`     |
| `std::optional`            | `NOT STARTED` |
| `std::variant`             | `NOT STARTED` |
| `std::vector`              | `PARTIAL`     |
| `std::string`              | `PARTIAL`     |
| Smart pointers             | `NOT STARTED` |
| Standard algorithms        | `NOT STARTED` |
| libc++ specification layer | `NOT STARTED` |

`std::array`, `std::vector`, `std::string` and dynamic-extent `std::span` are
`PARTIAL`: verified code uses them through the subset `SPEC.md` J.17 states
(RFC 0020). A `vector`, `string` or `span` is an abstract value whose one
observation is its length; `size()`, `length()` and `empty()` read it in a body
and in a contract alike. Its elements are places of the storage it owns or
views, and every subscript owes `i < size()`, proved by the kernel, where the
element place is formed. `std::array` is its `N` element places, as `T[N]` is.
Construction (default, a list, a count, a fill, a string literal, a copy, a
move), `push_back`, `pop_back`, `clear`, `reserve`, `append`, `+=` and
assignment are trusted library summaries over the length, supposed at the call
as a callee's postcondition is; `pop_back` owes a non-empty vector. A
refinement written as a template argument is its base type in the C++ type, so
`std::vector<Positive>` is `std::vector<unsigned>`; written as the element type
of a `vector` local it is a content invariant of that local's storage
(`SPEC.md` STDMODEL-020). A value entering an element of such a local owes the
predicate, a copy or move into it owes it of every source element, and a read
supplies it only for a local whose writes were all modeled. A refined container
is never handed to a call that may write it: not by mutable reference, not
through a writable span or data pointer.

Every operation that may reallocate, shrink, replace or move from a
container's storage gives it a new storage generation (an element write does
not, and the end of the owner's lifetime is not a generation but the end of
every view, which the scope rules for spans enforce): an element place formed
before is not matched again, and a span local or element reference formed
before is refused where it is used after, naming the operation. A loop that may
do this gives the container a fresh generation at its head. A span parameter
needs `readable(s)` or `writable(s)`; a verified call owes it, and refuses a
span or `data()` of a container it also passes by mutable reference, and an
element it also passes by mutable reference beside a writable span or `data()`
of the same container. `data()` is modeled only as such a capability argument,
over the length; a capability over zero elements says nothing of its pointer,
and `writable` of `const` elements is refused where it is stated.

Every claim resting on a function that uses a container, directly or through a
call, is listed under `Library-model-dependent claims` and never counted
assumption-free. Across translation units the list is complete: a verification
interface (format version 2 onwards) records with each contract the models its proof
used, in its declaration, its body and its callees, and every unit that
imports it carries them on, so a claim proven through an imported contract
names each of them with the record it arrived through (`TRUST.md`
TCB-LIB-010). An interface of the earlier format, which could not say, is
refused, and so is one naming a model this compiler does not have. Iterators, range-`for`, `at`, `front`, `insert`, `resize`,
`emplace_back`, `subspan`, `std::string_view`, static-extent spans, custom
allocators, `std::vector<bool>`, element types other than integers and `bool`,
and refined element types anywhere but a `vector` local (parameters, results,
spans and `std::array`) are refused. A `std::array` a reference designates, or
one a member of the implicit object holds, is its `N` element places, each
caller storage, read and written at a term within its extent; a clause reads
its element as the parameter's at the state the clause describes. An element or a dereference read in an `if` or loop condition, or in
an arm of a `?:`, `&&` or `||` that is returned, declares one local or is
assigned, or that stands inside such a statement's value, a call statement or
a returned value as an operand or a call's argument, is formed where it is
evaluated, and owes its bound or capability only on the routes that evaluate
it (`e2e/conditions.sh`). A container handed to a verified call by value is copied into the
parameter, and the caller's is unchanged. A signed index is bounded as the
size-type value C++ converts it to, and dividing by a length owes it non-zero,
through the machine-arithmetic rules (`SPEC.md` ARITH-008, ARITH-009). The same
claims verify against libc++ and libstdc++, and the erased program is the same
code as its hand-erased twin (`tests/e2e/containers.sh`).

---

# Concurrency status

| Capability                          | Status        |
| ----------------------------------- | ------------- |
| Sequential semantics                | `SPECIFIED`   |
| Thread model                        | `NOT STARTED` |
| Atomic model                        | `NOT STARTED` |
| Memory-order reasoning              | `NOT STARTED` |
| Race-freedom proofs                 | `NOT STARTED` |
| Concurrent invariants               | `NOT STARTED` |
| Verified synchronization primitives | `NOT STARTED` |

Until concurrency semantics exist, concurrency must not be silently treated using sequential reasoning.

---

# Proof caching status

| Capability                            | Status        |
| ------------------------------------- | ------------- |
| Incremental verification architecture | `SPECIFIED`   |
| Proof dependency graph                | `NOT STARTED` |
| Content-addressed proof cache         | `NOT STARTED` |
| Semantic invalidation                 | `NOT STARTED` |
| Deterministic proof artifacts         | `NOT STARTED` |
| Cross-build proof reuse               | `NOT STARTED` |

---

# Trust model status

| Capability                  | Status        |
| --------------------------- | ------------- |
| Explicit TCB model          | `PROTOTYPE`   |
| Small-kernel architecture   | `PROTOTYPE`   |
| Hidden axioms forbidden     | `PROTOTYPE`   |
| Trust transitivity          | `IMPLEMENTED` |
| Solver trust reporting      | `SPECIFIED`   |
| FFI trust reporting         | `NOT STARTED` |
| Per-Law assumption closure  | `IMPLEMENTED` |
| Trust-report implementation | `PARTIAL`     |

The TCB is stated in `TRUST.md` 4 and 5. There are no axioms. A
`trusted law` (`SPEC.md` 27) is the one implemented way to admit a proposition
without proof: it is stated to the formal core, given status `TRUSTED` rather
than `PROVEN`, never counted among proven laws, and named individually in the
trust report with its source location. Declaring a law trusted and also writing
a proof for it is refused. A build with no such declaration reports zero trusted
axioms, because that is true of it. The trust report prints counts it can
substantiate, and says _not analysed_ where C++L does not yet look.

Transitivity and per-claim closure are described under
[Unsafe and trusted boundary status](#unsafe-and-trusted-boundary-status). The
closure of a claim is the set of premises the kernel checked it relative to,
joined across verified calls; the reporting code that computes and prints it is
reporting TCB (`TRUST.md` TCB-REPORT-006) and fails the build rather than
report a claim it cannot account for.

See [TRUST.md](./TRUST.md).

---

# Security status

| Capability                                           | Status        |
| ---------------------------------------------------- | ------------- |
| Proof-soundness issues classified as security issues | `SPECIFIED`   |
| Responsible disclosure policy                        | `SPECIFIED`   |
| Kernel fuzzing                                       | `PROTOTYPE`   |
| Parser fuzzing                                       | `PROTOTYPE`   |
| Proof-certificate fuzzing                            | `PROTOTYPE`   |
| Erasure fuzzing                                      | `PROTOTYPE`   |
| Reproducible release metadata                        | `PARTIAL`     |

See [SECURITY.md](../SECURITY.md).

The fuzzing rows are `PROTOTYPE`: each is a libFuzzer target that CI's Fuzzing
job searches under AddressSanitizer and UndefinedBehaviorSanitizer and that
every build replays over its corpus (`tests/fuzz`, `docs/CI.md`, "Fuzzing").
Kernel fuzzing is `kernel_proof`, `kernel_arithmetic` and `kernel_terms`, whose
oracle is the independent model of the core described under [Proof system
status](#proof-system-status); proof-certificate fuzzing is `kernel_certificate`,
which puts integer systems and certificates decoded from bytes to the checker
and searches for a point that a certificate it accepted should have excluded.
Parser and erasure fuzzing are `frontend`: lexing tiles the input, projection
is deterministic and only blanks characters, and erasure of what recognition
accepted only deletes text, keeps every line and reports no error. That is a
structural check of erasure, not of runtime equivalence, which the erasure
equivalence fixtures test (Proof erasure status). A target that finds nothing
proves nothing.

`cppl --cppl-version` is the release record (`docs/INSTALL.md`): the compiler
version, the Git commit, tag and tree state it was built from, the compiler
that built it, the verification semantics and their digest, the kernel, formal
core and interface format versions, the libclang that analyses and the Clang
driver that compiles, and the target, language mode and standard library that
driver selects for the arguments given. It names no path and no time, and a
driver that cannot be asked makes it fail
(`tests/integration/release_metadata.sh`). Release archives carry a SHA-256
file each, checked by `integration_installed_package` and `tools/release.sh`.
It is `PARTIAL`: the archive's own bytes are not reproducible, since it records
file times, and nothing signs an archive outside GitHub's attestation step,
which has not been run here.

---

# Documentation status

| Document                   | Status        |
| -------------------------- | ------------- |
| `README.md`                | `SPECIFIED`   |
| `SPEC.md`                  | `SPECIFIED`   |
| `GRAMMAR.md`               | `SPECIFIED`   |
| `DESIGN.md`                | `SPECIFIED`   |
| `FOUNDATIONS.md`           | `SPECIFIED`   |
| `TRUST.md`                 | `SPECIFIED`   |
| `ACKNOWLEDGEMENTS.md`      | `SPECIFIED`   |
| `ROADMAP.md`               | `SPECIFIED`   |
| `COMPATIBILITY.md`         | `SPECIFIED`   |
| `SECURITY.md`              | `SPECIFIED`   |
| `CONTRIBUTING.md`          | `SPECIFIED`   |
| `STATUS.md`                | `SPECIFIED`   |
| RFC process                | `SPECIFIED`   |
| Formal semantics reference | `NOT STARTED` |
| C++ memory-model reference | `NOT STARTED` |
| Kernel reference           | `SPECIFIED`   |
| Erasure reference          | `NOT STARTED` |

---

# What C++L does not currently claim

Until implementation reaches the appropriate status, C++L does **not** claim:

- production readiness
- verified C++ compatibility
- a completed proof kernel
- a completed dependent type checker
- completed theorem proving
- completed refinement solving
- sound C++ pointer verification
- sound concurrency verification
- complete verified machine arithmetic (the subset of RFC 0019 is
  implemented; shifts, bitwise operators and several conversions are not, see
  [Machine arithmetic status](#machine-arithmetic-status))
- verified standard-library implementations (the container models are trusted
  summaries, `TRUST.md` 28.1)
- completed proof erasure
- completed Clang integration
- completed SMT integration
- a stable language specification
- a stable ABI
- a stable proof artifact format

Documentation of a feature is not evidence that the feature has been implemented.

---

# What may be demonstrated early

Early prototypes may demonstrate narrow vertical slices such as:

```text
law
    ↓
proof obligation
    ↓
kernel
    ↓
PROVEN
```

or:

```text
refinement
    ↓
SMT
    ↓
checked evidence
```

or:

```text
C++L
    ↓
erase proof syntax
    ↓
C++
    ↓
clang++
```

Such prototypes should be marked `PROTOTYPE` rather than presented as broad language support.

The first of these now exists: see _What the current implementation does_ above.
It is marked `PROTOTYPE` throughout this document, and the fragment it verifies
is stated explicitly rather than implied.

---

# Promotion rules

A feature should move from:

```text
SPECIFIED
→
PROTOTYPE
```

when executable implementation work exists.

A feature should move from:

```text
PROTOTYPE
→
PARTIAL
```

when the core implementation works but defined semantics remain incomplete.

A feature should move from:

```text
PARTIAL
→
IMPLEMENTED
```

only when:

- required semantics are implemented
- positive tests pass
- negative tests pass
- regression tests exist
- diagnostics are adequate
- trust impact is documented

A feature should move from:

```text
IMPLEMENTED
→
VERIFIED
```

only when the project's additional formal verification requirements for that component have been satisfied.

---

# No silent status promotion

Features must not be marked `IMPLEMENTED` merely because:

- a demo works
- a test passes
- an AI generated working code
- a solver returned `SAT` or `UNSAT`
- Clang accepted generated C++
- one proof example succeeded

Status reflects the defined semantics, not the best-case example.

---

# Current release policy

Before the first stable release, version numbers should communicate experimental status.

Examples:

```text
0.1.0
0.2.0
0.x
```

A `1.0.0` release should mean that the project's declared V1 guarantees are implemented and sufficiently stable to be relied upon.

---

# V1 target

C++L V1 should provide a coherent end-to-end implementation of:

```text
formal Laws
+
proofs
+
dependent/indexed relationships
+
refinement types
+
equality
+
induction
+
termination
+
contracts
+
explicit trust boundaries
+
C++ safety obligations
+
proof erasure
+
ordinary native C++ output
```

The implementation may initially support only a defined subset of difficult C++ constructs inside verified regions.

That subset must be explicit. It is: see [Verified C++ subset status](#verified-c-subset-status) and RFC 0022.

Unsupported C++ may remain executable as ordinary/unverified C++.

It must never silently count as formally verified.

---

# V1 closure

This section is the release-gate record for V1. `ROADMAP.md`, "V1 release
gates", states each gate's pass condition and points here; this section gives
each gate's status, the evidence behind it and what is still open. A gate's
status judges the behavior V1 requires, within the scope RFC 0022 fixes. The
rows elsewhere in this file judge the full specified semantics, which is why a
row may read `PROTOTYPE` or `PARTIAL` while its gate reads `IMPLEMENTED`: the
row covers semantics V1 does not claim, such as induction over the `@` domains,
or semantics not yet frozen. A gate is met only when its status is
`IMPLEMENTED` or `VERIFIED` and its pass condition holds.

**V1 is not met.** Gates G17 and G18 are open, and the project stays
**Production ready: No**.

| Gate | What it covers | Status | Met |
| --- | --- | --- | --- |
| G1 | C++L syntax | `IMPLEMENTED` | yes |
| G2 | Laws and kernel-checked proofs | `IMPLEMENTED` | yes |
| G3 | Dependent and indexed relationships | `IMPLEMENTED` | yes |
| G4 | Refinement types, static and runtime-checked construction | `IMPLEMENTED` | yes |
| G5 | Equality | `IMPLEMENTED` | yes |
| G6 | Induction | `IMPLEMENTED` | yes |
| G7 | Termination | `IMPLEMENTED` | yes |
| G8 | Contracts, within and across translation units | `IMPLEMENTED` | yes |
| G9 | Explicit unsafe and trusted boundaries | `IMPLEMENTED` | yes |
| G10 | C++ safety semantics of the verified subset | `IMPLEMENTED` | yes |
| G11 | Proof erasure and native output | `IMPLEMENTED` | yes |
| G12 | Trust reporting | `IMPLEMENTED` | yes |
| G13 | Kernel assurance: mechanized model | `IMPLEMENTED` | yes |
| G14 | Kernel and verifier assurance: adversarial testing | `IMPLEMENTED` | yes |
| G15 | Correspondence TCB | `IMPLEMENTED` | yes |
| G16 | Artifact provenance | `IMPLEMENTED` | yes |
| G17 | Stability of the specification and the proof system | `PARTIAL` | no |
| G18 | Delivery: platforms, ABI and release | `PARTIAL` | no |

Post-V1 by decision, each refused wherever it would be used, so none can
silently count as verified:

- existential quantification and its proof surface (`SPEC.md` 9);
- proof `let`;
- the proof-only `@` domains and conversions into them;
- induction over them and over pointer structures;
- kernel type families;
- shifts and bitwise operators;
- the constructs RFC 0022 lists as refused;
- a serialized proof-term format and evidence identity in the trust report.

None of these is in the V1 release criteria. Each would enlarge the kernel or
the trusted computing base, and a later RFC adds it with its kernel change and
mutation-tested rules.

## V1 closure: syntax (G1)

`IMPLEMENTED`. `GRAMMAR.md` is the normative grammar of the V1 surface, and the
recognizer implements it with the C++-first contextual-word rule (WORD-001 to
WORD-013): a program that uses a C++L word as a C++ name keeps its C++ meaning,
and is warned.

Evidence:

- `conformance_contextual_identifiers`, `conformance_readme_examples` and
  `conformance_guide_examples`;
- `unit_recognizer_test`;
- the `fuzz_frontend` corpus;
- the mutation entries `declarator-list-ends-clauses` and
  `verified-specifier-span`.

## V1 closure: Laws and kernel-checked proofs (G2)

`IMPLEMENTED`. Every PROVEN claim comes from a kernel acceptance of exactly its
goal (`TRUST.md` 36.3). The kernel checks 15 rules and adds no axiom
(cppl-kernel-0.9.0, cppl-core-0.9.0, `KERNEL.md`).

Evidence:

- tests: `kernel_check_test`, `kernel_malformed_test`, `kernel_adversarial_test`,
  `kernel_substitution_property_test`, `kernel_evidence_regression_test`,
  `e2e_valid_law`, `e2e_invalid_law` and `negative_written_proofs`;
- mutation entries: one for each rule's check.

## V1 closure: dependent and indexed relationships (G3)

`IMPLEMENTED` within RFC 0022:

- indexed refinements applied at a constant;
- contracts and propositions whose meaning depends on parameters, including
  quantified and implicational ones;
- indexed observation with its bound as a separate obligation (RFC 0016).

Kernel type families are post-V1. Evidence: `e2e_refinement_types`,
`e2e_quantified_propositions`, `negative_quantified_propositions`,
`e2e_safety_subset`, and `kernel_paths_test`.

## V1 closure: refinement types (G4)

`IMPLEMENTED`. A value enters a refinement type only on a static proof, which
may use the facts of its path and adds no runtime code (RUNTIMECHECK-010).
Otherwise it enters only through an explicit validation `validate<R>(e)`, whose
validator the program keeps and whose site the claim names
(RUNTIMECHECK-011 to RUNTIMECHECK-021, RFC 0021). Anything else is refused.

Evidence: `e2e_refinement_types`, `e2e_refinement_flow`,
`e2e_runtime_validation` and `negative_runtime_validation`; the matrices of
`e2e_runtime_validation_matrix` (a validation in every admitted position, 21
contracts run on valid, invalid and boundary input) and
`negative_runtime_validation_matrix` (46 refusals: every route, write, call,
alias and loop after which the fact no longer holds, every position where no
program runs it, every predicate no program can evaluate soundly); and the
`validation-*` and `runtime-check-*` mutation entries.

## V1 closure: equality (G5)

`IMPLEMENTED`. Covered: definitional equality by the kernel's normalization,
propositional `Eq<T>`, and `rewrite` by capture-safe substitution. Evidence:
`e2e_formal_equality`, `negative_formal_equality` and `e2e_rewritten_proof`,
and the `transport-result` and `substitution-capture` mutation entries.

## V1 closure: induction (G6)

`IMPLEMENTED` over unsigned machine integers, which is the V1 scope (RFC 0022).
It covers the short form and explicit arms. It is checked by the kernel's
induction rule, with the range premise in the successor arm (INDUCT-001 to
INDUCT-005). Every other subject is refused.

Evidence: `kernel_induction_test`, `e2e_induction` and `negative_induction`.

## V1 closure: termination (G7)

`IMPLEMENTED`, as described under [Termination status](#termination-status).
Evidence: `e2e_termination`, `negative_termination`, and the mutation entries
`recursion-needs-measure`, `recursive-call-descent-owed`,
`totality-through-callees` and `totality-unmeasured-loop`.

## V1 closure: contracts (G8)

`IMPLEMENTED` for the verified subset:

- preconditions and postconditions;
- paths, locals, loops with invariants, and verified calls;
- non-virtual member functions (RFC 0018);
- contracts across translation units through verification interfaces
  (RFC 0017), with cycles across units refused.

Evidence: `e2e_verified_*` and `negative_verified_*`, `e2e_cross_tu`,
`negative_cross_tu`, `e2e_integration_ledger` and `e2e_cross_feature`, and the
`xtu-*` mutation entries.

## V1 closure: unsafe and trusted boundaries (G9)

`IMPLEMENTED`. See [Unsafe and trusted boundary status](#unsafe-and-trusted-boundary-status)
and `TRUST.md` 36.3. Evidence: `e2e_unsafe_boundary`,
`negative_unsafe_boundary`, `e2e_trusted_assumptions`, `e2e_trust_closure`
and `negative_trusted_dependencies`, and the `unsafe-*` mutation entries.

## V1 closure: C++ safety semantics (G10)

`IMPLEMENTED` for the explicit subset: RFC 0022 and
[Verified C++ subset status](#verified-c-subset-status). All 151 constructs of
Annex X are classified: 94 are verified, each with a refused twin, and 57 are
refused, each with its diagnostic.

Evidence:

- defined behavior of arithmetic (RFC 0019);
- memory capabilities, aliasing and lifetime of scalars and the sequence subset
  (RFC 0014, RFC 0020), including the adversarial storage and bounds attacks
  of `negative_sequence_attacks`, and the exhaustive matrices of
  `negative_sequence_generations` and `negative_sequence_boundaries` (316
  refusals) with their 328 accepted twins, each run to its stated value;
- a refused twin for every accepted fixture (`negative_refused_twins`).

## V1 closure: proof erasure and native output (G11)

`IMPLEMENTED`. The runtime text is checked against the analysed text with every
proof-only span blanked and every lowering recomputed (`TRUST.md` 29). It is
compiled by Clang to ordinary native code with the same assembly as a
hand-erased twin at `-O0` and `-O2`, in each standard mode the fixtures name.

Evidence:

- `e2e_erasure_equivalence`, `e2e_erasure_equivalence_providers`,
  `e2e_erasure_equivalence_programs` and `e2e_abi_equivalence`;
- `e2e_erasure_source_mapping` and `negative_erasure`;
- the `erasure-*` mutation entries.

The formal erasure theorem is not started. The checker is the V1 guarantee, and
it is trusted (G15).

## V1 closure: trust reporting (G12)

`IMPLEMENTED`. Every claim's closure covers trusted laws, library models, unsafe
code, imported contracts with their interface provenance, and runtime
validation sites. The text report, the JSON report and the editor name each
category, and a proven claim with no closure is an internal error.

Evidence:

- `TRUST.md` 36.3;
- `unit_trust_closure_test`, `unit_trust_report_test`,
  `e2e_trust_report_json`, `lsp_verification_test` and
  `lsp_interfaces_test`;
- `e2e_provenance_matrix` and
  `every_claim_of_the_provenance_matrix_names_exactly_its_closure`: the exact
  closure of each of 52 claims, with every kind of dependency reached at its
  source, through one call, a chain of three, a diamond, a recursion group, a
  unit between and in combination, and not at all, agreeing across the text
  report, the JSON report and the editor;
- the `trust-*` and `lsp-*` mutation entries.

The rows "Trust reporting (closures, categories)" and "Trust report output"
stay `PARTIAL` for what V1 does not claim. There is no evidence identity, since
proof terms have no serialized form, and trusted memory propositions are not
consumable.

## V1 closure: kernel assurance, mechanized model (G13)

`IMPLEMENTED`; the gate is met. `kernel_sound` (`Linear.v`) is the pass
condition: for every context of definitions as the kernel admits them and every
model of it, evidence the checker of all fifteen rules accepts establishes a
proposition that holds in that model. Its only premise is that the checker
accepted. A model is an interpretation of the context, one that gives every
observation, element and call a value of its type and every definition the
meaning of its body; those conditions are what a model is, not premises about
the checker, and `check_sound_closed` states the same theorem with them
written out. `kernel_consistent`: no evidence establishes `False`. Both are
audited closed under the global context, with no axiom and no admitted proof,
by `tools/formal/check.sh`.

The checking judgment is mechanized in Coq, and `check_sound` is
proven relative to two hypotheses (`KERNEL.md` 17): that normalization
preserves meaning (M2), and that the arithmetic translation is sound (M3).
Both are discharged for models of the kernel's procedures:

- M2: `Normalize.v` models the kernel's normalization, `nf_sound` proves that
  it keeps the meaning of every typed term, and `check_sound_normalized`
  instantiates `check_sound` with it.
- M3: `Linear.v` models the translation of arithmetic facts into constraints,
  and `lin_sound_model` proves, with `check_certificate_sound` (`Certificate.v`),
  that an accepted arithmetic step's facts entail its goal.

`check_sound_closed` (`Linear.v`) is `check_sound` with reflexivity deciding
by the modeled normalization and linear arithmetic by the modeled translation
and certificate checker. It has no premise about the checker. Its premises are
conditions on the interpretation and the definitions: that the interpretation
gives every observation, element and call a value of its type and every
admitted definition the meaning of its body (`proj_ok`, `elem_ok`, `call_ok`,
`call_body`), and that every definition has a supported result type and a body
of that type (`sig_ok`, `body_typed`). `check_consistent_closed`: no evidence
establishes `False` in that checker. `syntactic_consistency` is unconditional
for rules 2 to 8 and 10 to 15.

Evidence: `formal/coq/Linear.v`; `formal/coq/Audit.v` prints the assumptions
of `lin_sound_model`, `check_sound_closed`, `check_consistent_closed`,
`kernel_sound` and `kernel_consistent` with the earlier theorems, and
`tools/formal/check.sh` requires each to be closed under the global context (CTest `formal_kernel_model`, CI job *Formal model*).

The C++ kernel corresponds to the model by transcription and stays in the
logical TCB (`TRUST.md` 41).

Beyond the gate, and open: M5, a reference checker, and M6, re-checkable
evidence, through which trust would move from the C++ kernel to the model.

Pass condition: `check_sound` with no hypothesis, audited closed by
`tools/formal/check.sh`.

## V1 closure: adversarial testing (G14)

`IMPLEMENTED`. Every kernel rule, primitive and verifier check named in
`TRUST.md` 36.3 has adversarial tests and a mutation entry; there are 349
entries (`MUTATION_TESTING.md`). Persistent fuzz targets exist for kernel
proofs, terms, certificates and arithmetic, and for the recognizer, the
verification-interface decoder and the language server.

The audit's edges are tested exhaustively rather than by example, each refusal
attributed to its own case by source location and each accepted twin run:

- the sequence subset: every storage event against every view kind, every
  bound, alias, call form, unmodeled member and content invariant
  (`negative_sequence_generations`, `negative_sequence_boundaries` and their
  e2e twins; 327 refusals and 402 accepted twins, each proven and run);
- runtime validation: every admitted position, every route, write, call, alias
  and loop after which a validation's fact no longer holds, and every position
  and predicate no program can run (`e2e_runtime_validation_matrix`,
  `negative_runtime_validation_matrix`; 46 refusals and 21 twins run on valid,
  invalid and boundary input);
- trust reporting: the exact closure of 52 claims across every dependency kind
  and path, agreeing in the text report, the JSON report and the editor
  (`e2e_provenance_matrix`);
- normalization: 141 edge terms the kernel and the Coq model's normalizer both
  compute (`kernel_normalization_edges_test`, `formal_kernel_model`);
- arithmetic translation: 82 steps over every path of the translation, for
  which the kernel and the Coq model's translation build the same system,
  constraint for constraint and in order (`kernel_translation_edges_test`,
  `formal_kernel_model`);
- the checking rules: 85 verdicts, an acceptance and its near misses for each
  of the fifteen rules, on which the kernel and the Coq model's checker agree
  (`kernel_check_edges_test`, `formal_kernel_model`).

The matrices found one soundness defect, fixed with permanent regressions and a
mutation entry (`TRUST.md` 36.3: a span passed by value was not followed to the
storage it views).

The V1 closure audit (`TRUST.md` 36.4) then attacked the verified subset for
claims reported PROVEN and false at runtime, and attacked each fix again with
variants its regressions did not cover. Every defect it found is fixed with a
regression and a mutation entry.

On the release candidate the full mutation suite catches all 349 entries, none
equivalent, and `ci-asan`, `ci-ubsan` and `ci-fuzz` are green ("Profiles on the
release candidate" lists the runs).

## V1 closure: correspondence TCB (G15)

`IMPLEMENTED` as a stated boundary. The Clang bridge, elaboration, obligation
construction and the erasure checker are trusted (`TRUST.md` 7 to 17, 29). They
are tested by positive tests, refused twins and mutation entries, and none of
them is verified. V1 states them as TCB wherever a PROVEN claim is described:

- the text trust report's `Trusted translation` line;
- the JSON report's `trusted_translation`;
- the editor's hover;
- `INSTALL.md`, "What a release claims".

Guarded by `e2e_trust_report_json`, `unit_trust_report_test` and
`lsp_verification_test`, and by `trust-translation-stated-text`,
`trust-translation-stated-json` and `lsp-hover-states-translation`.
Mechanizing the translation is not started, and it is not a V1 criterion.

## V1 closure: artifact provenance (G16)

`IMPLEMENTED` as a stated boundary. Verification interfaces are checked for
integrity, compatibility and staleness (`TRUST.md` 31.1). Every text and JSON
report that imports one states its provenance as unauthenticated. No claim
through a record is assumption-free (TCB-XTU-010).

Guarded by `e2e_cross_tu`, `negative_cross_tu` and `lsp_interfaces_test`, and
by `xtu-provenance-stated` and `xtu-import-not-assumption-free`. The checks
themselves are audited exhaustively on real interfaces whose records rest on
every kind of dependency: every line edited without its checksum, every cut at
a line and within one (`negative_interface_integrity`), and 61 edits of every
field with the checksum recomputed, each refused exactly where the field is
part of what was verified or of the configuration and staleness checks, and
accepted where it is provenance only (`negative_interface_fields`).

Interface provenance stays TCB: authentication is not implemented
(`TRUST.md` 32.2), and no ad hoc scheme stands in for it.

## V1 closure: stability (G17)

`PARTIAL`. The header of this file states that neither the language
specification nor the proof system is frozen. V1 requires both frozen at a
tagged version. That is the release decision: `CMakeLists.txt` names version
1.0.0, `SPEC.md`, `GRAMMAR.md` and `KERNEL.md` each state that they are frozen
at it, this file's header says so, and the commit is tagged. The kernel and
formal-core versions change only with the calculus (`KERNEL.md` 18), so a
freeze of the current one keeps `cppl-kernel-0.9.0` and `cppl-core-0.9.0`.

## V1 closure: delivery

`PARTIAL`. On the release candidate, the release archive is built, tested and
installed on the one platform a release claims ("Profiles on the release
candidate"), and no ABI is guaranteed. Open: an archive that `release.yml` has
built, tested and attested from a pushed, signed tag at the version G17
freezes.

### Tested platform

What has been built and tested, and nothing else, is supported (`INSTALL.md`).

| Component | Tested |
| --- | --- |
| OS and architecture | Linux x86_64 (Ubuntu 24.04) |
| LLVM / Clang | 22.1.8 from apt.llvm.org, as CI and the release workflow install it |
| C++ standard library of compiled programs | libstdc++ of GCC 13.3, which that Clang selects |
| Compiler of C++L itself | Clang 22.1.8, and GCC 13.3 (`ci-linux-gcc`) |
| Standard modes verified | `c++17`, `c++20`, `c++23` (the suite names each) |
| Build | CMake 3.28, Ninja 1.11 |
| Formal model | Coq 8.18.0 |

| Not tested | Note |
| --- | --- |
| macOS, Windows | CI configurations exist (`CI.md`); no run of them has been validated, and no release archive is produced for them |
| Linux AArch64 and other architectures | never built |
| Other LLVM majors | not supported: the build accepts only LLVM 22 unless `LibClang_PREFERRED_MAJOR` is overridden (`cmake/FindLibClang.cmake`) |
| libc++ as the standard library of compiled programs | the sequence subset names no library's layout, but no libc++ run has been recorded on this platform |
| `-std=c++98` to `c++14` | `NOT STARTED` (see "C++ standard compatibility") |

### Profiles on the release candidate

The release candidate is `e2611d7`. Each run started from a clean clone of it,
in its own build tree.

| Profile | Where | Result |
| --- | --- | --- |
| `ci-linux-clang` | Ubuntu 24.04 image (`tools/ci/linux.sh clang`), Clang 22.1.8 | 200 of 200 tests pass |
| `ci-linux-gcc` | Ubuntu 24.04 image, GCC 13.3.0 | 200 of 200 tests pass |
| Release workflow | Ubuntu 24.04 image: `cmake --workflow --preset release`, then the record and checksum checks of `release.yml` | 200 of 200 tests pass; `cppl-0.0.1-Linux-x86_64.tar.gz` is packaged and matches its checksum; the record names `e2611d7` and a clean tree. Attestation and publication run only from a pushed tag, and have not run |
| `ci-quality` | native (below) | the format check and lint report nothing |
| `ci-asan` | native | 200 of 200 tests pass |
| `ci-ubsan` | native | 200 of 200 tests pass |
| `ci-fuzz` | native | all 9 searches of 200000 inputs pass with no finding; `kernel_terms` took 593 s and `kernel_proof` 265 s |
| Full mutation suite | native | all 349 entries caught, none equivalent, on `113c15a`. The candidate differs from it only in the fuzz searches' time limit and test preset, `CI.md` and the GitHub workflow, which no mutation build configures or runs |
| Formal model | Ubuntu 24.04 image, Coq 8.18.0 (`tools/formal/check.sh`) | 12 theorems checked, none resting on an axiom; 141 normalization edges, 82 translations and 85 verdicts computed. Run on `20fdcf2`; nothing it reads has changed since |

The native runs are on Fedora 44 x86_64 with Homebrew's LLVM 22.1.8, whose
compiled programs use the libstdc++ of GCC 16.2. On the same host, GCC 16.2
built the sources of `20fdcf2` with the `ci-linux-gcc` settings, and all 201
tests passed. Neither is part of the tested platform.

### ABI

No ABI guarantee beyond Clang's own is made (**ABI guarantees: No**).
`e2e_abi_equivalence` shows, on the tested platform and in each standard mode,
three things. C++L metadata changes no mangled name, layout or calling
convention. Erased output links with ordinary C++ compiled by the same Clang.
Its code is identical to its hand erasure. That is evidence for this platform,
not a guarantee for others (`COMPATIBILITY.md`).

### Release

`INSTALL.md` covers two things:

- what a release archive contains, how to check its `.sha256` and its build
  attestation, and the release record;
- "What a release claims", which covers what a release guarantees, the trust
  closure a user inherits, and what it does not claim.

`tools/release.sh` and `.github/workflows/release.yml` build, test, package and
attest Linux x86_64 only. The release and every Linux CI job run on
`ubuntu-24.04`, the tested platform, rather than on whatever `ubuntu-latest`
names, so an archive never needs a newer C library or C++ runtime than the
platform it claims has.

---

# Definition of `PROVEN`

A Law may be reported as:

```text
PROVEN
```

only when:

1. its proposition is well-formed;
2. all required proof obligations have been discharged;
3. proof evidence is accepted by the trusted kernel or equivalent trusted foundation;
4. all dependencies and assumptions are known;
5. relevant unsafe dependencies are visible;
6. runtime semantics used by the proof are compatible with the executable semantics;
7. no unresolved obligation is being silently ignored.

A proven theorem may still depend on explicit trusted assumptions.

Those assumptions must remain visible.

---

# Example future status report

A mature build may eventually produce:

```text
C++L Verification Status

Source files:                  142

Laws:
  proven:                      518
  trusted:                       1
  unresolved:                    0

Proof dependencies:
  fully assumption-free:       503
  depend on trusted axioms:     15

Runtime:
  unsafe regions:                4
  runtime validation sites:     11
  unverified FFI boundaries:     0

Kernel:
  status: VERIFIED

Erasure:
  status: VERIFIED

C++ mode:
  c++23

Native backend:
  Clang / LLVM
```

That is the level of transparency C++L should eventually provide.

---

# Guiding rule

C++L must never confuse:

```text
designed
```

with:

```text
implemented
```

or:

```text
implemented
```

with:

```text
proven sound
```

The credibility of a proof-oriented language depends on maintaining those distinctions precisely.

## Abstract observation core

The core supports nominal abstract values and typed logical projections, with
independent malformed-evidence and substitution tests. The source decomposition
providers built on it are implemented and listed above, over proof parameters
and, through case splits on a runtime path, over values read from storage. The
language server offers only what
[Developer tooling status](#developer-tooling-status) lists.
