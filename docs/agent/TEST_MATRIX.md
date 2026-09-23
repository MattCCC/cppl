# Test matrix

Maps normative rules to the tests that verify them. A rule with no test is an
unverified claim about the implementation.

## Citing rules from tests

Tests cite rule IDs so coverage is attributable and survives specification
edits:

```cpp
// SPEC: REFINE-010
TEST(RefinementWrite, RejectsUnprovenReplacement) { ... }
```

```bash
# SPEC: REFINE-010, STORAGE-004
reject 'write-without-proof' <<'EOF'
...
EOF
```

Prefer a rule ID over a section reference. `SPEC.md 17` stops being accurate the
moment a section is inserted; `REFINE-010` does not.

Report current coverage:

```sh
cppl-spec-rules report          # rules cited by implementation and tests
cppl-spec-rules report --all    # including rules with no citation
```

## Required categories

Every feature needs all five. A feature with only the first is not implemented,
it is demonstrated.

| Category | Question it answers | Location |
| --- | --- | --- |
| Positive | Does a valid program verify? | `tests/fixtures/`, `tests/integration/` |
| Negative | Is an invalid program rejected, for the stated reason? | `tests/negative/` |
| Interaction | Does it hold in combination with other features? | `tests/integration/` |
| Adversarial | Can the feature be used to prove something false? | `tests/negative/`, `tests/kernel/` |
| Erasure | Is runtime behavior unchanged? | `tests/e2e/`, `tests/conformance/` |

A negative test must assert *why* the program was rejected. A test that only
asserts failure passes for the wrong reason as soon as an unrelated error
appears — the existing `tests/negative/refinement_types.sh` does this correctly
by requiring that no `PROVEN` status is reported and that an error is emitted.

## Per-feature matrices

### refinement-types

Manifest: `features/refinement-types.yaml`

| Required case | Category | Status |
| --- | --- | --- |
| Local initialization | positive, negative | covered — a local owes its predicate at initialization and inside a loop, and one declared without an initializer holds no value and so no evidence (`negative/refinement_types.sh` citing `REFINE-008`, `REFINE-010`, `REFINEOBL-002`, `e2e/refinement_flow.sh`, `negative/verified_locals.sh`) |
| Parameter crossing | positive, negative | covered — a refined parameter is an entry fact, and a plain argument crosses into one at the call only where the caller's precondition proves the predicate (`e2e/refinement_flow.sh`) |
| Return crossing | positive, negative | covered — a refined result must hold on the returning path, and an ordinary function's return cannot establish one (`e2e/refinement_flow.sh`, `negative/refinement_types.sh`) |
| Assignment and compound update | positive, negative | covered — a compound update owes its refinement at the updated value, proven for a symbolic update that preserves it and refused for one that leaves it (`e2e/refinement_flow.sh`, `negative/refinement_types.sh`) |
| Member initialization and write | positive, negative | covered — construction, direct write, write through a reference, and a sibling left alone (`refinement_types.sh`, `e2e/refinement_flow.sh`, `fixtures/refinement_types.cpp`) |
| Subobject entry validity | positive | covered — a verified parameter supplies its refined subobjects' validity (`REFINEOBL-005`) |
| Array/element write | positive, negative | covered — constant and symbolic indices; a symbolic index owes `index < extent` and is never assumed distinct from a sibling (`e2e/refinement_flow.sh`, `negative/verified_storage.sh`) |
| Capability extent bounds | positive, negative, adversarial | covered — a subscript through `readable(a, n)` owes `index < n` against the stated extent term, proven for a constant and a runtime `n`, refused when the index is unbounded or bounded by a wider extent, and refused entirely for the one-object form (`e2e/refinement_flow.sh`, `negative/verified_storage.sh`) |
| Per-specialization templates | positive, negative, adversarial | covered — each specialization is checked with its own instantiated contract; a contract true at one argument and false at another fails only where false, and an uninstantiated template proves nothing (`e2e/verified_templates.sh`, `negative/verified_functions.sh` citing `TEMPLATE-001`, `TEMPLATE-003`) |
| Refinements through templates | positive, negative | covered — indexed refinements applied at a template parameter, refined parameters, and calls between verified specializations (`e2e/verified_templates.sh`) |
| Indexed observation | positive, negative, adversarial | covered — a symbolic subscript into an array reference is bounded by its own type's extent with no prior element observed, a dependent extent bounds each specialization separately, a wider bound does not carry to a narrower extent, and the kernel admits neither injectivity nor extensionality nor any bound from the observation itself (`e2e/refinement_flow.sh`, `e2e/verified_templates.sh`, `kernel/value_model_test.cpp` citing `STORAGE-005`, `TCB-CORE-016`) |
| Dereference read and write | positive, negative | covered — `readable`/`writable` gate every form; non-nullness establishes neither, and neither capability entails the other (`negative/verified_storage.sh`) |
| Alias mutation invalidates facts | interaction, adversarial | covered — reference, member, pointer and symbolic-element aliases all invalidate conservatively |
| Verified call post-state | interaction | covered — the caller gets the callee's `ensures` and no more, and a call through a pointer to non-const invalidates the pointee while a pointer to const leaves it alone (`negative/verified_storage.sh`, `e2e/refinement_flow.sh`) |
| Nested refinements | positive, negative | covered — a refinement over a refinement owes both predicates, and the base alone does not enter the inner one (`e2e/refinement_flow.sh`) |
| Indexed refinements | positive, negative | covered — the index argument is substituted into the predicate; a wider bound follows from a narrower one only where the kernel proves it, and narrowing is refused (`e2e/refinement_flow.sh`, `negative/refinement_types.sh`) |
| Refinement implication and conversion | positive, negative | covered — a stronger refinement enters a weaker one, the converse is refused, and the relation is decided by the predicates rather than the names (`negative/refinement_types.sh`) |
| Overload erasure collision | erasure, negative | covered — two refinements of one base erase to one signature and are reported where written (`negative/refinement_types.sh`) |
| ABI equivalence | erasure | covered — the C++L source and the erased source compile to byte-identical objects across `c++17`, `c++20` and `c++23`, and no refinement name reaches the symbol table (`e2e/refinement_types.sh`) |
| No hidden runtime validation | erasure, adversarial | covered — identical object code is compared rather than identical output, so a check that happens to pass on the test input would still be caught (`e2e/refinement_types.sh`) |

