# C++L Trust Model

**C++L — Trusted Computing Base and Assurance Boundaries**

Status: Normative trust specification

This document defines the **Trusted Computing Base (TCB)**, trusted-assumption model, correspondence obligations, runtime trust chain, artifact trust rules, and assurance-reporting requirements of C++L.

It answers one question:

> What must be correct, and what assumptions must be explicit, for a C++L result reported as `PROVEN` to mean what it claims to mean?

This document is normative for trust classification and assurance boundaries. It does **not** redefine C++L source-language semantics, grammar, or the mathematics of the formal calculus.

Language syntax and semantics are defined by [SPEC.md](./SPEC.md). The formal calculus is defined by [FOUNDATIONS.md](./FOUNDATIONS.md). Implementation structure belongs in [ARCHITECTURE.md](./ARCHITECTURE.md) and [DESIGN.md](./DESIGN.md). Supported C++/toolchain combinations belong in [COMPATIBILITY.md](./COMPATIBILITY.md). Implementation maturity belongs in [STATUS.md](./STATUS.md).

A conforming implementation MUST NOT use implementation limitations, architecture choices, status, or tooling convenience to weaken the trust requirements in this document.

---

# 1. Normative terminology and authority

The words `MUST`, `MUST NOT`, `SHOULD`, `SHOULD NOT`, and `MAY` are normative requirements.

The documentation authority is separated by subject:

```text
SPEC.md
    language meaning and normative source semantics

FOUNDATIONS.md
    formal calculus and mathematical justification

TRUST.md
    TCB, assurance boundaries, assumption provenance and trust reporting

GRAMMAR.md
    concrete syntax

ARCHITECTURE.md / DESIGN.md
    implementation structure and engineering choices

COMPATIBILITY.md
    supported C++ modes, toolchains, platforms and ABI compatibility

STATUS.md
    implementation coverage only; never normative language or trust semantics
```

If this document appears to redefine a C++L construct, `SPEC.md` governs the construct's meaning and this document governs only what must be trusted for that meaning to be preserved.

If this document appears to redefine a proof rule, `FOUNDATIONS.md` governs the proof rule and this document governs only which checker or correspondence component must be correct for its use to be trustworthy.

**[TCB-AUTH-001]** `STATUS.md` MUST NOT weaken, narrow, reinterpret, or replace a requirement of this document.

**[TCB-AUTH-002]** An implementation MAY reject a construct it cannot verify soundly, but MUST NOT accept it under a weaker hidden trust model.

**[TCB-AUTH-003]** A trust requirement remains normative even when the corresponding feature is not implemented by a particular toolchain.

---

# 2. Terms

## 2.1 Trusted Computing Base

The **Trusted Computing Base** is the set of components whose incorrect behavior can cause an assurance claim to be accepted or presented with stronger meaning than is justified.

For C++L, the end-to-end TCB is intentionally decomposed rather than treated as one undifferentiated compiler.

## 2.2 Logical TCB

The **logical TCB** contains the components whose correctness is required for a core proof judgment to be sound.

A fault in the logical TCB can cause invalid evidence to be accepted for the core proposition actually presented to the checker.

## 2.3 Correspondence TCB

The **correspondence TCB** contains the components whose correctness is required for a core proposition, storage obligation, effect fact, or proof obligation to mean what the corresponding C++L/C++ source means.

A fault in the correspondence TCB can ask a perfectly sound kernel to prove the wrong proposition.

## 2.4 Runtime TCB

The **runtime TCB** contains the components whose correctness is required for the native executable to preserve the runtime semantics about which verification reasoned.

## 2.5 Artifact and reuse TCB

The **artifact and reuse TCB** contains any component whose correctness is required when a prior verification result, imported proof artifact, metadata summary, cache entry, or pre-checked interface is reused without reconstructing and independently checking all relevant evidence from source.

## 2.6 Reporting TCB

The **reporting TCB** contains the components whose correctness is required to present assurance status and trust dependencies accurately to a user or machine consumer.

A reporting defect does not necessarily make a mathematical proof false, but it can falsely present `TRUSTED`, `UNSAFE`, `UNVERIFIED`, or `UNRESOLVED` material as `PROVEN`; therefore it is security-sensitive.

## 2.7 Trusted assumption

A **trusted assumption** is an explicit proposition admitted through the C++L trusted surface defined by `SPEC.md` rather than derived from proof evidence.

A trusted assumption is not a software component and is not itself a TCB bug. It is an explicit premise of the assurance claim.

## 2.8 Trust closure

The **trust closure** of an assurance claim is the transitive set of trusted assumptions on which its accepted evidence depends.

## 2.9 Correspondence assumption

A **correspondence assumption** is an explicit dependency on an external semantic authority or model, such as the selected C++ implementation, standard-library specification, operating system guarantee, ABI contract, or hardware behavior.

Correspondence assumptions MUST be classified and reportable where they materially affect an assurance claim.

## 2.10 Fail closed

To **fail closed** means to refuse the stronger assurance claim when required semantics, evidence, provenance, identity, capability, or dependency information is unavailable or cannot be validated.

Failing closed MUST NOT be implemented by silently replacing static proof with trust, runtime validation, `unsafe`, or a weaker theorem.

---

# 3. End-to-end assurance model

C++L distinguishes the validity of a formal derivation from the correctness of its connection to source and runtime behavior.

An end-to-end verified claim has four separable obligations:

```text
SOURCE MEANING
    the source construct is interpreted according to SPEC.md

CORRESPONDENCE
    source meaning is translated to the correct formal/storage obligations

PROOF / CHECKING
    those obligations are discharged by valid evidence under explicit premises

RUNTIME PRESERVATION
    when the claim concerns execution, the compiled program preserves the verified runtime semantics
```

A small proof kernel addresses only the third obligation unless it independently checks certificates for the other obligations as well.

**[TCB-END2END-001]** A conforming implementation MUST NOT describe the proof kernel alone as the entire TCB for a source-level or executable-level claim unless all source-to-core and runtime-preservation correspondence required by that claim is independently checked outside the trusted boundary.

**[TCB-END2END-002]** A component that can cause the checker to verify a proposition different from the proposition denoted by the source is part of the correspondence TCB unless an independent checker validates that mapping.

**[TCB-END2END-003]** A component that can cause a different runtime program to execute than the runtime program whose semantics were verified is part of the runtime TCB unless an independent equivalence checker validates that mapping.

**[TCB-END2END-004]** Explicit trusted assumptions MUST remain premises of the assurance claim. Proof derived from them does not erase their presence.

## 3.1 Assurance claim model

For trust purposes, an accepted theorem can be viewed as:

```text
Claim = (P, A, C, R)
```

where:

```text
P   proposition established
A   transitive trusted-assumption closure
C   source/formal correspondence on which P depends
R   runtime correspondence required if P is about executable behavior
```

`PROVEN` means that valid evidence establishes `P` under the explicit premises and trust closure required by the claim. It does not mean that `A` is empty.

## 3.2 Unconditional and assumption-relative confidence

A theorem with an empty trusted-assumption closure and one with a non-empty trusted-assumption closure MAY both have language status `PROVEN` as defined by `SPEC.md`, but tooling MUST expose the difference.

The following presentations are forbidden when they hide a non-empty trust closure:

```text
fully verified
assumption free
proved without trust
zero trust
```

unless those descriptions are actually true for the claim being reported.

---

# 4. TCB decomposition

A conforming implementation MUST classify assurance dependencies into the following layers.

| Layer              | What it protects                                  | Typical failure if wrong                    |
| ------------------ | ------------------------------------------------- | ------------------------------------------- |
| Logical TCB        | validity of formal derivations                    | false core theorem accepted                 |
| Correspondence TCB | source/storage semantics to formal obligations    | correct proof of the wrong source claim     |
| Runtime TCB        | preservation into executable behavior             | verified source and executed binary diverge |
| Artifact/reuse TCB | safe reuse/import of prior evidence and summaries | stale/forged result accepted                |
| Reporting TCB      | truthful assurance status and provenance          | trusted/unsafe result shown as proven       |

**[TCB-LAYERS-001]** These layers MUST be distinguished in trust documentation and machine-readable reports.

**[TCB-LAYERS-002]** Moving functionality out of the logical kernel does not remove it from the end-to-end TCB when its failure can still make a source-level claim unsound.

**[TCB-LAYERS-003]** A component MAY be removed from a trust layer only when another independently trusted mechanism checks all properties that previously required trusting that component.

**[TCB-LAYERS-004]** Tests, fuzzing, code review, static analysis and formal verification can reduce confidence risk, but do not by themselves remove a component from the TCB. Removal requires an actual independently checked boundary.

---

# 5. Logical proof TCB

## 5.1 Proof checker

The proof checker is the final authority for the validity of core proof evidence.

**[TCB-CORE-001]** The checker MUST reject malformed, ill-typed, out-of-scope, capture-invalid, or logically invalid proof evidence.

**[TCB-CORE-002]** The checker MUST derive conclusions from the formal rules defined by `FOUNDATIONS.md`; it MUST NOT accept a proposition merely because a frontend, tactic, solver, AI system, or optimization pass labels it true.

**[TCB-CORE-003]** Any primitive logical rule implemented directly by the checker is part of the logical TCB.

**[TCB-CORE-004]** Any normalization or definitional-equality procedure whose result can close a proof obligation is part of the logical TCB.

**[TCB-CORE-005]** Any substitution, binder shifting, alpha/capture handling, universe/type checking, or term typing used to validate evidence is part of the logical TCB.

**[TCB-CORE-006]** Any built-in decision procedure whose output the checker accepts without an independently checked certificate is part of the logical TCB.

**[TCB-CORE-007]** The checker MUST fail closed on malformed or resource-exhausting evidence; resource failure MUST NOT be interpreted as success.

Falsity elimination (`FOUNDATIONS.md` §26) is one of the checker's primitive rules and so part of this TCB under TCB-CORE-003. It is ex falso quodlibet, not an axiom: it concludes any well-formed goal from checked evidence for `False`, and it is sound only as long as `False` cannot be established without a contradiction.

**[TCB-CORE-017]** `False` MUST have no introduction rule. Evidence for it MUST come only from a hypothesis the proof itself introduced, from an elimination rule applied to checked evidence, or from a linear-arithmetic certificate that refutes the stated facts with no goal taking part.

## 5.2 Primitive formal semantics

Primitive formal operations used by the checker must correspond exactly to their definitions in `FOUNDATIONS.md` and, where they model C++ operations, to the C++ semantics admitted by `SPEC.md`.

This includes, where applicable:

```text
machine integer constants and widths
wrapping arithmetic primitives
representability, truncating division and integer conversion primitives
comparisons
Boolean representation used by the core
equality and substitution
quantifier binding
logical connectives
abstract observation terms
indexed observation terms
mathematical-domain operations
termination/well-foundedness primitives admitted by the core
```

Indexed observation (`FOUNDATIONS.md` §45) places its typing, substitution, congruence and structural identity in the logical TCB. It introduces no axiom and no reduction rule: the observation is uninterpreted, so it admits no proposition congruence does not already justify.

**[TCB-CORE-016]** The checker MUST NOT admit injectivity or extensionality for an indexed observation. Equal observations MUST NOT prove equal indices, and equality at every index MUST NOT prove equal subjects, unless the formal model explicitly introduces such a principle.

**[TCB-CORE-008]** The checker MUST NOT use host-language overflow, undefined behavior, locale, address identity, pointer identity, floating host arithmetic, or nondeterministic container order as hidden semantics for formal terms unless those semantics are explicitly part of the formal model.

**[TCB-CORE-009]** Exact arithmetic used to check proof certificates MUST detect and reject implementation overflow rather than wrap silently.

## 5.3 Axioms and primitive assumptions

C++L distinguishes primitive logical rules from trusted user assumptions.

**[TCB-CORE-010]** The logical checker MUST have no hidden axiom-admission path.

**[TCB-CORE-011]** Any primitive axiom required by the formal calculus MUST be enumerated by `FOUNDATIONS.md` and represented in trust metadata as part of the calculus definition.

**[TCB-CORE-012]** User-authored `trusted law` declarations MUST NOT be silently compiled into ordinary kernel rules or hidden global axioms. Their identity and provenance MUST remain explicit.

## 5.4 Checker size and dependencies

Smallness is a design objective, not a semantic claim.

**[TCB-CORE-013]** The logical checker SHOULD minimize dependencies and mutable global state.

