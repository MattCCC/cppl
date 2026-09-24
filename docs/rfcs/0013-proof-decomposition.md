# Generic proof decomposition

> Surface syntax and formatting in this historical RFC are superseded by
> [RFC 0015](0015-canonical-language-surface.md) and the
> [normative grammar](../GRAMMAR.md). Semantic rationale remains applicable.

Status: implemented. Supersedes the scoped-enum-only framing of this RFC's first
revision. The normative boundary is SPEC.md 20.

Scoped enumerations were the **first vertical implementation** of proof-side
`cases`. They are no longer the architecture. The production architecture is
generic proof decomposition, and a scoped enum is its first provider.

## Separation

```text
ordinary C++ representation semantics
            from
generic proof decomposition and case reasoning
```

Clang stays authoritative for parsing, lookup, types, templates, constant
evaluation, layout, value categories, aliases, standard-library types and every
other ordinary C++ semantic. C++L owns only verification-level decomposition,
proof-state splitting, binders, hypotheses, exhaustiveness, evidence
construction, dependency checking and kernel lowering.

```text
C++ value/type
    ↓
Clang-resolved semantic information
    ↓
C++L representation provider
    ↓
ProofDecomposition
    ├── SumDecomposition
    └── ProductDecomposition
    ↓
generic proof reasoning
    ↓
existing VIR / core / kernel
```

```text
representation provider
    = knows the sound logical state model of one C++ representation family

generic case engine
    = performs proof splitting, binding, exhaustiveness, evidence and lowering
```

Adding a C++ representation to proof-side case reasoning means implementing one
sound provider and its semantic tests. It does not mean another case language,
another proof engine, another lowering path, or another editor subsystem.

## What the engine owns

Subject analysis, arm parsing, arm labels, binder declaration and scope,
`assume`, hypothesis introduction, goal propagation, nested `cases`, nested
binder capture, source locations, proof dependency tracking, self and mutual
recursion rejection, duplicate arm detection, missing arm detection,
exhaustiveness checking, arm result checking, evidence construction, VIR
lowering, kernel lowering, erasure and diagnostics. No provider reimplements any
of it.

## What a provider supplies

An ordered list of cases, each with a **discriminator** - a modeled Boolean
condition on the subject that holds in exactly that case - and its bindings; an
exhaustiveness model; and, when the model requires one, a residual label. A
provider never supplies the residual discriminator: the engine derives it as the
conjunction of the negated discriminators, so a provider cannot widen the
residual state.

Provider selection is by Clang-resolved semantic identity. Aliases, qualified
names and template specializations that resolve to one declaration select one
provider; a matching unqualified spelling never does.

## Evidence

Splitting on a discriminator is the existing conditional-elimination rule: its
true premise is the discriminator, its false premise the negation, and the
kernel derives and checks both itself. Splitting on each in turn leaves one
branch in which all are false, which conjunction introduction combines into the
tail case's fact.

No new logical rule is needed, for this or for any future provider whose states
are distinguished by decidable conditions on modeled values.

```text
representation-specific frontend knowledge
        ↓
ordinary logical propositions / evidence
        ↓
small generic trusted kernel
```

Logical assumptions: 0. Axioms: 0. Trusted mechanisms: 0. The existing
correspondence TCB gains the per-provider state mapping documented in TRUST.md
19.

## Scoped enumerations, as a provider

A scoped enum's state space is its fixed underlying type's entire value set, not
its enumerator list ([dcl.enum]). Modeling only the enumerators would prove false
claims such as every `E` being `E::a` for `enum class E { a };`, so the residual
case `unnamed` is explicit. Aliased enumerators name one case. Distinct values
produce distinct cases. New distinct enumerators require new arms.

## Tagged sums, as providers