### checked-contradiction

Manifest: `features/checked-contradiction.yaml`

| Required case | Category | Status |
| --- | --- | --- |
| Matched pair differing only in the omitted case | positive, negative, adversarial | covered — the accepted half omits the first case the partition splits on, so its own discriminator is the only case fact in its branch; withholding that discriminator fails it, and judging an omission by its goal accepts the refused half, whose goal is provable there (`fixtures/omitted_case.cpp`, `negative/contradictions.sh` citing `CASE-013`) |
| Omission under a provable goal | negative, adversarial | covered — `refl` would close the omitted arm, and the omission is still refused because nothing contradicts the case (`negative/contradictions.sh` citing `CASE-005`, `CASE-013`) |
| Omission under a satisfiable premise | negative | covered (`negative/contradictions.sh` citing `CASE-015`) |
| Absent arm with the evidence in scope | negative | covered — non-exhaustive, never an intentional omission (`negative/contradictions.sh` citing `CASE-005`) |
| Arm and omission for one case, unknown label | negative | covered (`negative/contradictions.sh` citing `CASE-004`) |
| Residual case omitted by its discriminator | positive | covered — enumeration and pointer residuals, refuted through disequalities (`fixtures/omitted_case.cpp`) |
| Omission under a quantifier | positive | covered — the obligation is closed over the parameter and the goal's own binder (`fixtures/omitted_case.cpp`) |
| Statement under flat and structured goals | positive, negative | covered — a quantifier, a conjunction and an implication are closed from one contradiction; a satisfiable premise and non-equality evidence are refused (`fixtures/contradiction.cpp`, `negative/contradictions.sh` citing `CASE-011`) |
| Corrupted evidence | adversarial | covered — facts, certificate and a fact's stated proposition each corrupted, and the evidence checked against another claim; every one refused by the kernel (`unit/contradiction_test.cpp` citing `CASE-014`, `CASE-015`) |
| One contradiction under two origins | adversarial | covered — two obligations, two identities, two reporting names, two diagnostics, and no strategy replaces refused evidence (`unit/contradiction_test.cpp` citing `CASE-012`, `CASE-016`) |
| Contextual words | positive, conformance | covered — the words as types, variables, functions and labels, in C++ and inside proofs, across `c++17`, `c++20`, `c++23` (`conformance/contextual_identifiers.sh` citing `WORD-002`, `WORD-010`) |
| Erasure | erasure | covered (`e2e/omitted_case.sh`, `e2e/contradiction.sh`) |
| Structured-value goal | positive, negative | covered — a matched pair: two records are proven equal under a false premise, by a written `contradiction` and by automation, and refused under a satisfiable one (`fixtures/contradiction.cpp`, `negative/contradictions.sh` citing `CASE-014`, `CASE-015`) |
| Falsity elimination | adversarial | covered — every proposition form is closed from a refuted fact; falsity elimination over reflexivity, an absurd equality, a satisfiable fact, no facts, a missing constraint or a restated fact is refused, `False` has no introduction, and a certificate that leaned on a negated goal is refused for `False` (`kernel/adversarial_kernel_test.cpp` citing `TCB-CORE-017`) |
| Unreachable runtime path from source | positive, negative, adversarial | covered — claims across branches, a loop, a verified call, a local's versions and an unbraced `if`, each counted as an impossible path and erased to an empty statement; a matched pair differing only in the branch condition; a reachable claim under a provable goal; unknown, refused, mistyped, over-instantiated and non-equality evidence; a claim resting on an unproven callee; a claim outside a verified function; the spelling kept as a C++ declaration where `contradiction` names a type; a claim never established by a strategy (`e2e/impossible_path.sh`, `negative/impossible_paths.sh`, `unit/recognizer_test.cpp`, `unit/projection_test.cpp`, `unit/contradiction_test.cpp` citing `VERIFIED-045`, `WORD-011`, `ERASE-016`) |