**[TCB-CORE-014]** Dependencies whose incorrect behavior can cause invalid evidence to be accepted are themselves part of the logical TCB and MUST be counted as such.

**[TCB-CORE-015]** A claim that the kernel is "small" MUST NOT exclude linked libraries or generated tables that participate in proof acceptance.

---

# 6. Untrusted proof producers

The following components SHOULD be treated as untrusted proof producers whenever all output they produce is independently validated before it contributes to `PROVEN`:

```text
tactics
proof search
rewriters
simplifiers
automation
SMT/SAT model search
AI-generated proofs
AI-generated code
proof repair tools
IDE quick fixes
counterexample generators
optimization of proof terms
```

**[TCB-PRODUCER-001]** A proof producer MAY be outside the logical TCB only if every artifact capable of establishing a proposition is checked by the logical TCB or another explicitly trusted checker before use.

**[TCB-PRODUCER-002]** A proof producer MUST NOT be able to mark an obligation discharged by returning a Boolean success flag that bypasses evidence checking.

**[TCB-PRODUCER-003]** If an automation engine is trusted directly, it becomes part of the appropriate TCB and that direct trust MUST appear in the trust report.

The arithmetic refutation search (`compiler/refutation`) is such a producer. It
serves automation and the written `contradiction` alike, and it only proposes
certificates: the kernel states the constraint system itself and checks every
one, and a search that finds nothing, or runs out of budget, leaves the claim
unproven.

## 6.1 SMT and SAT solvers

Preferred architecture:

```text
obligation
    ↓
solver
    ↓
certificate / proof evidence
    ↓
independent checker
    ↓
accepted evidence
```

**[TCB-SOLVER-001]** Solver search heuristics are not trusted when the solver produces independently checkable evidence whose complete semantic content is checked.

**[TCB-SOLVER-002]** If a solver result is accepted without independently checkable evidence, the solver, its relevant configuration, and any translation into its input language are part of the TCB for that result.

**[TCB-SOLVER-003]** A solver timeout, `unknown`, crash, unsupported result, malformed certificate, or incomplete certificate MUST NOT be promoted to proof or trust automatically.

## 6.2 AI systems

**[TCB-AI-001]** AI systems are never formal authorities by virtue of being AI systems.

**[TCB-AI-002]** AI-generated source, specifications, Laws, proofs, trusted declarations, patches and refactorings MUST be treated exactly like human-authored candidate input.

**[TCB-AI-003]** An AI explanation, confidence value, chain of reasoning, majority vote, or self-review MUST NOT substitute for formal evidence or explicit trust.

**[TCB-AI-004]** An AI system MAY modify trusted declarations only as ordinary source editing; the resulting trust expansion MUST remain explicit in source and reports.

---

# 7. Source-to-core correspondence TCB

The proof kernel proves propositions. It does not, by itself, establish that those propositions correspond to the user's C++L source.

The source-to-core correspondence boundary therefore includes every mechanism that can change:

```text
which source construct is being verified
which C++ entity a name denotes
which runtime value a formal term denotes
which path or state is represented
which precondition/postcondition is associated with which function
which write affects which storage
which effect summary applies to which call
which proposition is submitted for which source obligation
```

**[TCB-CORR-001]** A frontend/elaborator is outside the end-to-end TCB only to the extent that an independent checker validates its source-to-core mapping.

**[TCB-CORR-002]** Kernel validation of a proof term does not validate a frontend's choice of proposition unless the kernel receives and validates enough certified source-semantic information to reconstruct that proposition independently.

**[TCB-CORR-003]** Source locations, generated helper names, textual spellings, presumed line directives, or unstable addresses MUST NOT be used as semantic identity when Clang/C++ semantic identity is required.

**[TCB-CORR-004]** Ambiguous correspondence MUST fail closed.

---

# 8. Preprocessing, recognition and source ownership

C++ preprocessing occurs according to the selected C++ mode before C++L contextual interpretation as specified by `SPEC.md`.

The recognition layer decides which token sequences are ordinary C++ and which are C++L constructs. That decision is trust-sensitive.

**[TCB-SOURCE-001]** The recognizer MUST preserve ordinary C++ token meaning outside valid C++L grammatical contexts.

**[TCB-SOURCE-002]** A recognition defect that can reinterpret ordinary C++ as proof-only syntax, erase runtime-bearing text, attach a contract to the wrong declaration, or change a C++ expression into a different formal proposition is a correspondence-TCB defect.

**[TCB-SOURCE-003]** Macro expansion, conditional compilation, included source, module imports and generated source participating in verification MUST be bound to the exact analyzed token stream.

**[TCB-SOURCE-004]** Verification results MUST NOT be reused for a semantically different preprocessed program merely because original filenames and source line numbers match.

## 8.1 Analysis text and runtime text

If an implementation creates a transformed analysis view and a distinct runtime view, the mapping between them is trust-critical.

**[TCB-SOURCE-005]** The toolchain MUST establish that ordinary runtime-bearing C++ semantics are preserved between the analyzed program and the runtime program.

**[TCB-SOURCE-006]** Proof-only source MAY be erased only where `SPEC.md` defines it as proof-only.

**[TCB-SOURCE-007]** Runtime-bearing lowering, such as refinement aliases, MUST be canonical and semantically constrained by `SPEC.md`; arbitrary source rewriting MUST NOT hide behind the term "erasure".

The analysis text may also carry a reference whose only purpose is to make the C++ authority expose an entity it otherwise reports no cursor for, such as the specialization an explicit instantiation names. Which entity that reference denotes is resolved by the C++ authority, not by the text that names it.

**[TCB-SOURCE-008]** An analysis-only reference emitted to make an entity reachable MUST name it with the spelling the source used, and MUST NOT select among candidate entities itself. What it reaches is then whatever ordinary C++ name lookup and template argument resolution select, which is the same authority every other construct is resolved by (`TCB-CLANG-002`).

---

# 9. Clang and C++ semantic authority

C++L delegates ordinary C++ parsing, name lookup, overload resolution, type identity, template substitution, access control, object-model semantics and other supported C++ semantic questions to the selected C++ semantic authority.

For implementations using Clang, this makes Clang part of the correspondence and runtime trust chains for claims that depend on those answers.

**[TCB-CLANG-001]** C++L MUST NOT independently guess a C++ semantic fact when the selected C++ authority resolves it differently.

**[TCB-CLANG-002]** Formal lowering of a C++ expression MUST use the resolved operation, conversions, type, value category, declaration identity and template instantiation selected by C++, not merely its textual spelling.

**[TCB-CLANG-003]** Overloaded operators MUST NOT be lowered as built-in operators unless the selected C++ semantics establish that the built-in operation is the operation invoked.

**[TCB-CLANG-004]** A C++ conversion MUST NOT be ignored merely because source and destination have similar textual types.

**[TCB-CLANG-005]** If the implementation cannot obtain enough C++ semantic information to model a construct soundly, verification MUST fail closed for the stronger claim.

## 9.1 Compiler bugs

A bug in Clang or another selected C++ semantic authority can invalidate a C++L source-level or executable-level claim whose correctness depends on that semantic answer.

Such dependence MUST NOT be described as logical-kernel trust; it is correspondence/runtime trust.

---

# 10. VIR and elaboration correspondence

Verification IR is a semantic boundary between resolved C++ and formal obligation construction.

**[TCB-VIR-001]** Each VIR value, place, type, control-flow edge, call, effect, lifetime event and formal proposition MUST correspond to the resolved source construct it represents.

**[TCB-VIR-002]** Unsupported or partially representable source semantics MUST NOT be approximated by a stronger or simpler VIR node that changes proof meaning.

**[TCB-VIR-003]** Information discarded from VIR MUST be irrelevant to every proof obligation derived from that VIR; otherwise discarding it is a correspondence defect.

**[TCB-VIR-004]** Malformed VIR received from an untrusted producer MUST be rejected before it can create an accepted assurance claim.

**[TCB-VIR-005]** If VIR is serialized or imported, semantic identities and trust dependencies MUST survive serialization without collision or ambiguity.

---

# 11. Obligation construction

Obligation construction is part of the correspondence TCB whenever the kernel does not independently derive the same obligations from certified source semantics.

**[TCB-OBL-001]** Every language rule that requires proof MUST generate all corresponding obligations on all relevant paths.

**[TCB-OBL-002]** Missing an obligation is a soundness defect even when every generated obligation is kernel-checked correctly.

**[TCB-OBL-003]** Generating an extra obligation may cause incompleteness but does not by itself create unsoundness; implementations SHOULD prefer conservative refusal to omitted obligations.

**[TCB-OBL-004]** An obligation MUST be associated with the exact source entity, logical value version, trust context, and semantic dependencies from which it was derived.

**[TCB-OBL-005]** A successful proof of one obligation MUST NOT discharge another obligation merely because their diagnostics, source text, or pretty-printed propositions are equal.

**[TCB-OBL-006]** Obligation identity MUST include semantic dependencies sufficient to prevent stale or cross-entity reuse.

---

# 12. Control-flow correspondence

Verified path reasoning depends on correct modeling of the C++ control-flow graph and evaluation order.

**[TCB-CFG-001]** Every reachable normal path relevant to a verified postcondition MUST be represented or conservatively rejected.

**[TCB-CFG-002]** Conditions supplied to a path MUST correspond to conditions actually established on that path under C++ evaluation semantics.

**[TCB-CFG-003]** Short-circuit evaluation, sequencing, temporary lifetime, branch polarity, fallthrough, early return, `break`, `continue`, and exceptional edges MUST NOT be approximated in a way that grants facts on paths where C++ does not establish them.

**[TCB-CFG-004]** A call in a condition MUST satisfy its own preconditions before facts derived from its result are made available.

**[TCB-CFG-005]** Facts from one mutually exclusive path MUST NOT leak into another path unless a valid join rule establishes them.

A claim that a path cannot occur (`SPEC.md` `VERIFIED-045`) ends its path in the control-flow model, so nothing after it on that path owes an obligation. The kernel checks the claim's evidence, but only against the facts the model says hold where the claim stands, and so which facts those are, and where the path ends, is correspondence TCB like every other edge. The claim adds no rule and no assumption; its evidence is built by the same refutation an omitted case uses (§19).

**[TCB-CFG-006]** A claim that a path cannot occur MUST be checked against exactly the facts established on the path to the point where it is written, read at the versions current there, and MUST end only that path.

## 12.1 Loops

When loop correctness is established through generated verification conditions rather than a kernel-native loop theorem, the loop-rule implementation is correspondence TCB.

**[TCB-LOOP-001]** The implementation MUST generate entry, preservation and exit obligations required by `SPEC.md`.

**[TCB-LOOP-002]** Every mutation capable of affecting an invariant or post-loop fact MUST be reflected in the loop's carried state or conservative havoc set.

**[TCB-LOOP-003]** `continue`, `break`, return, exceptions and loop-step semantics MUST be accounted for according to the loop form.

**[TCB-LOOP-004]** A partial-correctness loop MUST NOT be used to establish a total-correctness theorem about a value that requires termination.

**[TCB-LOOP-005]** A `decreases` proof MUST be checked on every continuing recursive/iterative path required by `SPEC.md`.

In this implementation the loop and recursion rules are correspondence TCB (`compiler/obligations/src/contracts.cpp`, `compiler/automation/src/composition.cpp`). Every path that continues a loop owes its measure strictly smaller, lexicographically for a list, in the machine's non-wrapping unsigned order, as an obligation the kernel decides; a `do` loop owes its invariant before the body first runs and decides at each iteration's end; a `for` without a condition is left only by `break` or `return`. Functions that call each other form a recursion group, found as a strongly connected component of the verified call graph. Each member states a measure of one length over its parameters, every call within the group owes the callee's measure at the call's arguments strictly below the caller's at its entry values, and each member's conditions suppose the others' contracts as the induction hypothesis. That supposition is the one exception to TCB-CALL-002, and it is sound only by well-founded induction on the shared measure: the group is established, and usable as proven call evidence outside it, only once every member's conditions and descents are proven, and a recursive function without a measure is refused. A contract is reported total only when every loop and callee it depends on terminates (TCB-LOOP-004); a function stating `decreases` that is not is refused. No kernel rule is added: a recursive function is never admitted as a definition.

---

# 13. Function contracts and calls

Function-contract composition is trust-sensitive because callers reason from summaries rather than re-proving callees at every call.

