# Conjunction evidence

> Surface syntax and formatting in this historical RFC are superseded by
> [RFC 0015](0015-canonical-language-surface.md) and the
> [normative grammar](../GRAMMAR.md). Semantic rationale remains applicable.

Status: implemented by this checkpoint. Normative rules: SPEC.md 7.6;
precedence: GRAMMAR.md 33.

## Capability and rules

The retained `&&` connective states both operands. A proposition has an `And`
node; introduction supplies evidence for each side and elimination projects
either side of an evidenced conjunction. Neither operation can be encoded using
the existing first-order equality and implication rules without adding another
logical representation or assumption, so these two rules belong in the kernel.

The kernel validates both propositions, checks both introduction premises, and
checks elimination evidence against the whole restated conjunction before
matching the selected side to the goal. It performs substitution itself and
does not change binder depth across conjunction. No normalization equation or
equality relaxation is added.

## Compiler integration

Clang resolves built-in `&&` and its operands. The bridge and VIR preserve the
operator, and proposition lowering constructs `And`. No fake logical C++ type,
function or template is introduced. Conjunctions compose with quantifiers,
implications, written proofs, contracts, rewriting and arithmetic automation.
Every projection offered as an arithmetic fact retains its explicit proof.
Written proof failure still refuses the unit without trying automation.

The surface currently requires Boolean C++ predicates as conjuncts. Explicit
formal operands, disjunction, equivalence, term-level `&&`, verified guards and
loop invariants remain refused. Ordinary unverified runtime C++ is unchanged.
No new erasure operation or runtime check is introduced.

## Trust, identity and tests

Two kernel rules are added, bringing the total to eleven. Kernel and formal-core
versions become 0.4.0. Logical assumptions, axioms and trusted mechanisms added:
zero. Both sides and their reachable definitions contribute to obligation
identity. There is still no proof-cache loader or serialized proof format.

Coverage includes introduction and both projections, nested use, rewriting,
reusable evidence, `assume`, quantified scope, arithmetic, contracts, and native
execution in C++17/20/23. Rejections cover a false conjunct in either position,
wrong evidence, wrong type, malformed nodes, free variables, capture, recursive
proof reference, and failed written proofs with no binary or `PROVEN` result.
Generated substitution and proof tests include conjunctions and compare accepted
propositions against an independent finite model. Hash tests change each side
and definitions reachable exclusively from either side.