`std::variant`, `std::optional` and `std::expected` share one shape: a
discriminating observation and one payload observation per state. They are
therefore one mechanism, not three. A variant's states are its alternative
**indices** plus `valueless`, so repeated and aliased alternative types stay
distinct and `valueless` is never omitted. An optional's are `some` and `none`,
with the payload bound only in `some`. An expected's are `value` and `error`,
each with its own payload. Only the public semantics of these types are modeled;
no standard library's layout is read, and no `std::get`, `std::visit`,
`.index()`, `has_value()` or dereference is emitted or relied on.

## Pointers, as a provider

A pointer decomposes into `null` and `non_null` and into nothing else. The
provider states no lifetime, provenance, dereferenceability, bounds,
initialization, ownership, uniqueness or dynamic type. `non_null` binds nothing:
a non-null pointer is not an assertion that a live, initialized object exists.

## Products, as a provider

Records, `std::pair`, `std::tuple`, `std::array` and built-in arrays all project
onto their components in order. A product has one state, so `decompose` is not a
case split and generates no discriminator; it is spelled with its own keyword for
that reason. Each binding is a logical projection onto the existing subobject, so
no structured binding, copy, move or temporary is created. Component order,
types, access and array extents come from Clang. A component Clang reports as
inaccessible is refused by name rather than projected.

## Composition

Nesting is generic. An arm binder is an ordinary proof expression, so
decomposing it selects a provider the same way the outer subject did. No pairwise
handler exists for `variant<optional<T>, E>` or `optional<pair<A, B>>`, and none
is needed.

Induction stays separate from finite case decomposition. A provider may later
expose states useful to induction, but supporting `cases` does not make a
representation inductive.

## Remaining boundaries

A representation with no provider is refused at the provider boundary, by name,
and its states are never guessed. Arm syntax does not make a class a sum.

In a proof body, `cases` and `decompose` see no assignment, call, construction
or destruction, so no case fact there can go stale. Written as statements of a
verified function's body they split that body's path, over values that do
change (SPEC.md 20.7). The subject stays a logical value. It is an ordinary C++
expression Clang resolves where the statement stands, so mutable storage
reaches the split only along the existing read path:

```text
Place -> current PlaceVersion -> logical Value -> decomposition
```

Case facts are facts about that one version (CASE-008). A later write creates or
havocs versions through the ordinary storage/effect model, so stale case facts
cannot describe the new current value, and no aliasing exception is needed
(CASE-009). `cases` must never take a `Place` as its subject or turn one into a
proof term: a place is not a value, and the kernel's term language stays closed
precisely because no address-typed term exists (RFC 0014). Making the case
engine storage-aware would breach that, and is the wrong direction even though
it looks like the shorter path to mutable subjects.

## Splitting a runtime path

A split in a verified body has no goal to close: the proof obligation it splits
is the rest of the path. So its arms are that path continued, one per case, and
the code after the split is verified once in each. Each arm supposes exactly
what the proof-side split gives the same case: every earlier discriminator
false and its own true, or every one false for the residual. Those suppositions
cover every state by excluded middle on each discriminator in turn, whatever the
provider claimed about completeness, so the only way to lose a state is to lose
its arm. The path walk therefore re-checks that each state has exactly one arm,
independently of elaboration, before it walks any of them.

An arm holds what can stand on a path: nested splits, and a `contradiction`
claim, which ends the arm's path under the claim's own obligation. Statements
that close a goal have nothing to close there and are refused by the recognizer.
An omission is the claim form of the proof-side omission and keeps its own
origin, so the trust report still counts omitted cases and impossible paths
apart.

Nothing new tracks invalidation. The subject is read by the same `read_place`
every other read uses, so a case fact is a proposition about the version term
current there. A write binds a new version, a possible alias write or a call
effect binds an unknown one, and a loop head binds a fresh one; none of them is
the term the fact mentions. Tests pin each of those against a matched pair that
differs only in the write.

What a split can reach is what the body model reaches. The bridge tracks
integer, Boolean and scoped-enumeration storage, including members and
elements, so those are the subjects that change. Verified code cannot call the
standard library, so `std::optional`, `std::variant` and `std::expected`
subjects are parameters that do not change within the body, and pointer locals
are not modeled at all. A local aggregate has no whole value, so a split
reaches it through its members. Binders are declared for Clang once per
statement as written; a function template whose specializations bind values
of different types is refused at the specializations the declared type does not
fit, which the elaborator checks for every binder it reads.