**[TCB-CALL-001]** A contract MUST be bound to the exact C++ callable entity to which it belongs.

**[TCB-CALL-002]** A verified definition MUST discharge its own contract before that contract may be used as proven call evidence, unless an explicit trusted Law supplies the relevant proposition.

**[TCB-CALL-003]** A caller MUST prove every applicable callee precondition at the call site.

**[TCB-CALL-004]** A callee postcondition may be assumed only for normal-return paths to which that postcondition applies.

**[TCB-CALL-005]** Call argument substitution MUST preserve C++ parameter binding, conversions, aliases, reference collapsing, object identity and value categories relevant to proof.

**[TCB-CALL-006]** Recursion, mutual recursion and call cycles MUST NOT create proof by circular use of an unestablished contract.

**[TCB-CALL-007]** A call through virtual dispatch, function pointer, callback, generic callable or other dynamic target set MUST use only guarantees common to every target permitted by the verified dispatch model, unless the dynamic target is itself proven.

---

# 14. Storage, places, regions and logical versions

C++L's storage model is a major correspondence boundary.

A **place** identifies proof-relevant C++ storage. A **logical version** identifies the value of that place after a particular sequence of writes/effects. A **region** identifies the live object/allocation whose storage and lifetime constrain accesses.

**[TCB-MEM-001]** Place identity MUST follow C++ object/subobject identity and MUST NOT be guessed from source spelling alone.

**[TCB-MEM-002]** Every proof-relevant write MUST create or select the correct new logical version.

**[TCB-MEM-003]** Reads MUST observe the logical version current on the corresponding execution path.

**[TCB-MEM-004]** A fact about an earlier logical version MUST NOT be reused for a later version unless a valid preservation argument exists.

**[TCB-MEM-005]** The implementation MUST conservatively account for mutation through references, pointers, captured aliases, globals/statics, callbacks, virtual dispatch, escaped addresses, contained pointer/reference members, foreign code and other C++ access paths.

## 14.1 Aliasing and disjointness

**[TCB-ALIAS-001]** Two places MUST be treated as potentially aliasing unless C++ semantics and checked evidence establish disjointness.

**[TCB-ALIAS-002]** Type-based alias assumptions MUST NOT be used to establish disjointness where doing so presupposes the absence of undefined behavior that verification is itself required to prove.

**[TCB-ALIAS-003]** Distinct member names do not universally imply disjoint storage. Unions, potentially-overlapping subobjects, base subobjects, `[[no_unique_address]]`, bit-fields and implementation-defined layout require the actual C++ object model.

**[TCB-ALIAS-004]** A write MUST invalidate every proof fact whose place may alias the target unless the write semantics re-establish that fact for the resulting version.

**[TCB-ALIAS-005]** Repeated actual arguments that alias the same storage MUST share one post-state model.

**[TCB-ALIAS-006]** Two places MUST be treated as potentially distinct unless C++ semantics and checked evidence establish that they are the same place. Identity and aliasing are separate questions with opposite conservative answers, and both require evidence: treating distinct places as one transports a fact from storage that never held it, exactly as treating aliasing places as disjoint keeps a fact that a write destroyed (`TCB-ALIAS-001`). Where a place is selected by a computed value, its identity includes that value, so selections whose selectors are not established equal are distinct for transporting a fact while remaining potentially aliasing for invalidating one.

## 14.2 Effects

**[TCB-EFFECT-001]** A verified effect summary MUST be derived from checked semantics and bound to the exact callable identity.

**[TCB-EFFECT-002]** A call without a sufficient checked effect summary MUST conservatively invalidate every mutable place it may affect.

**[TCB-EFFECT-003]** By-value passing of a pointer, reference-containing object, view, iterator, callback or handle MUST NOT be interpreted as proving that caller storage is unaffected.

**[TCB-EFFECT-004]** `const` restricts particular C++ access paths; it MUST NOT be treated as a global frame condition.

## 14.3 Element observation

An array element read at a symbolic index is modeled by the indexed observation defined in `FOUNDATIONS.md` §45. The formal observation is total, so the correspondence between it and the C++ subscript carries the definedness requirement.

**[TCB-ELEM-001]** The claim that a C++ subscript expression denotes the element observation of that array's modeled value at that index term is a correspondence assumption. Selecting the wrong subject, the wrong index term or the wrong element type is a correspondence-TCB defect.

**[TCB-ELEM-002]** The extent used in the bounds obligation MUST be the extent of the accessed array's own C++ type, including when that extent is symbolic under a template. An extent taken from another array, another specialization or a capability MUST NOT be substituted for it.

**[TCB-ELEM-003]** The index term used in the bounds obligation MUST be the same formal term the observation is formed at. Reconstructing, renormalizing or re-deriving the index separately for the two uses is a correspondence-TCB defect, because a proof would then bound a term the read does not use.

**[TCB-ELEM-004]** Forming an element observation MUST NOT contribute bounds, liveness, initialization, readability or writability evidence. Totality of the formal observation is a typing property only, and MUST NOT be reported as definedness of the C++ access.

**[TCB-ELEM-005]** Element observation is a read model. It MUST NOT be used to write an element or to create a logical version; writes remain governed by §14.

---

# 15. Memory capabilities

C++L memory propositions such as `readable(...)` and `writable(...)` describe proof-relevant access validity defined by `SPEC.md`.

The implementation MAY check these through a dedicated capability calculus rather than the general logical kernel, but that does not make them untrusted implementation detail.

**[TCB-CAP-001]** Any checker or flow analysis whose incorrect behavior can grant an invalid memory capability is part of the end-to-end TCB for memory-safety claims.

**[TCB-CAP-002]** A capability MUST arise only from semantics that establish it, from checked proof/contract evidence, or from an explicit trusted assumption permitted by `SPEC.md`.

**[TCB-CAP-003]** "Needed by the operation" is not evidence. The verifier MUST NOT insert a capability hypothesis merely because a dereference or write requires one.

**[TCB-CAP-004]** Non-nullness MUST NOT imply lifetime, provenance, bounds, alignment, initialization, readability, writability, ownership or uniqueness.

**[TCB-CAP-005]** Capability state MUST be invalidated or updated when lifetime transitions, may-alias writes, unknown effects, deallocation, object replacement or other relevant C++ events can invalidate it.

**[TCB-CAP-006]** Pointer dereference and subscript checking MUST consume the capability/bounds/lifetime facts required by `SPEC.md`; omission of such a check is a correspondence-TCB defect.

**[TCB-CAP-007]** Bounds proofs that are represented as formal arithmetic propositions MUST still be checked by the formal proof machinery; capability tracking MUST NOT silently decide arithmetic facts it is not specified to decide.

**[TCB-CAP-008]** A `trusted law` that admits a memory proposition expands the trusted-assumption closure; it does not change runtime memory or create a runtime validation.

**[TCB-CAP-009]** A verified call MUST consume every capability its callee's contract states, exactly as it owes the callee's precondition (`SPEC.md` VERIFIED-013, VERIFIED-043): the caller holds a capability of the same kind on the pointer it passes, and where either side states an element count the callee's is proved no larger than the caller's. A caller that passes a pointer it holds nothing for, or a larger region than it holds, hands the callee storage the proof never established it may access. This implementation checks the kind and the pointer in the obligation layer's path walk (`compiler/obligations/src/contracts.cpp`, correspondence TCB) and states the count comparison as an ordinary obligation the kernel decides.

---

# 16. Refinement correspondence

Refinement types have verification identity but erase to their base representation. Soundness therefore depends on checking semantic validity at every crossing and mutation point required by `SPEC.md`.

**[TCB-REFINE-001]** Every base-to-refinement introduction MUST generate the complete semantic-validity obligation for the destination refinement.

**[TCB-REFINE-002]** Recursive semantic validity of refinement-bearing members, elements and bases MUST be propagated exactly as specified by `SPEC.md`.

**[TCB-REFINE-003]** A refined parameter's entry validity is an entry premise of the verified claim; unverified construction history MUST NOT be treated as proof of invalidity or validity beyond the boundary rule defined by `SPEC.md`.

**[TCB-REFINE-004]** A mutation affecting a refinement-bearing place MUST invalidate stale membership facts and establish the destination validity required for the new logical value.

**[TCB-REFINE-005]** Equal erased representation MUST NOT manufacture refinement evidence.

**[TCB-REFINE-006]** Indexed-refinement identity and argument substitution MUST use resolved semantic identity, not textual spelling.

**[TCB-REFINE-007]** Erasure of refinements MUST preserve the base representation and MUST NOT add hidden validation or runtime tags.

**[TCB-REFINE-008]** A declaration collision caused by refinement erasure MUST be diagnosed rather than relying on an ABI distinction that does not exist.

**[TCB-REFINE-009]** Deriving that a value read from storage satisfies that storage's declared type MUST rest on closed accounting: validity established for the version by which the storage entered the modeled state, and the same requirement charged at every operation that may establish a later version. The set of those operations MUST be established rather than assumed, and where it cannot be, the derivation MUST be withheld and the storage invalidated conservatively or the body refused.

Indirection does not by itself disqualify storage — what disqualifies it is a writer the model cannot account for — but an alias is never itself evidence. A pointer to a refined type erases to a pointer to its representation, so the pointer value says nothing about what the pointee holds, exactly as equal erased representation is not evidence of a refinement (`TCB-REFINE-005`). An implementation MAY therefore close the accounting for storage reached through an alias, and MUST NOT treat having reached it through one as the accounting.

The derivation states a fact about the current logical version and never a new obligation. Demanding the predicate again where the value is read would charge one refinement crossing twice (`SPEC.md` REFINE-060, REFINE-061, REFINE-062).

---

# 17. Arithmetic, conversions and undefined behavior

A proof about C++ execution is meaningful only when the formal model matches the selected C++ semantics on the admitted domain.

**[TCB-ARITH-001]** Machine-integer operations MUST be lowered only to formal primitives whose semantics match the resolved C++ operation for every admitted operand.

**[TCB-ARITH-002]** Signed overflow, invalid division, invalid shifts, invalid conversions and other undefined/erroneous behavior MUST NOT be replaced by mathematical-integer semantics merely to make a proof succeed.

**[TCB-ARITH-003]** If a C++ operation is partial because some executions have undefined behavior, verification MUST establish the definedness precondition before using a total formal operation to model that execution.

**[TCB-ARITH-004]** Integer promotions and usual arithmetic conversions MUST be modeled according to the selected C++ semantics.

**[TCB-ARITH-005]** Floating-point reasoning MUST account for the floating representation and operations promised by `SPEC.md`; mathematical real arithmetic MUST NOT silently replace IEEE/C++ floating semantics.

**[TCB-UB-001]** Undefined behavior MUST NOT be used as a proof principle.

**[TCB-UB-002]** The absence of an observed runtime failure does not establish defined behavior.

**[TCB-UB-003]** A verifier may reject semantics it cannot model, but MUST NOT silently assume the undefined-behavior precondition.

In this implementation (RFC 0019) the precondition TCB-ARITH-003 names is an
obligation of its own. A signed `+`, `-`, `*` or unary `-` is stated with the
wrapping primitive and owes `add_fits`, `sub_fits` or `mul_fits` of its operands;
`/` and `%` are stated with `quot` and `rem`, total by definition, and owe a
nonzero divisor and, signed, that the operands are not the least value and `-1`;
a conversion is stated with `convert` and owes, for a signed target, that the
value fits. The wrapping primitive is modular for every operand, so a valid
signed operation such as `-1 + -1` still reduces at the level of the encoding;
what the verifier relies on is that, once the obligation is discharged, the
kernel's linear rule proves the primitive's value, read as a signed value, equal
to the exact C++ result. The obligation is proven from what the path
established before the evaluation, never from the result, a later guard or the
postcondition, and no modular identity stands in for it. The primitives are logical TCB: their
typing, their folding on literals and the constraints linear arithmetic states
for them (`kernel/src/context.cpp`, `kernel/src/arithmetic.cpp`,
`kernel/src/term.cpp`, `kernel/src/linear.cpp`, about 550 lines with comments).
They add no rule, axiom or assumption; each constraint is satisfied by every
machine assignment, and where a primitive's definition is not linear nothing is
stated, except that a product of two unknowns is decided where the ranges its
operands always lie in, their types' or the narrower types a widening
conversion took them from, keep every product within the type.