### trust-propagation

Manifest: `features/trust-propagation.yaml`

| Required case | Category | Status |
| --- | --- | --- |
| Direct trust | positive | covered — a law, a proof and a proof of a law instance each name a trusted law and are reported as resting on it directly (`fixtures/trust_closure.cpp`, `e2e/trust_closure.sh` citing `TRUSTED-006`) |
| Transitive trust through a chain | positive, adversarial | covered — only the first of three proofs names the law, and all three rest on it; dropping inherited premises is an internal error, and not discharging them is a kernel rejection (`e2e/trust_closure.sh`) |
| Several trusted laws in one claim | positive, adversarial | covered — two premises in declaration order; reversing their hypotheses is a kernel rejection (`fixtures/trust_closure.cpp`) |
| Unused trusted law | positive | covered — listed as unused, and so is one declared beside an independent proof of the same proposition (`e2e/trust_closure.sh`, `e2e/trusted_assumptions.sh`) |
| Chain joining assumption-free and trust-dependent proofs | positive | covered (`fixtures/trust_closure.cpp`) |
| False trusted law | positive | covered — `x == 5u` is proven only relative to it, and reported so (`fixtures/trust_closure.cpp`) |
| Premise still owed | negative | covered (`negative/trusted_dependencies.sh` citing `TRUSTED-007`) |
| Premise only where named | positive, negative | covered — matched pair differing only in the evidence one contradiction names (`fixtures/trust_closure.cpp`, `negative/trusted_dependencies.sh` citing `TRUSTED-008`) |
| Ambiguous names | negative | covered — a proof and a trusted law, and two overloaded trusted laws (`negative/trusted_dependencies.sh` citing `TRUSTED-009`) |
| Unstated assumption | negative | covered (`negative/trusted_dependencies.sh` citing `TRUSTED-005`) |
| Circular proofs | negative | covered (`negative/trusted_dependencies.sh` citing `PROOFSRC-007`) |
| Omitted cases inherit their proof's closure | positive, adversarial | covered — forgetting the premises is a kernel rejection (`fixtures/trust_closure.cpp`) |
| Contract closure through calls, including cycles | positive, unit | covered — a runtime path claim naming a trust-dependent proof puts the law into its function's contract and a caller's (`fixtures/trust_closure.cpp`); direct, through two calls, and a recursive graph, and not following calls is caught (`unit/trust_closure_test.cpp`) |
| Verdict gate | unit, adversarial | covered — fewer, other or extra premises than the acceptance was checked under are all refused (`unit/verdict_test.cpp` citing `TRUSTED-002`, `STATUS-002`) |
| Attribution faults | unit, adversarial | covered — a premise that is not a trusted law, a dependency no claim accounts for, an unknown callee, a shared or missing obligation, mismatched results; dropping a claim kind is an internal error (`unit/trust_closure_test.cpp`) |
| Zero-trust report | regression | covered — every existing line unchanged, every split zero (`e2e/trust_closure.sh`, `e2e/omitted_case.sh`, `e2e/trusted_assumptions.sh`) |
| Determinism | regression | covered (`e2e/trust_closure.sh` citing `TCB-PROV-005`) |
| One assumption in two units | positive | covered — one identity, unused only where unused (`e2e/trust_closure.sh` citing `TCB-TRUST-005`) |
| Erasure | erasure | covered (`e2e/trust_closure.sh`) |
| Trusted memory proposition | positive, negative | not built (`TRUSTED-003`) |

Status here describes test coverage, not implementation maturity.
`docs/STATUS.md` is authoritative for the latter, and neither weakens what
`docs/SPEC.md` requires.

## Adversarial baseline

These apply to every feature and correspond to `AGENTS.md` §38:

```text
False is not inhabitable
1 == 2 cannot be proven
proof objects cannot be forged
nontermination cannot prove arbitrary propositions
unsafe memory cannot manufacture proof evidence
trusted assumptions remain visible
ghost state cannot affect runtime behavior
erasure preserves runtime behavior
stale caches cannot preserve invalid proofs
unsupported C++ cannot silently become verified
solver failure cannot become proof success
AI output cannot bypass the kernel
```

A new feature must not weaken any of them. If a feature makes one of these
harder to state, that is a design problem to escalate, not a test to relax.