## Omitting an impossible case

CASE-004 allows a case to be accounted for by contradiction instead of by an
arm, and CASE-005 requires that contradiction be checked evidence from the
current proof context rather than a heuristic. VERIFIED-023 states the same
requirement for discharging an unreachable runtime path. Both are uses of one
operation: from contradictory premises in the current context, close a goal of
any shape.

The two should share that prover and keep separate obligation origins and
diagnostics, because a proof-side omitted case and a runtime control-flow path
are different claims about different things, and conflating their provenance
would make a diagnostic name the wrong one.

The elimination is not the transport it first looks like. Eliminating an absurd
equality with `EqualityElimination` does not work: transport needs evidence of
the goal at the equality's other side, and with a constant motive that is the
goal again. Transport can only conclude the contradiction itself, not an
arbitrary goal.

A contradiction is evidence for `False`, a proposition of the core with no
introduction rule, and the goal is closed from it by falsity elimination,
`p : False` giving `P` for any well-formed `P` (FOUNDATIONS.md 26). That is two
steps. The first refutes the named evidence and every premise standing at that
point -- for an omitted case, including its own discriminator -- into `False`
with `LinearArithmetic`, which refutes `F1 /\ ... /\ Fn /\ not G`; for
`G = False` the negation holds outright and states no constraint, so the
certificate can only be refuting the facts. The second eliminates `False` into
the goal. The split is not cosmetic. A single step against the real goal also
succeeds when the goal merely follows from the premises, and an omission judged
that way accepts `omit C by contradiction e;` for a perfectly possible case `C`
whenever `C`'s goal happens to be provable. Separating the steps makes "the
context cannot occur" the content of a checked proof term rather than an
inference drawn from one.

The first version of this mechanism added no rule: it carried absurdity as
`0 == 1` over booleans and closed the goal from it with a second
linear-arithmetic step, after taking the goal apart by its introduction rules.
That reached every goal built from equalities of integers and nothing else, so
a goal equating two records was refused by name, and whether a contradiction
closed a goal depended on what the goal happened to be about. Falsity
elimination replaces it. It is ordinary ex falso quodlibet, not an axiom or an
assumption: it concludes nothing until evidence for `False` has been checked,
and it is the only primitive that makes a contradiction independent of the
goal's shape. A rule specific to equalities of structured values was rejected
for the same reason; it would have left the next proposition form refused in
turn. Kernel rules added: one. Axioms and assumptions added: none. The
core/kernel version is 0.7.0, so evidence checked by an earlier kernel is not
reused.

Finding a certificate is search, and it is the search automation already runs
for arithmetic goals, moved below both layers into `compiler/refutation` so that
obligations can use it without depending on automation. It is untrusted: the
kernel states the constraints and checks every certificate itself, and a search
that finds nothing leaves the claim unproven rather than showing the context is
satisfiable.

The surface form is `contradiction evidence;` (GRAMMAR.md 5.6), an ordinary
proof statement rather than a second kind of arm. Keeping it a statement is what
lets the same construct discharge a runtime path, below.

Omitting a case is spelled `omit label by contradiction evidence;` inside the
`cases` statement (GRAMMAR.md 5.7). The label only has meaning relative to that
one decomposition, so the discharge belongs lexically at the site rather than
before it. Three alternatives were rejected. Writing the contradiction in an arm
body for the case leaves an arm present, which is CASE-004 clause 1, so there
would be no omitted case at all and clause 2 should have been deleted instead.
Letting the engine scan the surrounding context for a refutation when an arm is
missing makes an accidental omission and an intentional impossibility
indistinguishable, which is what CASE-005 forbids. Putting the discharge before
the `cases` statement separates the label from the partition that gives it
meaning.