Which operation owes which condition, and where, is correspondence TCB
(`compiler/obligations/src/definedness.cpp`, `compiler/obligations/src/contracts.cpp`):
every operation a runtime path evaluates owes its condition under what the path
supposes before it, supposing only the postconditions of calls sequenced before
it, and a specification states the conditions of its operations as part of
what it states. The conversions C++ performs are read from the ones Clang
recorded (TCB-ARITH-004) and are never derived again: each operation is stated
at the common type Clang gave it after the integral promotions and the usual
arithmetic conversions, and an operand not already of that type is refused.
Types whose promotion is not modeled (`char8_t`, `char16_t`, `char32_t`,
`wchar_t`, bit-fields) fail closed where they are named. The refutation search,
which proposes the certificates, stays untrusted (§6).

---

# 18. Equality, logical connectives and quantifiers

Formal equality and logical connectives are checked by the logical TCB, while their correspondence to source specification syntax is handled by the correspondence TCB.

**[TCB-LOGIC-001]** The recognizer/elaborator MUST distinguish formal `Eq<T>(a,b)` from ordinary C++ names according to `SPEC.md`.

**[TCB-LOGIC-002]** `&&`, `||`, implication and equivalence in specification context MUST lower according to their formal semantics and precedence, not according to an approximate textual parser.

**[TCB-LOGIC-003]** C++ short-circuit behavior relevant to definedness of lifted operands MUST be represented exactly where `SPEC.md` requires C++ evaluation semantics.

**[TCB-LOGIC-004]** Universal/existential binder scope and substitution MUST be capture-safe.

**[TCB-LOGIC-005]** `assume` MUST name context-supplied evidence only; it MUST NOT create a new proposition.

**[TCB-LOGIC-006]** `exact`, `apply`, `rewrite`, `cases`, `decompose`, `induction` and automation MUST ultimately produce evidence checked against the intended goal or invoke another explicitly trusted checker defined by this trust model.

---

# 19. Case analysis, decomposition and representation models

Structural proof features can be logically sound while still depending on a representation correspondence that is wrong. This correspondence is TCB.

**[TCB-DECOMP-001]** A representation model/provider that defines the state partition of a C++ type is part of the correspondence TCB unless an independent checker derives the partition from authoritative semantics.

**[TCB-DECOMP-002]** A provider defect that omits, merges, invents, or mischaracterizes a C++ state can be a soundness defect when generated case assumptions no longer correspond to runtime states. It MUST NOT be claimed that a bad provider can only cause incompleteness.

**[TCB-DECOMP-003]** Exhaustiveness MUST cover every state required by `SPEC.md`, including residual states such as unnamed enumeration values and `std::variant` valueless state where applicable.

**[TCB-DECOMP-004]** Payload bindings MUST denote the actual logical subobject/observation represented by the arm; they MUST NOT be invented values.

**[TCB-DECOMP-005]** Pointer case decomposition MUST establish only null/non-null state unless additional capability/lifetime facts are independently available.

**[TCB-DECOMP-006]** A product decomposition MUST be obtained from the resolved type rather than from whatever declaration cursors the C++ authority happens to expose, and MUST account for every part of the object, base subobjects included. A route that reports no members for an instantiated class template, or no base for a derived one, does not report an empty object: it reports nothing, and a decomposition built from it would identify values that differ in the part left out (`TCB-DECOMP-002`).

A case omission, `omit label by contradiction evidence;` (`SPEC.md` `CASE-004`,
`CASE-011`), adds nothing to this TCB. Its claim is discharged by ordinary kernel
evidence: the named evidence and the premises standing in the case, including the
discriminator the provider supplies, are refuted into `False` by linear
arithmetic whose certificate the kernel checks, and the claim is recorded and
checked as an obligation of its own. The kernel rules involved are linear
arithmetic and, where a goal is closed from the contradiction, falsity
elimination (§5.1); neither is specific to cases. Axioms: 0. Assumptions: 0.
The certificate search is an untrusted producer (§6). What an
omission rests on is what an arm rests on: that the provider's discriminator for
the omitted case is the right one (TCB-DECOMP-002). A discriminator that stated
the wrong condition could make a possible state look contradictory, exactly as
it could hand an arm a false premise. Omission therefore adds no correspondence
of its own, and a provider defect is as much a soundness defect for it as for an
arm.

**[TCB-DECOMP-007]** A case split on a runtime path (`SPEC.md` 20.7) adds no kernel rule, axiom or assumption, but it is a verification-condition split, and the decision to split lies in the correspondence layer the same way a conditional's does (§12.1). Each arm's conditions MUST suppose exactly the proof-side split's facts for that case, and every state of the partition MUST have exactly one arm walked; a state with no arm would drop its path from verification without any kernel ever seeing it. The path walk re-checks that coverage itself rather than trusting the split it was given. The subject MUST be read through the one storage read, so a case fact names the version current where the split stands and never a later one. TCB delta: the split's path walk (`compiler/obligations/src/contracts.cpp` `split_path`) and its lowering (`clang/src/bridge.cpp` `lower_split`).

## 19.1 Standard representation obligations

For each modeled standard representation, the correspondence TCB must correctly identify semantic identity and public state space. At minimum:

```text
scoped enum
    every underlying-domain value, named distinct values, unnamed residual;
    each enumerator's value read at the underlying type's width and signedness,
    so an unsigned enumerator with its top bit set is never taken as negative

std::variant
    one state per alternative index plus valueless state

std::optional
    some / none

std::expected
    value / error

pointer
    null / non_null only

product decomposition
    existing component subobjects in specified order; no invented alternative states
```

Recognition by spelling alone is insufficient where aliases, namespaces, templates or substitution can change semantic identity.

---

# 20. Induction and termination trust

**[TCB-TERM-001]** A proof computation whose soundness relies on termination MUST not be admitted through an unchecked recursive evaluator.

**[TCB-TERM-002]** Termination measures MUST be interpreted under the well-founded ordering specified by `FOUNDATIONS.md`/`SPEC.md`.

**[TCB-TERM-003]** Recursive call decreases obligations MUST cover every recursive edge in the relevant strongly connected recursion set.

**[TCB-TERM-004]** Lexicographic measures MUST preserve component order and strictness as specified; dropping components or comparing in a different order is a trust defect.

**[TCB-INDUCT-001]** Induction principles are part of the formal calculus or correspondence model defined by the governing documents; their base cases, predecessor relation, range premises and induction hypotheses MUST be generated exactly.

**[TCB-INDUCT-002]** `decreases` is not itself induction evidence and induction is not itself runtime termination evidence.

---

# 21. Objects, classes, construction and destruction

**[TCB-OBJ-001]** Construction reasoning MUST follow C++ initialization order, base/member lifetime rules and selected constructor semantics.

**[TCB-OBJ-002]** A refined member or base MUST satisfy its semantic validity obligation on every construction path that creates the corresponding live subobject.

**[TCB-OBJ-003]** Default member initializers, aggregate initialization, delegating constructors, copy/move operations and temporary materialization MUST NOT bypass refinement or lifetime obligations.

**[TCB-OBJ-004]** Destruction and lifetime end MUST invalidate capabilities and value facts that depend on the destroyed object.

**[TCB-OBJ-005]** `const` member functions and cv-qualification MUST follow C++ alias/mutation semantics; they MUST NOT create global immutability facts.

**[TCB-OBJ-006]** A verified member function's implicit object is a receiver place, and its modeled subobjects are places projected from it by the member and element numbering a member access resolves to (`SPEC.md` CLASS-008). This implementation lowers the receiver to one reference parameter per scalar place; the lowering is correspondence code in the Clang bridge, and it MUST preserve the object's identity and alias relations: an enumeration that skipped a place a body can reach, numbered a member as another, or passed the places of one object as unrelated storage would let a write escape the facts it must invalidate. A member whose storage is not modeled -- a reference member, a bit-field, a member of a union or of an anonymous struct, a volatile member, a pointer -- is left without a place and refused wherever a verified body names it, never read as some other member, and a class whose storage the model does not cover -- a union, a class with a base -- is refused whole.

**[TCB-OBJ-007]** The implicit object is caller storage. Its places relate to every other access path exactly as the common alias model relates them (`BodyLowering::may_alias`, `TCB-ALIAS-001`, `TCB-ALIAS-003`): every other external place, a dereference, a call's write and an unsafe block may reach them, and no type-based argument keeps them apart. Two scalar places of one object are disjoint because C++ never lets two scalar objects that are not bit-fields overlap (`SPEC.md` CLASS-010); no other disjointness is inferred for a subobject form, and the forms that may overlap are refused rather than modeled as apart.

**[TCB-OBJ-008]** A call on an object passes the object's places as the callee's implicit-object arguments (`SPEC.md` CLASS-011, `TCB-ALIAS-005`). Every place the callee may write -- each place of its object when it is not `const`, a `mutable` member when it is, each argument its reference parameter may write -- receives a fresh post-call version, of which only the callee's established postcondition is supposed; so does every place it only reads that the common alias model does not keep apart from one it writes, and every place when it may write through a pointer. A place it only reads that no write of the call can reach keeps its version, and the callee's postcondition is supposed of it at that version. Which places those are is decided from the callee's declaration and the caller's own storage, never from the callee's body (`ARCHITECTURE.md` ARCH-CALL-001). A receiver a pointer parameter designates is formed as dereference places under the capability the contract states for the pointer, `readable` for every place and `writable` as well for each the callee may write (`SPEC.md` VERIFIED-038).

**[TCB-OBJ-009]** A refined place a function receives by reference -- a place of its implicit object or a reference parameter -- is not charged its refinement at a normal return merely because the function returns (`SPEC.md` CLASS-010, REFINE-060 to REFINE-062). Its validity at return is derived from closed accounting kept by the Clang bridge: the version it entered with holds the refinement, a write charged the place's refinement establishes a valid version, a call's effect charged at the caller does, and a write through another access path that may reach the place is charged the place's refinement too and leaves it valid exactly when it was valid before. Every other version -- one an unsafe block, a loop head, a call writing another argument or any route not listed left -- is not derived valid, and the return is charged the refinement for it. This accounting replaces an obligation the kernel used to check at every return, so an error in it -- a route that establishes a version without charging it and is still counted as valid -- would let an invalid value reach a caller that supposes validity. That is why absence is the default: a version is valid only where one of the listed routes marked it (`TCB-REFINE-009`).

The member-function model adds no kernel rule, no axiom and no logical assumption: a member function is the verified callable a function is, with more parameters. It grows the correspondence TCB by the receiver lowering, the implicit-object resolution of `this` and member accesses, the object places of a member call -- a pointer's included -- and the validity accounting of places received by reference, all in the Clang bridge (section 7).

## 21.1 Virtual dispatch

**[TCB-VIRTUAL-001]** Override checking MUST preserve the substitutability rules defined by `SPEC.md`.

**[TCB-VIRTUAL-002]** A virtual call may rely only on pre/post/effect guarantees valid for the dynamic target set admitted by the call.

**[TCB-VIRTUAL-003]** Devirtualization used for proof MUST be justified by the same dynamic-type facts required by C++ execution semantics.

**[TCB-VIRTUAL-004]** Until override substitutability is checked, `verified` on a virtual function and a call to a virtual function from verified code MUST be refused (`SPEC.md` CLASS-014). Whether a function is virtual is read from Clang's resolved declaration, so an override that does not say it is one is refused as surely as one that does.

---

# 22. Templates, constexpr and compile-time C++

Templates are ordinary C++ mechanisms whose instantiated semantics are resolved by the selected C++ authority.

**[TCB-TEMPLATE-001]** Verification MUST be performed against the semantically instantiated entity, including substitutions that affect contracts, refinements, effects and proof expressions.

**[TCB-TEMPLATE-002]** A proof for one instantiation MUST NOT be reused for another instantiation unless semantic identity and all relevant dependencies are identical or a generic proof establishes the required theorem.

**[TCB-TEMPLATE-003]** Template constraints/concepts MUST NOT be promoted to formal proof facts unless `SPEC.md` explicitly gives them that meaning or a checked correspondence establishes the fact.

**[TCB-TEMPLATE-004]** Explicit instantiation and cross-TU template use MUST preserve the same verification metadata and trust dependencies as ordinary definitions.

**[TCB-CONSTEXPR-001]** A C++ compile-time result may be used as formal evidence only when the trust model explicitly relies on the selected C++ constant-evaluation semantics and the expression is within the modeled domain.

**[TCB-CONSTEXPR-002]** Compiler constant folding is not, by itself, proof evidence for an arbitrary formal proposition.

---

# 23. Exceptions and abnormal exits

**[TCB-EXCEPT-001]** Normal-return postconditions MUST NOT be assumed on exceptional exits.

**[TCB-EXCEPT-002]** If a proof depends on exception freedom, that property MUST be established by checked semantics or an explicit trusted assumption; it MUST NOT be inferred from the absence of an exception specification.

**[TCB-EXCEPT-003]** Stack unwinding, destructor execution and mutation occurring on exceptional paths MUST be included when they can affect facts used by a subsequent verified claim.

**[TCB-EXCEPT-004]** `noexcept` and exception specifications MUST be interpreted according to C++ semantics; they do not mean that the underlying code has been formally proved not to encounter every abnormal condition unless that is what the semantic rule establishes.

---

# 24. Concurrency and atomics

Concurrency creates behaviors not captured by purely sequential reasoning.

**[TCB-CONCUR-001]** A verified concurrent claim MUST use a concurrency model sufficient for the C++ memory-order, synchronization and interference properties on which the claim depends.

**[TCB-CONCUR-002]** Sequential value/version facts MUST NOT survive possible concurrent interference unless synchronization or ownership evidence justifies preservation.

**[TCB-CONCUR-003]** Data-race freedom MUST NOT be assumed merely because a sequential proof succeeds.

**[TCB-CONCUR-004]** Atomic operations MUST be modeled with their selected memory orders and permitted observations when those details affect the theorem.

**[TCB-CONCUR-005]** Unsupported concurrency semantics MUST fail closed for the stronger concurrent assurance claim; `unsafe` may mark runtime execution without creating proof facts.

---

# 25. Trusted Laws and explicit assumptions

`trusted law` is the production trusted proposition-admission surface defined by `SPEC.md`.

**[TCB-TRUST-001]** A trusted Law MUST be explicit in source or in an equivalently explicit imported trusted interface artifact.

**[TCB-TRUST-002]** A trusted Law MUST be reported as `TRUSTED`, never as independently `PROVEN`.

**[TCB-TRUST-003]** A theorem derived from a trusted Law MAY be `PROVEN` relative to that premise, but its trust closure MUST include the trusted Law transitively.

**[TCB-TRUST-004]** Unsupported semantics, proof failure, timeout, solver `unknown`, unverified code, unsafe code, compiler crash or missing metadata MUST NOT create a trusted assumption automatically.

**[TCB-TRUST-005]** Trusted assumptions MUST be identified by stable semantic identity and source/import provenance, not only by display name.

**[TCB-TRUST-006]** Changing the proposition, premise, parameters or semantic identity of a trusted Law MUST invalidate every cached/imported claim whose trust closure depends on the previous assumption.

**[TCB-TRUST-007]** A trusted memory proposition such as `readable(...)` or `writable(...)` is an explicit assumption about the modeled storage relation; it does not perform runtime checking or mutate storage.

**[TCB-TRUST-009]** When a proof names a trusted Law as evidence (`SPEC.md` TRUSTED-006), the premise supposed for it MUST be exactly the proposition that Law states, over its parameters and under its premise. A component that could suppose a different proposition under the Law's name would hide an assumption behind a reported one, so the construction of that premise is part of the correspondence TCB.

In this implementation a trusted Law admitting a memory proposition (`SPEC.md` TRUSTED-003) is recorded apart from Laws, because it has no kernel proposition: it is never lowered, never supposed as a premise and never counted as proven, and the trust report lists it `TRUSTED` with its location, a content-derived identity and what it admits. No statement can name one as evidence, so no claim rests on one and the report lists it as unused. Only an explicit `trusted law` may state a memory proposition; an ordinary Law or a proof claiming one is refused. This adds no kernel rule, axiom or logical assumption beyond the one the author declares.

## 25.1 No hidden assumptions

The following are forbidden as hidden assumption sources:

```text
foreign binding annotations not reported as trust
compiler intrinsics silently treated as theorems
solver answers accepted without declared trust/certificates
standard-library models silently assumed exact
platform documentation silently treated as proof
runtime assertions silently promoted to static evidence
optimization facts silently promoted to contracts
unverified function declarations silently treated as verified summaries
```

**[TCB-TRUST-008]** Every proposition admitted without derivation MUST be traceable to an explicit trusted source or an explicitly enumerated primitive calculus rule.

---

# 26. `unsafe`, unverified code and runtime validation

`unsafe`, `UNVERIFIED`, `RUNTIME-CHECKED`, and `TRUSTED` are distinct assurance concepts.

## 26.1 Unsafe

**[TCB-UNSAFE-001]** `unsafe` permits runtime execution across a boundary where C++L does not establish its strongest static guarantees; it MUST NOT create proof evidence.

**[TCB-UNSAFE-002]** Effects of unsafe code on verified storage MUST be modeled conservatively unless a checked contract or trusted Law supplies the relevant facts.

**[TCB-UNSAFE-003]** An unsafe result MUST NOT acquire refinement membership, capability, range, lifetime, provenance or other formal facts merely because it crossed an unsafe marker.

In this implementation the model of an unsafe block is correspondence TCB, like the rest of body lowering (`clang/src/bridge.cpp`). A block's statements are never lowered. Every place it could have written receives a fresh value that inherits no earlier fact and no refinement: what a pointer designates, the storage a reference parameter designates, and every local whose address the body takes or that any unsafe block of the body names, since a block may keep an address and write through it later from a block that never names it. A parameter the body does not follow and a block may rebind is refused. From the block on, the path holds none of its contract's capabilities, for a dereference in the bridge and for a verified call in the obligation layer's path walk (`compiler/obligations/src/contracts.cpp`). Control that leaves a block other than by its end is refused. The trust closure attributes each block to the contract whose body holds it and, to a fixed point over verified calls, to every contract calling that one; the report lists each such contract, and each path or case claim of such a body, with every block it rests on, and counts none of them assumption-free (TCB-REPORT-005, TCB-REPORT-006). None of this adds a kernel rule, axiom or logical assumption: an unsafe block makes fewer facts available, never more.

## 26.2 Unverified code

**[TCB-UNVERIFIED-001]** Ordinary unverified C++ may execute normally but cannot contribute formal facts unless those facts are established by an explicit boundary mechanism defined by `SPEC.md`.

**[TCB-UNVERIFIED-002]** An unverified callee without a checked effect summary MUST be conservatively modeled for mutation and other proof-relevant effects.

## 26.3 Runtime validation

**[TCB-RUNTIMECHK-001]** Runtime validation is ordinary runtime behavior, not proof-kernel execution.

**[TCB-RUNTIMECHK-002]** A fact derived after a successful runtime guard is valid only on executions/paths where that guard has succeeded according to C++ semantics.

**[TCB-RUNTIMECHK-003]** The runtime check itself remains part of the executable; erasure MUST NOT remove it merely because proof facts were derived from its successful branch.

**[TCB-RUNTIMECHK-004]** A runtime assertion that may terminate the program is not automatically a universal theorem.

---

# 27. Foreign code and external systems

Foreign code includes C, C++, Objective-C++, assembly, JNI, N-API, OS APIs, device drivers, GPU APIs, dynamically loaded code and any component whose semantics are not internally established by C++L verification.

**[TCB-FFI-001]** Foreign code MUST NOT be treated as verified merely because it has a C++ declaration.

**[TCB-FFI-002]** Formal facts about foreign behavior require one of:

```text
checked verification of the foreign implementation under an accepted model
explicit trusted Law / trusted interface assumption
ordinary runtime validation that establishes a path-local fact
another independently validated proof artifact
```

**[TCB-FFI-003]** A verified wrapper does not prove the wrapped foreign implementation correct. If the wrapper relies on an unverified external contract, that contract remains in the trust closure.

**[TCB-FFI-004]** ABI, calling convention, ownership, lifetime, thread-safety, callback and error/exception behavior required by a foreign interface are part of the correspondence/runtime trust chain.

## 27.1 Environment assumptions

Operating-system, hardware, device, protocol and deployment guarantees MAY appear as explicit trusted assumptions or compatibility/environment assumptions, but MUST NOT disappear from auditability when a theorem depends on them.

---

# 28. Standard library and modeled libraries

A formal model of a library abstraction is a correspondence claim between public library semantics and the runtime implementation used by the executable.

**[TCB-LIB-001]** A library model MUST identify the semantic entity it models by resolved identity, not merely by textual type or function name.

**[TCB-LIB-002]** A model MUST NOT depend on private object layout or implementation details unless those details are explicitly part of the selected compatibility contract.

**[TCB-LIB-003]** If correctness of the model-to-runtime correspondence is assumed rather than verified, that dependency is part of the runtime/correspondence trust chain and MUST be reportable at the appropriate granularity.

**[TCB-LIB-004]** Library version/configuration differences that can change modeled semantics MUST participate in artifact/cache identity and compatibility checks.

**[TCB-LIB-005]** A library abstraction that performs hidden allocation, invalidation, aliasing, synchronization, exception propagation or callback execution MUST expose those proof-relevant effects through its model.

## 28.1 The verified sequence models

This implementation models `std::array`, `std::vector`, `std::basic_string<char>` and dynamic-extent `std::span` for verified code (`SPEC.md` J.17, RFC 0020). Nothing of libc++ or libstdc++ is verified. What is believed about them is listed here in full, and every claim that rests on it says so.

**[TCB-LIB-006]** The **library summaries** of `SPEC.md` STDMODEL-013 are trusted assumptions about the library the program runs with. They are stated once, in `compiler/obligations/src/library.cpp`, as kernel propositions over the one observation each sequence's abstract value has, its length:

```text
construction      length == 0 | k | n | n | literal's characters before its first null
copy              length(copy) == length(source)
move              length(result) == length(source before); source afterwards: nothing
span of v         length(span) == length(v)
push_back, s += c length' == length + 1  and  length < length'
pop_back          requires length != 0;  length' == length - 1  and  length' < length
clear             length' == 0
reserve           length' == length
append, s += t    length' == length + length(t), length <= length', length(t) <= length'
assignment        length' == length(source)
data              nothing
```

A summary is supposed at the call exactly as a verified callee's postcondition is, and only on the call's normal return; its precondition is an ordinary obligation. That each summary holds of a conforming library, and that the library conforms, is assumed.

**[TCB-LIB-007]** The **observations** are correspondence assumptions: that `size()` and `length()` return the length the summaries speak of, that `empty()` is `size() == 0`, that `std::array<T, N>::size()` is `N`, and that `operator[](i)` with `i < size()` designates a live element `i` of the storage the container owns or the span views, of the element type, with no other effect. The bound itself is proved by the kernel, never assumed (`TCB-CAP-007`).

**[TCB-LIB-008]** The **generation rules** of `SPEC.md` STDMODEL-015 are correspondence TCB, in `clang/src/bridge.cpp`: which operations establish a new storage generation, that an element place is matched only at the generation it was formed at, and that a span local or element reference used at another generation is refused. The model assumes, conservatively, that every mutator may reallocate: no capacity is modeled, and no operation is assumed to keep `data()` stable, small-string storage included. What it does assume is the C++ object model's disjointness of live objects: a write to a scalar element, local or referent never reaches a container object's own storage, so it leaves every container's length alone (RFC 0020 §3).

**[TCB-LIB-009]** The capability a contract states over a span or pointer parameter includes, as a precondition the caller owes, that the storage it designates is not element storage of a sequence the function reaches by value or by mutable reference (`SPEC.md` STDMODEL-016). A verified caller discharges it structurally at the call; an unverified caller is trusted to meet it, as it is trusted to meet every other precondition. Reading an `expects` clause that conjoins capabilities with predicates apart, the capabilities on their channel and the predicates as preconditions, is correspondence TCB (`split_conjunction` in `clang/src/bridge.cpp`, `elaborate_contract` in `compiler/elaboration/src/elaborate.cpp`); every other clause that conjoins them is refused whole.

**[TCB-LIB-010]** Every claim resting on a verified function whose contract, body or callees use a modeled sequence is reported under `Library-model-dependent claims` with each model it rests on, and is never counted assumption-free (`SPEC.md` STDMODEL-018). The closure is computed with the trusted-law and unsafe closures, over the same call edges, in `compiler/obligations/src/trust.cpp`. Across translation units it is incomplete: a verification interface (§31) records no library models, so a claim proven through an imported contract names the models that contract's own declaration uses, and not those the other unit's body uses. Such a claim still rests on the imported contract and is never counted assumption-free (§31); only the list of models is short.