An omission is not an arm with a shorter body: it binds nothing, has no body,
and is recorded as omitted rather than inferred from its shape, because CASE-004
accounts for a case by exactly one of the two and a real arm's body can also be
a single statement. Its evidence is still checked under the case's own
discriminator premise, so the check is the one an arm would have received.

Each omission that holds is an obligation of its own (CASE-012, CASE-016):
origin `OmittedCase`, a goal stating that the premises standing in the case,
closed over the binders they stand under, entail `False`, the refutation as its
evidence, and an identity that includes its origin, so an unreachable runtime
path stating the same proposition never shares it. The kernel checks it apart
from the proof it occurs in, and the trust report counts it apart from laws.

`omit` is recognized only where a case label followed by `by` comes after it, so
it stays an ordinary name everywhere else, including as the first name of a
label such as `omit::State::idle` (WORD-010). The statement after `by` is read
by the same parser as every other proof statement, so its evidence reference
takes arguments exactly as `exact` does.

## Claiming a runtime path cannot occur

VERIFIED-023 lets a path be discharged as impossible from checked contradiction
evidence. Its source form is the same statement, `contradiction evidence;`,
written in a verified function's body (VERIFIED-045). A second spelling was
rejected: one construct with one meaning, "the context where this stands cannot
occur", is what CASE-012 asks for, and what differs between a proof and a runtime
path is the context, which is exactly what the obligation's origin records.

The claim is checked where it stands. The context is the path's own: every
fresh value it binds and every fact it supposes - preconditions, branch
conditions, loop invariants and the postconditions of verified calls already
made - which are precisely what the path's partial-correctness conditions state
(RFC 0007). The claim is one more such condition: the path's facts closed over
`False`, of origin `ImpossiblePath`, with an identity that includes that origin.
Its evidence is the refutation an omitted case uses, the named proof instantiated
at its arguments and every fact refuted into `False`, so no frontend heuristic
ever decides that a path is unreachable. The path ends at the claim, so what
follows it on that path owes nothing: that is the point of the claim, and it is
sound only because the claim itself is proven.

The evidence is built after the written proofs are lowered, because the claim
names one, and a claim that cannot be given evidence carries its refusal and is
reported once, where it was written. Nothing else may establish it. Automation
could often prove `False` from a contradictory path on its own, which is exactly
the inference CASE-005 forbids, so a claim is never offered to a strategy, and
one resting on a callee's postcondition is not accepted until that callee's
contract is.

The statement is proof syntax inside runtime code, which raises two questions
C++L's other constructs do not. First, `contradiction name;` is a valid C++
declaration wherever `contradiction` names a type, and only Clang knows what a
name denotes. The recognizer runs before Clang, so it takes the claim only where
the translation unit uses the word for nothing else outside laws and proofs;
there the statement cannot be C++, and anywhere else C++ keeps it and a warning
says so (WORD-011). Second, the statement stands where C++ needs a statement.
Erasing it entirely would make `if (c) contradiction e; return x;` return under
the `if`, so it erases to an empty statement: its words go and its `;` stays
(ERASE-016). Clang is given a block at the same point, a marker declaration and
one declaration per argument, so the arguments are resolved in the scope the
statement sees and read at the versions current there.

A claim is verified only as part of a verified body. In an ordinary function
nothing would check it, so it is refused rather than erased unchecked.

Validation is a matched pair differing only in the branch condition that leads
to a claim, the same claim across branches, a loop, a verified call, a local's
versions and an unbraced `if`, and rejections for a reachable claim under a
provable goal, evidence that is unknown, refused, mistyped, over-instantiated or
not an equality, a claim resting on an unproven callee, and a claim outside a
verified function. The spelling is kept as a C++ declaration where the word
names a type. Mutations that drop the C++-first rule, erase the `;`, refute
without the path's facts, accept written evidence before its callees are proven,
or let a strategy establish an impossibility are each caught.

## Validation