TCB delta of the subset: kernel rules 0, axioms 0, term formers 0; trusted assumptions: the summaries and observations above; correspondence: recognition, the place and generation model, span liveness, the call-site disjointness check, and the split of a conjoined `expects` clause.

---

# 29. Erasure and lowering trust

Erasure/lowering connects verified C++L source to ordinary C++ runtime source. It is correctness-critical.

The governing semantic requirement is defined by `SPEC.md`:

```text
Sem_runtime(p) = Sem_runtime(erase(p))
```

for every accepted program within the supported semantics.

**[TCB-ERASE-001]** Proof-only constructs MUST erase without adding, removing or changing observable runtime behavior.

**[TCB-ERASE-002]** Runtime-bearing C++L constructs whose runtime representation is defined by `SPEC.md` MUST lower only to that representation.

**[TCB-ERASE-003]** Erasure MUST NOT remove explicit runtime validation, runtime branches, runtime side effects, required destruction, volatile/atomic operations, lifetime operations or other ordinary C++ behavior.

**[TCB-ERASE-004]** Erasure MUST NOT add hidden assertions, validators, proof interpreters, tags, fields, constructors, dispatch, branches, loops or changed calling conventions.

**[TCB-ERASE-005]** Ghost initialization/destruction may be erased only when the source semantics guarantee that no observable runtime behavior depends on them.

In this implementation that guarantee is checked before a verified body is lowered, and the check is erasure TCB (`clang/src/bridge.cpp`, `GhostScan`): a ghost is an integer or a Boolean local with automatic storage and a value, so it constructs and destroys nothing; its initializer has no assignment, increment, allocation, throw, lambda or volatile read, and every call in it is to a `pure` function, which elaboration confirms; and no reference to it stands anywhere in the body outside another ghost's initializer or a specification expression the projector declared under its own prefix, a prefix no name the author writes may begin with. The last rule is what makes the analysed and the erased program agree: a use it missed would be verified against a value the program never computes, so each is refused where it stands, whether or not a path reaches it. The erasure validator then confirms the whole declaration, and nothing else, left the program.

**[TCB-ERASE-006]** A mismatch between the analyzed program and the runtime program MUST be an internal verification failure, never a successful weaker assurance result.

**[TCB-ERASE-010]** Erasing a claim that a path cannot occur (`SPEC.md` `ERASE-016`) MUST leave its `;` as an empty statement, so that a statement it was the body of keeps one and no control flow changes. Erasing the `;` with the claim's words would silently make the next statement that body.

## 29.1 Refinement lowering

**[TCB-ERASE-007]** A non-indexed refinement lowers to an ordinary representation equivalent to its base type as required by `SPEC.md`.

**[TCB-ERASE-008]** An indexed refinement lowers without runtime index metadata; proof-only indices MUST NOT become hidden runtime state.

**[TCB-ERASE-009]** Lowering MUST diagnose erased-signature collisions rather than relying on refinement identity at native ABI level.

## 29.2 Independent erasure validation

An implementation SHOULD independently validate erasure/lowering where practical.

Such validation can reduce the trusted erasure implementation only to the extent that the validator checks the complete semantic property needed for runtime preservation.

This implementation's validator (`compiler/erasure/`) checks the runtime text against the analysed text byte by byte. Outside the recognized proof-only spans and runtime-bearing declarations nothing may differ; inside a proof-only span every byte must be blank except a newline, so no proof-only text is left behind; each refinement must be exactly the canonical alias recomputed from its declaration; and the line count must be unchanged. The driver writes the program Clang compiles only after this check passes, and from the text it checked. A failed check is an internal error (`TCB-ERASE-006`).

What the validator does not establish is that the recognized spans are the right ones. It takes each span from the recognizer, so a span that also covered runtime text, such as a `static` beside a `verified` specifier, would be blanked with the validator's approval. The recognizer therefore stays in the correspondence TCB (`TCB-SOURCE-002`). Its spans are checked from outside by tests rather than by the validator: fixtures erased by hand as `SPEC.md` Annex M prescribes must compile to the same assembly as their C++L originals, in every supported standard, and an ordinary C++ client compiled without C++L must link against and call a library written with refinements and contracts. Those tests are evidence about the constructs they exercise; they do not remove the recognizer from the TCB.

---

# 30. Native compiler, linker, ABI and execution trust

The final executable depends on ordinary compiler/toolchain correctness.

**[TCB-RUNTIME-001]** Claims about the native executable depend on the selected C++ compiler correctly implementing the supported C++ semantics used by the verified program.

**[TCB-RUNTIME-002]** Linker symbol resolution, LTO, ABI lowering, calling conventions, object layout and runtime-library selection are part of the runtime trust chain when they can affect the verified behavior.

**[TCB-RUNTIME-003]** Compiler optimization is outside the logical TCB but inside the runtime trust chain for executable-level guarantees.

**[TCB-RUNTIME-004]** Miscompilation must not be described as a failure of the proof kernel; trust reports SHOULD distinguish proof validity from compiler/runtime preservation.

**[TCB-RUNTIME-005]** Hardware and execution-environment behavior required by the theorem is part of the runtime/environment trust chain unless independently verified or constrained by the theorem's scope.

## 30.1 ABI promises

**[TCB-ABI-001]** Where `SPEC.md` promises that verification metadata does not alter native ABI, the lowering and native interface emitted by C++L are runtime-TCB responsibilities.

**[TCB-ABI-002]** Cross-language/foreign ABI assumptions MUST be explicit in compatibility/trust metadata where they affect verified behavior.

---

# 31. Translation units, modules and verification metadata

Native ABI does not carry all proof metadata. Sound modular verification therefore depends on trustworthy verification interfaces.

**[TCB-XTU-001]** Imported metadata MUST be bound to the exact semantic declaration/entity it describes.

**[TCB-XTU-002]** Imported contracts, refinements, effect summaries, proof evidence, purity/termination guarantees and trust closures MUST preserve semantic identity across translation units/modules.

**[TCB-XTU-003]** Missing metadata MUST cause the dependent verification step to fail closed; it MUST NOT be reconstructed by guessing from native signatures.

**[TCB-XTU-004]** Stale metadata MUST NOT remain valid after a semantically relevant declaration, definition, compiler semantic mode, trust assumption or model changes.

**[TCB-XTU-005]** A metadata transport format MAY be implementation-specific, but its correctness is part of the artifact/correspondence TCB unless every imported claim is independently reconstructed and rechecked.

**[TCB-XTU-006]** Interface identity MUST resist accidental collision across overloads, templates, namespaces, modules, generated names and source remapping.

## 31.1 Verification interfaces as implemented

This implementation carries contracts across translation units through a verification interface each unit may write (`--cppl-emit-interface`) and others may import (`--cppl-import-interface`), as `SPEC.md` Annex L.2.1 (TUBOUND-002 to TUBOUND-009) and RFC 0017 define. No evidence crosses: an interface records *that* a unit proved a contract, not a proof term to re-check. The trust this adds is therefore artifact and reuse TCB (2.5), stated here in full.

What the consuming unit does not have to believe, because it checks it itself:

```text
what the contract means     rebuilt from the consumer's own declaration and
                            compared, by a canonical statement identity, with
                            the one recorded (TUBOUND-004); nothing recorded is
                            read as a proposition
which function it is        Clang's USR of the consumer's own declaration, with
                            external linkage, compared with the one recorded
what callers must prove     the consumer's own preconditions, discharged by the
                            kernel at every call like any other obligation
what follows the call       every condition supposing the recorded
                            postcondition is checked by the kernel
```

What it does believe, and so what joins the artifact and reuse TCB:

```text
the producing compile       that the unit which wrote the interface really
                            proved each recorded contract; the consumer never
                            re-checks it (Composition::established)
the emitter                 that an entry is written only for a contract every
                            obligation of which the kernel accepted, with the
                            closure close_trust computed for it
the reader and validator    that a refused interface is refused: format,
                            version, checksum, configuration, staleness,
                            conflicts and dependency closure
                            (compiler/artifact, compiler/driver/src/interface_io)
the statement identity      that two statements with one identity are one
                            contract (obligations/src/interface.cpp); this is
                            correspondence TCB as well, since it decides which
                            proposition the kernel is asked to suppose
the file's integrity        between the write and the read; the checksum
                            detects accidental damage, not a deliberate edit
the object's correspondence that the object linked is the one compiled with
                            the unit that wrote the interface (SPEC.md L.5);
                            nothing binds the two
```

**[TCB-XTU-007]** A claim proven through an imported contract MUST be reported with that contract, the interface that recorded it and the identity of the record, and with every trusted assumption and unsafe dependency the producing unit's proof rested on, transitively across units. It MUST NOT be reported as assumption-free, and the imported contract MUST NOT be counted as proven by the consuming unit.

**[TCB-XTU-008]** An interface MUST be bound to the configuration that gives its records meaning, compared exactly: the compiler version and the digest of the compiler's own executable, the kernel and formal-core versions, the Clang that resolved C++ semantics, the language mode and the target. It MUST also be bound to the content of every file its unit was preprocessed from, so an edit after it was written makes it stale. Content is compared, never a timestamp.

**[TCB-XTU-009]** A record MUST be usable only while every record it rests on is imported with the identity it had when the dependent proof was made, and two records of one function that differ MUST make both unusable. Staleness therefore propagates along the dependency chain rather than stopping at the unit that changed.

**[TCB-XTU-010]** A hand-edited interface whose checksum is recomputed is not detected: the checksum establishes integrity, not authenticity (TCB-ARTIFACT-003). The honest statement is that such an edit can make a caller assume a contract that was never proven; it can never make one kernel-checked, and every claim resting on it is reported as resting on that record (TCB-XTU-007). Authenticating interfaces, or transporting evidence to re-check, is left to a later design (RFC 0017).

TCB delta of this feature: no kernel rule, no axiom, no term or proposition former, no change to what the kernel accepts. The artifact and reuse TCB grows by the components listed above; the reporting TCB grows by the imported-closure propagation in `close_trust` and its report.

---

# 32. Proof artifacts, caches and incremental verification

Caching is a performance feature, never an additional proof principle.

## 32.1 Preferred zero-trust reuse

The preferred design stores proof evidence plus complete semantic dependency identity and rechecks that evidence before use.

**[TCB-CACHE-001]** A cache that stores only a prior Boolean verdict and reuses it without independent validation is part of the artifact/reuse TCB.

**[TCB-CACHE-002]** A cache MAY be outside the proof TCB only when corruption/staleness can at worst cause a cache miss or invalid evidence that a trusted checker rejects.

**[TCB-CACHE-003]** Cache keys/dependency manifests MUST include every semantically relevant input needed to distinguish proof validity.

At minimum where relevant:

```text
formal proposition / obligation identity
resolved C++ entity identity
source/preprocessed semantic hash
formal type and refinement definitions
called contracts/effect summaries
imported Laws and proof artifacts
trusted-assumption identities and contents
core calculus version
checker/kernel version
semantic-model version
selected C++ language mode
target/ABI properties used by the proof
library models and versions
solver/certificate format versions
verification configuration affecting meaning
```

**[TCB-CACHE-004]** A missing dependency MUST invalidate reuse rather than default to compatibility.

**[TCB-CACHE-005]** Incremental invalidation MUST be monotone with respect to uncertainty: uncertainty causes recomputation/refusal, not reuse.

## 32.2 Artifact authenticity

**[TCB-ARTIFACT-001]** Imported proof artifacts MUST be syntactically and semantically validated before use.

**[TCB-ARTIFACT-002]** If artifact authenticity or provenance affects whether evidence is accepted, the authenticity mechanism is part of the artifact TCB.

**[TCB-ARTIFACT-003]** Cryptographic integrity can establish artifact identity/integrity but does not establish theorem validity; proof evidence still requires semantic checking.

---

# 33. Versioning

Proof validity can depend on the meaning of the formal calculus and semantic correspondence rules.

**[TCB-VERSION-001]** Proof artifacts MUST identify the formal calculus version against which they are checked.

**[TCB-VERSION-002]** Checker/kernel changes that alter proof acceptance semantics MUST invalidate incompatible artifacts.

**[TCB-VERSION-003]** Changes to C++ correspondence, memory semantics, erasure rules, effect semantics, standard-library models or other proof-relevant semantic translations MUST participate in verification-artifact compatibility.

**[TCB-VERSION-004]** Version numbers alone are insufficient if two builds with the same nominal version can contain semantically different trust-critical code; reproducible identity SHOULD include content/build provenance sufficient for auditing.

---

# 34. Determinism and reproducibility

**[TCB-DETERMINISM-001]** Given identical formal evidence, checker semantics and configuration, proof checking SHOULD produce the same accept/reject result.

**[TCB-DETERMINISM-002]** Nondeterministic proof search MAY change which proof is found, but MUST NOT change what invalid evidence the checker accepts.

**[TCB-REPRO-001]** A release verification record SHOULD contain enough information to reproduce or independently audit the assurance decision.

Relevant information includes, as applicable:

```text
C++L compiler identity
proof checker identity
formal calculus identity
selected C++ standard/mode
target and ABI
semantic-model versions
solver versions when relevant
trusted-assumption closure
imported artifact identities
verification configuration
proof/certificate hashes
runtime compiler/linker identities when claiming executable preservation
```

**[TCB-REPRO-002]** Reproducibility metadata MUST NOT be mistaken for proof evidence; it supports auditability of the evidence and trust chain.

---

# 35. Trust propagation and provenance

Trust is transitive through proof dependencies.

If theorem `A` depends on theorem `B`, and `B` depends on trusted assumption `X`, then `X` belongs to the trust closure of `A` unless the dependency is eliminated by a proof independent of `X`.

**[TCB-PROV-001]** Trust dependency propagation MUST follow the actual evidence/dependency graph, not source proximity or module boundaries.

**[TCB-PROV-002]** Re-proving a theorem without a trusted dependency MAY remove that dependency only when the accepted evidence no longer references it transitively.

**[TCB-PROV-003]** Renaming, moving or reformatting source MUST NOT accidentally sever a trust dependency if semantic identity is unchanged.

**[TCB-PROV-004]** Reusing a theorem through an alias/import/re-export MUST preserve its trust closure.

**[TCB-PROV-005]** Trust closure computation MUST be cycle-safe and deterministic.

**[TCB-PROV-006]** A claim's reported trust closure MUST include every premise its accepted evidence was checked relative to. Where a closure is derived rather than read from checked evidence, as when it is joined across verified calls, the derivation is reporting TCB, and a dependency it cannot attribute to a reported claim MUST fail closed rather than be left out.

## 35.1 Trust identity

Trusted dependencies SHOULD have stable machine identities derived from semantic declaration identity and proposition content sufficient to distinguish materially different assumptions.

---

# 36. Assurance statuses and reporting integrity

Language statuses are defined by `SPEC.md`. This document defines reporting requirements.

At minimum, tooling must preserve the distinction among:

```text
PROVEN
TRUSTED
RUNTIME-CHECKED
UNSAFE
UNVERIFIED
UNRESOLVED
```

**[TCB-REPORT-001]** Tooling MUST NOT collapse these statuses into a generic `verified` indicator when doing so hides material assurance differences.

**[TCB-REPORT-002]** A `PROVEN` theorem with non-empty trusted closure MUST report that closure or an unambiguous indication that trusted dependencies exist.

**[TCB-REPORT-003]** A trusted Law MUST NOT be counted as a proven Law.

**[TCB-REPORT-004]** A runtime-checked fact MUST NOT be displayed as a universal static proof.

**[TCB-REPORT-005]** An unsafe/unverified dependency MUST NOT disappear from an assurance report merely because a higher-level function also has some proven properties.

**[TCB-REPORT-006]** Reporting code that can turn a weaker assurance state into a stronger displayed state is part of the reporting TCB.

## 36.1 Per-claim report

For each reportable theorem/contract, the trust system MUST be capable of representing:

```text
semantic claim identity
status
formal proposition identity/hash
proof/evidence identity when applicable
trusted-assumption closure
runtime-check dependencies
unsafe/unverified dependencies relevant to the claim
external/library/environment assumptions
checker/calculus identity
source/entity identity
verification metadata identity
```

## 36.2 Build-level report

A build-level report MUST aggregate without losing per-claim provenance. Counts alone are insufficient when trusted assumptions exist; each assumption must be enumerable.

---

# 37. Strict assurance policies

A toolchain MAY provide policy modes that reject otherwise valid programs because their trust posture does not satisfy a deployment policy.

Examples include policies requiring:

```text
empty trusted-assumption closure
no unsafe regions reachable from selected claims
no unverified foreign dependencies
no directly trusted solver results
no unresolved obligations
reproducible proof artifacts
specified compiler/toolchain identities
```

**[TCB-POLICY-001]** Policy rejection MUST NOT change theorem semantics.

**[TCB-POLICY-002]** A policy mode MUST NOT relabel `TRUSTED` as `PROVEN`; it may only accept/reject based on the existing trust graph.

**[TCB-POLICY-003]** Policy configuration affecting acceptance MUST be included in audit/reproducibility metadata.

---

# 38. Security-sensitive trust failures

A defect is trust/security-sensitive when it can cause any of the following:

```text
invalid core evidence accepted
wrong source proposition proven as though it were the intended one
required obligation omitted
stale logical version reused after mutation
invalid memory capability granted
incorrect alias disjointness assumed
trusted assumption hidden or dropped from closure
trusted result displayed as independently proven
unsafe/unverified behavior displayed as verified
invalid proof artifact/cache entry reused
proof certificate accepted without required checks
runtime validation erased or bypassed
proof-only state affects runtime behavior
runtime-bearing source erased incorrectly
verified source and compiled runtime source diverge
native ABI differs from the guaranteed erased ABI
wrong cross-TU metadata bound to a declaration
wrong representation state partition used in proof
compiler/library/environment assumption hidden from an executable-level claim
```

**[TCB-SEC-001]** Such defects MUST be handled under the project's security policy when they can cause a false assurance claim or materially hide its trust dependencies.

**[TCB-SEC-002]** Denial of service, poor diagnostics or proof incompleteness are not automatically soundness bugs, but MAY still be security bugs under `SECURITY.md`.

**[TCB-SEC-003]** A change that turns a fail-closed case into acceptance without adding sufficient checked semantics requires trust review.

---

# 39. TCB growth and change policy

Trust expansion is a first-class design change.

**[TCB-CHANGE-001]** Any change that adds a component, rule, assumption source, direct solver dependency, cache-trust mechanism, semantic shortcut, representation model or runtime equivalence dependency to the TCB MUST be explicitly identified in review.

**[TCB-CHANGE-002]** The preferred direction is:

```text
trusted implementation
    ↓
independently checked evidence/certificate
```

not the reverse.

**[TCB-CHANGE-003]** Moving logic from the proof kernel into correspondence code is not automatically a TCB reduction; if the correspondence code remains unchecked and can create unsound source claims, trust has only moved.

**[TCB-CHANGE-004]** Adding an independently checked certificate format MAY reduce trust only after the checker validates every semantic fact formerly trusted.

**[TCB-CHANGE-005]** A new `trusted` source form is a language-semantic change and requires corresponding `SPEC.md` authority; TRUST.md alone MUST NOT create one.

**[TCB-CHANGE-006]** A new proof-relevant feature MUST document which existing trust layers it uses and any new trust obligations it introduces before it may be reported as fully conforming.

---

# 40. Verification of the TCB

The TCB SHOULD receive stronger assurance than ordinary compiler convenience code.

Recommended assurance techniques include:

```text
small interfaces
negative/adversarial unit tests
property tests
fuzzing
mutation testing
differential testing against independent semantics
sanitizers
reproducible builds
code review requiring trust-impact analysis
formal specification
mechanized meta-theory
formal verification of critical checkers/lowering
independent implementations for cross-checking
```

These techniques improve confidence but do not alter the trust classification unless they establish an independently checked boundary.

## 40.1 Kernel testing

**[TCB-TEST-001]** The logical checker MUST have negative tests for malformed evidence, wrong types, wrong binders, forged hypotheses, substitution capture, invalid certificates and every primitive inference rule.

## 40.2 Correspondence testing

**[TCB-TEST-002]** Each correspondence rule MUST have positive and negative source-level tests demonstrating both the intended accepted case and the nearest unsound case that must be rejected.

**[TCB-TEST-003]** Cross-feature tests MUST cover interactions capable of invalidating facts, especially aliasing, calls, loops, exceptions, templates, virtual dispatch, concurrency, refinements and memory capabilities.

## 40.3 Erasure/runtime testing

**[TCB-TEST-004]** Erasure/lowering MUST have tests that compare runtime representation/behavior where `SPEC.md` promises preservation.

**[TCB-TEST-005]** ABI-sensitive features MUST have ABI/codegen conformance tests across supported toolchain modes as defined by `COMPATIBILITY.md`.

## 40.4 Trust-report testing

**[TCB-TEST-006]** Trust reporting MUST have tests proving that transitive assumptions are neither lost nor incorrectly added, including cross-TU/imported evidence.

---

# 41. Formal verification and mechanized meta-theory

Mechanized meta-theory can reduce informal trust in the formal core, but only to the extent of the mechanized statement and the trusted theorem-prover/checker chain used to establish it.

Potential properties include:

```text
consistency relative to stated assumptions
substitution
preservation/progress for proof terms
normalization of proof-relevant fragments
soundness of arithmetic certificate checking
soundness of induction/termination rules
erasure preservation
refinement validity preservation
memory capability calculus soundness
```

**[TCB-META-001]** Mechanized proofs MUST identify the formal model/version to which they apply.

**[TCB-META-002]** A mechanized theorem about an abstract compiler pass does not remove trust from the production implementation unless a verified correspondence connects the implementation to that model.

**[TCB-META-003]** External proof assistants/toolchains used to certify meta-theory have their own trust bases; those dependencies SHOULD be documented when claims rely on them.

---

# 42. Relationship between proof trust and runtime trust

C++L deliberately distinguishes these questions:

```text
1. Is the formal proposition established?
2. Is that proposition the correct formalization of the C++L source?
3. Does the compiled executable preserve the verified source behavior?
```

A `PROVEN` formal proposition can be mathematically correct while an executable-level claim is invalid because correspondence, erasure or native compilation is wrong.

Conversely, a program may execute correctly even when no formal proof exists.

**[TCB-SEPARATION-001]** Tooling and documentation MUST NOT conflate these three assurance questions.

**[TCB-SEPARATION-002]** Reports SHOULD allow users to distinguish at least proof validity, source correspondence and runtime preservation dependencies.

---

# 43. Conformance requirements

A complete conforming C++L implementation MUST satisfy all applicable requirements of this document.

In particular it MUST:

1. identify the logical, correspondence, runtime, artifact/reuse and reporting trust layers;
2. reject invalid formal evidence;
3. preserve explicit trusted-assumption provenance transitively;
4. prevent hidden assumptions from entering accepted proof;
5. prevent unsupported semantics from being promoted to proof or trust automatically;
6. ensure source-to-core correspondence for every accepted verified construct;
7. generate every proof obligation required by `SPEC.md`;
8. model control flow without leaking path facts;
9. model calls, effects, aliases, places and logical versions conservatively;
10. check memory capabilities and bounds according to `SPEC.md`;
11. enforce recursive refinement validity across construction, mutation and boundaries;
12. model C++ arithmetic and undefined-behavior preconditions without mathematical substitution that changes semantics;
13. treat representation/decomposition models as correspondence-TCB responsibilities;
14. preserve termination/partial-correctness distinctions;
15. preserve class, constructor, destructor, virtual, template, exception and concurrency trust obligations;
16. preserve foreign/library/environment trust dependencies;
17. erase/lower C++L constructs without changing required runtime semantics;
18. preserve promised ABI behavior;
19. bind cross-TU/module verification metadata to exact semantic entities;
20. reuse caches/artifacts only with sufficient semantic dependency validation;
21. report assurance statuses and trusted closures accurately;
22. treat trust expansion as an explicit reviewed change; and
23. fail closed whenever required trust-critical information cannot be established.

A toolchain that omits a language feature MAY be incomplete, but it MUST NOT claim conformance for that feature by accepting a weaker trust interpretation.

---

# 44. Non-goals of this document

This document does not specify:

```text
concrete parser architecture
VIR class layouts
source file paths
solver search algorithms
cache database schema
CLI command names
IDE UX
release schedule
implementation maturity
current test counts
current kernel version numbers
current unsupported features
roadmap ordering
```

Those belong to implementation/status/tooling documents.