Generic tests exercise arm matching, binder scope, nesting, goal propagation,
dependency checking, duplicate and missing arms, source mapping, erasure and
kernel-evidence corruption once, through one provider. Provider tests cover only
representation-specific state modeling. Source tests cover C++17/20/23, aliases,
negative enumerators, empty enums, nested arms, direct and Law proofs, forward
dependencies, residual binders under quantifiers, erased runtime equivalence,
and a newly added enumerator. Every provider is exercised end to end, including
repeated and aliased variant alternatives, cv-qualified and reference subjects,
template-dependent payloads, all five product forms, and cross-provider nesting
in both directions; `std::expected` is gated on the C++23 library. Rejection
tests cover false goals, missing residual and named arms, an omitted
`valueless`, an out-of-range and a duplicated alternative, a payload bound in a
stateless arm, a pointee bound through `non_null`, an inaccessible member, a
product written as a sum and a sum written as a product, a type spelled like a
standard one, wrong types, labels and evidence, wildcards, binder escape and
capture, self and mutual dependencies, malformed arms, written failure without
fallback, and representations with no provider. Adding an alternative or a
product field invalidates a previously exhaustive proof. Unit tests
corrupt VIR partitions and generated kernel evidence; the kernel remains the
final authority. No test result is treated as proof of soundness.

Because the representation-to-partition correspondence is trust-sensitive and
the kernel never sees the C++ type, it is attacked separately from the arms:
tests corrupt the subject's own model, so that the partition the provider
reports describes no state, contradicts the arms, or names a different
discriminator value. The last is accepted, as it must be -- it is a different
but well-formed partition -- and is pinned to produce different evidence, which
is what shows the discriminator reached the kernel rather than being trusted.
Source tests cover the read paths a subject can arrive by, a member and a
built-in array element as well as a parameter, so CASE-008 is not satisfied
merely because every subject in the corpus is an identifier.

Omission is pinned by a matched pair: one law, one premise, one piece of
evidence, differing only in whether the omitted case is the one the premise
contradicts or the one it agrees with. The accepted half omits the first case
the partition splits on, so its own discriminator is the only case fact in its
branch, and the refused half's goal is provable in the omitted branch. Two
mutations are therefore each caught: withholding the omitted case's
discriminator fails the accepted half, and judging an omission by whether its
goal follows accepts the refused half. Beside that pair: an omission under a
provable goal, under a satisfiable premise, of an unknown label, together with
an arm for the same case, and a case simply left out while the evidence that
would discharge it is in scope. Unit tests corrupt an omission's evidence -- its
facts, its certificate, a fact's stated proposition -- and check it against the
wrong claim, and every corruption is refused. The same evidence claimed under
`OmittedCase` and `ImpossiblePath` stays two obligations with two identities,
and is reported under each claim's own name when refused.

Falsity elimination is pinned by a matched pair whose goal equates two records:
closed under a false premise, by a written proof and by automation, and refused
under a satisfiable one. Kernel tests close every proposition form from a
refuted fact, and refuse falsity elimination over reflexivity, over a
hypothesis that is an absurd equality rather than `False`, over a satisfiable
fact, over no facts, over a certificate naming a constraint that is not there,
and over a fact restated as something its evidence does not establish. `False`
alone is refused under every introduction, and a certificate that leaned on a
negated goal is refused once the goal is `False`. Mutations that check the
evidence against the goal, skip the check, or let `False`'s negation state a
constraint are each caught.

## Abstract observation signature

The generalized value model admits `V(identity; T0, ..., Tn)` and checked
`project<i>(v) : Ti`, as specified in FOUNDATIONS.md 44. It needs no new inference
rule: equality, substitution and conditional elimination already apply to the
resulting typed propositions. A signature is part of identity, so changing a
component invalidates old evidence. Observation normalization never unfolds C++
code or assumes a constructor or payload value. The core/kernel version is
0.6.0; the typing and normalization TCB grows, without axioms or logical
assumptions. This removes the scalar-only core restriction; provider and source
integration status is stated separately from availability of this core model.