This document also does not require zero trust. Native execution necessarily depends on some implementation and environment unless the entire stack is independently verified.

The objective is **explicit, minimal, auditable, reducible trust**.

---

# 45. Fundamental trust rule

For every claim reported as `PROVEN`, C++L MUST be able to account for:

```text
what proposition was established
what source/runtime semantics that proposition represents
what evidence established it
what checker accepted the evidence
what explicit trusted assumptions it depends on
what correspondence components must be correct
what runtime components must be correct for executable-level meaning
what imported artifacts/models it depends on
```

The acceptable answer is never merely:

```text
because the compiler said so
```

The governing principle is:

> **Proof authority should be small; correspondence trust should be explicit; runtime trust should be separated; assumptions should be visible; and every uncheckable gap should fail closed rather than silently become truth.**

---

# Annex A (normative) — Trust classification matrix

This annex is normative.

The table classifies common components by default. A concrete implementation MAY reduce trust through independent checking, but MUST document the checker and the exact property it validates.

| Component / responsibility                           |                             Logical TCB |                 Correspondence TCB |                    Runtime TCB |            Artifact/reporting TCB | Notes                                            |
| ---------------------------------------------------- | --------------------------------------: | ---------------------------------: | -----------------------------: | --------------------------------: | ------------------------------------------------ |
| core proof checker                                   |                                     yes |                                 no |                             no |                                no | validates formal evidence                        |
| definitional equality / normalization used by kernel |                                     yes |                                 no |                             no |                                no | acceptance-critical                              |
| kernel type/binder/substitution checker              |                                     yes |                                 no |                             no |                                no | acceptance-critical                              |
| independently checked proof search                   |                                      no |                                 no |                             no |                                no | producer only                                    |
| solver with checked certificate                      |                                      no |           translation may be corr. |                             no |                                no | certificate checker may be logical TCB           |
| directly trusted solver                              |                           yes/auxiliary |                        translation |                             no |                            report | must be disclosed                                |
| C++L recognizer/parser                               | only if proof checking depends directly |                                yes |           possibly via erasure |                                no | source interpretation                            |
| Clang semantic analysis                              |                                      no |                                yes |                            yes |                                no | ordinary C++ authority                           |
| VIR construction                                     |                                      no |                                yes |                             no |  imported VIR may be artifact TCB | source correspondence                            |
| obligation generation                                |                                      no |                                yes |                             no |                                no | omitted obligations are unsound                  |
| path/CFG lowering                                    |                                      no |                                yes |                             no |                                no | path facts                                       |
| call-summary binding                                 |                                      no |                                yes |                             no |                metadata transport | exact entity required                            |
| Place/PlaceVersion mapping                           |                                      no |                                yes |                             no |                                no | storage identity/versioning                      |
| alias/effect analysis                                |                                      no |                                yes |                             no |                                no | must be conservative                             |
| memory capability checker                            |       if implemented as core logic, yes |                      otherwise yes |                             no |                                no | invalid capability can make source claim unsound |
| refinement-crossing generator                        |                                      no |                                yes |                             no |                                no | complete `Valid` obligations                     |
| representation/decomposition provider                |                                      no |                                yes |                             no |                                no | wrong partition can be unsound                   |
| loop VC generator                                    |                                      no |                                yes |                             no |                                no | if loop rule not kernel-native                   |
| termination checker outside kernel                   |                                 depends |                                yes |                             no |                                no | checker must be trusted/independently checked    |
| standard-library semantic model                      |                                      no |                                yes | yes for runtime correspondence |                  version metadata | public semantics mapping                         |
| FFI contract                                         |                                      no |                   assumption/corr. |                            yes |                            report | explicit trust unless verified                   |
| `trusted law` proposition                            |                         not a component |                explicit assumption |         maybe external runtime |                            report | remains in trust closure                         |
| `unsafe` marker                                      |                                      no |                    effect boundary |          runtime code executes |                            report | supplies no facts                                |
| erasure/lowering                                     |                                      no |              source/runtime bridge |                            yes |                                no | correctness-critical                             |
| native C++ compiler optimizer/codegen                |                                      no | may affect source semantic answers |                            yes |                                no | executable-level guarantee                       |
| linker/LTO                                           |                                      no |                                 no |                            yes |                                no | symbol/ABI behavior                              |
| proof cache storing evidence then rechecking         |                                      no |                                 no |                             no | usually no trust beyond integrity | corruption causes rejection/miss                 |
| verdict-only proof cache                             |                                      no |                                 no |                             no |                               yes | reuse logic is trusted                           |
| cross-TU verification metadata                       |                                      no |                                yes |                             no |                               yes | binding/invalidation critical                    |
| diagnostics text                                     |                                      no |                                 no |                             no |                        usually no | unless used as machine authority                 |
| assurance status/report generator                    |                                      no |                                 no |                             no |                               yes | must not overstate assurance                     |
| AI assistant                                         |                                      no |                                 no |                             no |                                no | candidate producer only                          |

---

# Annex B (normative) — Correspondence obligations by feature

This annex is normative. It summarizes the minimum trust-sensitive correspondence that an implementation must preserve for each language family. It does not replace the detailed semantics in `SPEC.md`.

## B.1 Laws and proofs

The implementation must correctly preserve:

```text
Law declaration identity
parameter quantification
expects premise
proves conclusion
proof-body goal
trusted vs proved declaration status
proof reference instantiation
binder scope
trust closure
```

It must never turn declaration presence into proof evidence.

## B.2 Function contracts

The implementation must correctly preserve:

```text
callable identity
entry values
normal-return state
result binding
old(...) entry snapshots
reference/pointer post-state
callee precondition obligations
postcondition availability only after successful verified normal return
effect summary application
```

## B.3 Refinements

The implementation must correctly preserve:

```text
refinement declaration identity
base type
index parameters
predicate
recursive Valid(T,v)
logical version on which validity is known
all construction/write/call/return crossings
erasure identity
```

## B.4 Storage and memory

The implementation must correctly preserve:

```text
place identity
projection path
region/lifetime
logical version
may-alias relation
read/write capability
initialization
provenance
extent/bounds
effect invalidation
```

A false negative may reject valid code. A false positive that grants access or preserves stale facts can be unsound.

## B.5 Control flow

The implementation must correctly preserve:

```text
evaluation order
condition polarity
short-circuit behavior
branch reachability
join facts
returns
break/continue
loop iteration edges
exceptional exits
```

## B.6 Arithmetic and conversions

The implementation must correctly preserve:

```text
resolved operator
operand/result types
promotions/conversions
width/signedness
overflow/definedness conditions
comparison semantics
cast category
```

## B.7 Cases/decomposition

The implementation must correctly preserve:

```text
representation identity
complete state partition
residual states
payload/component identity
arm binder scope
impossible-state proof
```

## B.8 Induction and termination

The implementation must correctly preserve:

```text
induction domain
base/successor/structural cases
predecessor relation
induction-hypothesis proposition
well-founded measure
recursive/continuing edges
decrease comparison
```

## B.9 Classes and virtual dispatch

The implementation must correctly preserve:

```text
this/object identity
base/member construction order
lifetime
refined subobject validity
override relation
base/derived contract substitutability
dynamic target set
virtual effect guarantees
```

## B.10 Templates

The implementation must correctly preserve:

```text
template declaration identity
concrete substitution
instantiation-specific contracts/refinements
NTTP values
specialization selection
verification metadata visibility
```

## B.11 Exceptions

The implementation must correctly preserve:

```text
normal vs exceptional exits
unwinding/destruction effects
exception specifications
postconditions that do/do not apply
```

## B.12 Concurrency

The implementation must correctly preserve:

```text
thread interference
atomic operations
memory order
synchronization/happens-before
ownership transfer
facts invalidated by interference
```

## B.13 Erasure

The implementation must correctly preserve:

```text
which source is proof-only
which source remains runtime-bearing
canonical refinement lowering
runtime validation
runtime side effects
ABI/calling convention
object lifetime/destruction
```

---

# Annex C (normative) — Trust report semantic schema

This annex is normative for the semantic information exposed by machine-readable trust reporting. It does not mandate a concrete file format.

## C.1 Build record

A build-level record must be able to represent:

```text
build identity
C++L compiler identity
formal calculus identity
proof checker identity
selected C++ mode
target/ABI identity
runtime compiler/linker identity when executable assurance is claimed
verification configuration
set of reportable claims
set of trusted assumptions
set of unsafe/unverified/runtime-checked boundaries relevant to selected claims
external/library/environment assumption classes
imported artifact identities
```

## C.2 Claim record

Each reportable claim must be able to represent:

```text
claim semantic identity
source entity/location provenance
status
proposition/evidence identity
trusted-assumption closure
proof dependencies
runtime-check dependencies
unsafe dependencies
unverified dependencies
external model dependencies
cross-TU/imported metadata dependencies
checker/calculus version
```

## C.3 Trusted-assumption record

Each trusted assumption must be enumerable with:

```text
semantic identity
declaration/import provenance
proposition identity
premise/parameters
source package/module if imported
dependents or reverse-dependency discoverability
```

## C.4 Directly trusted automation

If any solver, plugin, external checker or generated summary is trusted without independently checkable evidence, the report must identify:

```text
component identity/version
scope of claims affected
reason direct trust is required
configuration affecting semantics
```

## C.5 Status integrity

A report consumer must be able to determine whether a `PROVEN` claim has a non-empty trusted closure without parsing human prose.

---

# Annex D (normative) — TCB change review checklist

Every change affecting verification semantics or assurance reporting MUST be evaluated against this checklist.

## D.1 Logical core

- Does the change add a proof rule?
- Does it change definitional equality or normalization?
- Does it change term typing, substitution or binders?
- Does it trust a new decision procedure directly?
- Does it change certificate checking?
- Does it introduce an axiom or primitive assumption?

If yes, the logical TCB and calculus/version compatibility must be reviewed.

## D.2 Correspondence

- Does the change recognize new source syntax?
- Does it map a new C++ construct into formal terms?
- Does it create new obligations?
- Can it omit an obligation?
- Does it alter CFG/path conditions?
- Does it alter place/alias/effect semantics?
- Does it alter representation decomposition?
- Does it alter refinement validity/crossings?
- Does it alter template/virtual/exception/concurrency reasoning?

If yes, correspondence-TCB obligations and adversarial tests must be updated.

## D.3 Runtime

- Does it change erasure/lowering?
- Does it change emitted C++?
- Does it change ABI or calling convention?
- Does it remove or alter runtime validation?
- Does it change linked libraries/toolchain assumptions?

If yes, runtime-TCB analysis and preservation tests must be updated.

## D.4 Reuse/artifacts

- Does it change proof or metadata serialization?
- Does it change cache keys or dependency invalidation?
- Does it import new metadata across TUs/modules?
- Does it trust a stored verdict instead of rechecking evidence?

If yes, artifact/reuse TCB must be reviewed.

## D.5 Reporting

- Does it introduce a new assurance status?
- Can it hide a trusted/unsafe/unverified dependency?
- Does it change trust-closure propagation?
- Does it change machine-readable report fields?

If yes, reporting TCB and compatibility must be reviewed.

## D.6 Trust expansion decision

A review MUST explicitly record one of:

```text
TCB unchanged
TCB reduced by independent checking
TCB expanded: <new trusted responsibility>
trusted-assumption surface expanded: requires SPEC change
runtime/environment assumption changed
```

Silence is not an acceptable trust-impact assessment for a trust-sensitive change.

---

# Annex E (informative) — Mental model

This annex is informative.

A useful mental model is:

```text
C++L source
    │
    ├── ordinary C++ meaning -----------------------------┐
    │                                                     │
    └── C++L specification meaning                        │
            ↓                                             │
      correspondence TCB                                  │
            ↓                                             │
      formal obligations + storage/capability obligations │
            ↓                                             │
      proof producers (untrusted when independently checked)
            ↓
      logical/capability checkers                         │
            ↓                                             │
      PROVEN under explicit trust closure                 │
            │                                             │
            └── erasure/lowering → C++ compiler → binary ─┘
                          runtime TCB
```

The proof checker answers:

```text
Does this evidence establish this formal goal?
```

The correspondence TCB answers:

```text
Is this the right formal goal for this source program?
```

The runtime TCB answers:

```text
Does the executable preserve the runtime behavior that was verified?
```

The trust graph answers:

```text
Which explicit assumptions remain underneath the claim?
```

A mature assurance story requires all four answers.
