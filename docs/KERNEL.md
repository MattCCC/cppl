# C++L Kernel Reference

**The core calculus the trusted proof kernel checks, as implemented**

Formal core: `cppl-core-0.9.0` — Kernel: `cppl-kernel-0.9.0`
(`kernel/include/cppl/kernel/version.hpp`)

This document states exactly the calculus the kernel in `kernel/` decides: its
types, terms, propositions and evidence, the typing and well-formedness
judgments, substitution, normalization, the fifteen rules, the translation of
arithmetic facts into integer constraints, certificate checking, and the limits
under which all of it fails closed. It then describes how the kernel is tested
and how the calculus is to be mechanized.

It is the reference for one version of the core. `FOUNDATIONS.md` is
authoritative for the mathematical interpretation of the proof concepts and
`TRUST.md` for what must be trusted; `SPEC.md` decides what the source language
means. What this document adds is precision about the fragment the kernel
implements. The kernel MUST implement exactly the rules stated here for the
version named above. A difference between the two is a defect that is resolved
before either is believed, and neither is changed to agree with the other
without the review a kernel change requires (`AGENTS.md` 7, 37,
`.agents/skills/cppl-kernel-change`). A change to anything this document states
changes the formal core and raises its version.

The names used below are the kernel's own C++ names where that helps an auditor
find the code; the notation is otherwise the metasyntax of `FOUNDATIONS.md` 4.

---

## Contents

1. The boundary
2. Types
3. Machine integers
4. Terms and primitives
5. Typing
6. Definitions and contexts
7. Propositions and well-formedness
8. Substitution and shifting
9. Normalization
10. Evidence and the checking judgment
11. The rules
12. Linear arithmetic
13. Certificate checking
14. Limits and resource failure
15. What the kernel does not contain
16. Testing
17. Mechanization plan
18. Changing the core

---

## 1. The boundary

The kernel has one entry point that can establish a proposition:

```cpp
std::expected<Acceptance, Rejection>
check(const Context&, const Proposition&, const ProofTerm&, const CoreLimits&);
```

`Acceptance` can be constructed only by `check` (it is the one friend of its
private constructor), and it carries the proposition that was checked. The
kernel links nothing but the C++ standard library; the architecture tests
refuse a dependency on the frontend, the Clang bridge, VIR, solvers, diagnostics
rendering or editor tooling (`kernel/CMakeLists.txt`,
`tests/architecture/dependency_boundaries.sh`, `TRUST.md` 5.4).

Above the kernel, an obligation becomes `PROVEN` only through
`obligations::Verdict::proven`, which compares the proposition the acceptance
carries with the obligation's goal closed over exactly the trusted premises the
verdict will name:

```text
acceptance.proposition() == relative_to(premises, goal)
relative_to([Q1, ..., Qn], G) = Q1 -> ... -> Qn -> G
```

Anything else is `UNRESOLVED` (`compiler/obligations/src/status.cpp`). A trusted
law therefore reaches the kernel only as the premise of an implication the
kernel checks; the kernel itself has no axiom, no assumption environment and no
way to admit a proposition without evidence (`TRUST.md` TCB-CORE-010,
TCB-CORE-012).

Every producer of evidence — automation, the refutation search, elaborated
written proofs, AI — is outside the kernel and untrusted (`TRUST.md` 6).

---

## 2. Types

```text
T ::= int(w, s)            machine integer: w in 1..64, s in {signed, unsigned}
    | value(id, [T1..Tn])  abstract nominal value with n total observations
    | indexed(T, k)        homogeneous finite domain of k >= 1 components of type T
```

`int(1, unsigned)` is the Boolean type `u1` (`kBoolean`): comparisons,
representability tests and negation yield it, and a proposition about a
condition is an equality at it (section 7).

A `value` type is identified by its identity string and its signature together;
two types are one type exactly when they are structurally equal. An `indexed`
type's extent is part of its identity, so `indexed(T, 4)` and `indexed(T, 8)`
are different types. A symbolic C++ extent is not a type: it is carried by the
bounds obligation of the access that needs it (`FOUNDATIONS.md` 45).

A type is **supported** (`is_supported`) when:

- every integer width is in 1..64;
- every `value` identity is non-empty and at most 4096 bytes, with at most 256
  observations;
- every `indexed` type has exactly one element type and an extent of at least 1;
- the type has at most 4096 nodes and nests at most 64 deep.

An unsupported type is malformed, and a proposition or term mentioning one is
rejected, never approximated.

Types are ordered and printed by `describe(Type)`. The printed form of a `value`
type prefixes its identity with the identity's length, so the printed forms of
distinct types are distinct; the term order of section 9 relies on that.

---

## 3. Machine integers

For `T = int(w, s)`:

```text
min(T) = -2^(w-1)       max(T) = 2^(w-1) - 1       if s = signed
min(T) = 0              max(T) = 2^w - 1           if s = unsigned
range(T) = { v in Z | min(T) <= v <= max(T) }
wrap(T, v) = the unique u in range(T) with u = v (mod 2^w)
```

Values are held as 128-bit integers (`Wide`), which hold every value, product
and certificate combination the kernel forms for widths up to 64. Every
operation on them that could leave 128 bits is overflow-checked, and an
overflow rejects rather than wraps (`TRUST.md` TCB-CORE-009). The core never
exchanges a machine integer for a mathematical one (`FOUNDATIONS.md` 2.3).

---

## 4. Terms and primitives

```text
t ::= #i                     variable (de Bruijn index; #0 is the innermost binder)
    | lit(T, v)              literal; T an integer type
    | d(t1, ..., tn)         application of an admitted definition d
    | prim(op, T, t1..tn)    primitive operation op stated at integer type T
    | proj(D, i, t)          the i-th observation of t, a value of type D = value(...)
    | elem(D, t, u)          the component of t : D = indexed(...) at index u
```

A primitive denotes exactly one total operation on values of `T`. It is not a
C++ operator: lowering a C++ operator onto primitives, together with the side
conditions C++ requires for defined behavior, is the elaborator's
responsibility (`SPEC.md` 29, RFC 0019). For `a`, `b` the values of the
operands:

| `op`             | arity | operand types | result | value                                                  |
| ---------------- | ----- | ------------- | ------ | ------------------------------------------------------ |
| `add_wrap`       | 2     | `T, T`        | `T`    | `wrap(T, a + b)`                                       |
| `sub_wrap`       | 2     | `T, T`        | `T`    | `wrap(T, a - b)`                                       |
| `mul_wrap`       | 2     | `T, T`        | `T`    | `wrap(T, a * b)`                                       |
| `eq` `ne`        | 2     | `T, T`        | `u1`   | `a = b`, `a != b`                                      |
| `lt` `le` `gt` `ge` | 2  | `T, T`        | `u1`   | the order of the values in `range(T)`                  |
| `not`            | 1     | `u1` (T = u1) | `u1`   | `1 - a`                                                |
| `select`         | 3     | `u1, T, T`    | `T`    | the second operand if the first is 1, else the third   |
| `add_fits`       | 2     | `T, T`        | `u1`   | 1 iff `a + b` (unbounded) is in `range(T)`             |
| `sub_fits`       | 2     | `T, T`        | `u1`   | 1 iff `a - b` (unbounded) is in `range(T)`             |
| `mul_fits`       | 2     | `T, T`        | `u1`   | 1 iff `a * b` (unbounded) is in `range(T)`             |
| `quot`           | 2     | `T, T`        | `T`    | 0 if `b = 0`, else `wrap(T, trunc(a / b))`             |
| `rem`            | 2     | `T, T`        | `T`    | `a` if `b = 0`, else `a - trunc(a / b) * b`            |
| `convert`        | 1     | any integer   | `T`    | `wrap(T, a)`                                           |

`trunc` rounds toward zero. `quot` and `rem` are made total: the cases C++
leaves undefined (a zero divisor, the least signed value over `-1`) are the
lowering's obligations, never these definitions (`SPEC.md` ARITH-007,
DEFINEDBEHAVIOR-002, DEFINEDBEHAVIOR-003). `convert` is C++'s conversion to an
unsigned type exactly, and C++20's to a signed one; the lowering owes
representability wherever a signed target may not hold the value (ARITH-008),
and never states a conversion to `bool` with it.

`proj` and `elem` are uninterpreted observations: they have no reduction rule,
and the kernel admits neither injectivity nor extensionality for them (`TRUST.md`
TCB-CORE-016). Forming `elem(D, t, u)` proves nothing about `u`; in particular it
does not prove `u < k`, which the C++ subscript owes separately (`SPEC.md`
STORAGE-005).

The term formers and proof formers are fixed by `static_assert`s on the variant
sizes (`term.hpp`, `proof.hpp`), so a new alternative fails to compile until
every site that decides what it means has been reviewed.

---

## 5. Typing

`Γ` lists the types of the enclosing binders outermost first; `#i` denotes
`Γ[|Γ| - 1 - i]`. `Σ` is the context of admitted definitions (section 6). The
judgment `Σ; Γ ⊢ t : T` (`type_of`) is:

```text
i < |Γ|    T = Γ[|Γ|-1-i] supported
---------------------------------------- var
Σ; Γ ⊢ #i : T

T supported    v in range(T)
---------------------------------------- lit
Σ; Γ ⊢ lit(T, v) : T

d = (T1..Tn) -> R in Σ    Σ; Γ ⊢ ti : Ti  (each i)
---------------------------------------- call
Σ; Γ ⊢ d(t1..tn) : R

D = value(id, [U0..Um-1]) supported    i < m    Σ; Γ ⊢ t : D
---------------------------------------- proj
Σ; Γ ⊢ proj(D, i, t) : Ui

D = indexed(U, k) supported    Σ; Γ ⊢ t : D    Σ; Γ ⊢ u : int(w, s)
---------------------------------------- elem
Σ; Γ ⊢ elem(D, t, u) : U
```

and for `prim(op, T, ...)` with `T` a supported integer type and exactly
`arity(op)` operands, each operand typed as the table of section 4 requires
(the first operand of `select` at `u1`, the operand of `convert` at any integer
type, every other operand at `T`, and `T = u1` for `not`); the result type is
`u1` for the comparisons and representability tests and `T` otherwise. An index
of any integer type types an `elem`. Typing descends at most `max_term_depth`
levels.

Typing is decided by structure alone; no rule converts between types, and two
types are compared by structural equality.

---

## 6. Definitions and contexts

A definition `d` is `(id, name, [T1..Tn], R, body)`: a total first-order
function whose body is a term over its parameters, parameter 0 being the
outermost binder. `name` is diagnostic text; identity is `id`.

`Context::define` admits a definition only when its `id` is new, every
parameter type and `R` are supported, and `Σ; [T1..Tn] ⊢ body : R` holds **in
the context as it stands before the definition is added**. A definition can
therefore call only definitions admitted before it: the call graph is acyclic by
construction, which is why unfolding terminates and why the kernel needs no
termination checker (`SPEC.md` 22.2, 22.4). A recursive or looping C++ function
is never admitted as a definition (`STATUS.md`, Termination status).

---

## 7. Propositions and well-formedness

```text
P, Q ::= Eq(T, t, u)       propositional equality at T
       | Forall(T, P)      universal quantification; P under one more binder
       | P -> Q            implication; binds no term variable
       | P && Q            conjunction
       | P || Q            disjunction
       | False             falsity
```

`Σ; Γ ⊢ P prop` (`validate_proposition`) holds when every `Forall` binder is a
supported type and, for every `Eq(T, t, u)` in `P`, `Σ; Γ' ⊢ t : T` and
`Σ; Γ' ⊢ u : T` under the binders `Γ'` enclosing it; it descends at most
`max_term_depth` levels. `False` states nothing about any term.

A Boolean condition `c` (a term of type `u1`) becomes a proposition by
`pred(c, positive)`, which strips `not` (flipping `positive` each time) and then:

```text
pred(eq(T, a, b), true)   = Eq(T, a, b)
pred(ne(T, a, b), false)  = Eq(T, a, b)
pred(eq(T, a, b), false)  = pred(ne(T, a, b), true) = Eq(u1, eq(T, a, b), 0)
pred(c, positive)         = Eq(u1, c, positive ? 1 : 0)   otherwise
```

Each is equivalent, in the machine semantics, to `c` evaluating to `positive`.

---

## 8. Substitution and shifting

Terms use de Bruijn indices, so substitution is defined without names and
cannot capture (`TRUST.md` TCB-CORE-005).

`shift(t, n, c)` raises every variable at or above the cutoff `c` by `n`; it is
the restatement of `t` under `n` more binders inserted at depth `c`. On a
proposition it passes under a `Forall` with `c + 1`, and leaves `False` alone.

`t[a]_k` (`instantiate(t, a, k)`) replaces the binder `k` levels out with `a`
and closes the gap it leaves:

```text
#i[a]_k = shift(a, k, 0)    if i = k
        = #(i - 1)          if i > k
        = #i                if i < k
```

and structurally elsewhere, passing under a `Forall` with `k + 1`. The argument
is stated outside the binder it replaces, so it is shifted by the binders it
descends through: a variable it mentions keeps denoting what it denoted. `P[a]`
abbreviates `P[a]_0`.

Inside a definition body, unfolding (`substitute`) replaces parameter `#i` by
the `(n - 1 - i)`-th argument; a body has no binders of its own, so no shifting
is needed, and a variable that is not a parameter is rejected.

Every rule that closes a goal rests on these operations: reflexivity on
normalization, universal elimination, equality elimination, conditional
elimination and induction on substitution, hypothesis use on shifting.

---

## 9. Normalization

Definitional equality is decided by normalization: two terms are definitionally
equal exactly when their normal forms `nf(t)` are the same term (`TRUST.md`
TCB-CORE-004). Normalization is deterministic and every rewrite it performs is
an identity of the machine semantics of section 4.

```text
nf(#i)               = #i
nf(lit(T, v))        = lit(T, v)
nf(d(t1..tn))        = nf(body_d with its parameters replaced by nf(t1)..nf(tn))
nf(proj(D, i, t))    = proj(D, i, nf(t))
nf(elem(D, t, u))    = elem(D, nf(t), nf(u))
nf(prim(op, T, ts))  = norm_op(T, nf(ts))
```

Free variables, observations and anything the rules below do not fold are
opaque. `norm_op` is:

- **Ring operations** (`add_wrap`, `sub_wrap`, `mul_wrap`). The operands are
  read as a polynomial over the ring of integers modulo `2^w`: a literal of
  `T` is a constant, a ring operation of `T` combines its operands' polynomials,
  and any other normal term of type `T` is an opaque factor of degree one.
  Coefficients are residues modulo `2^w`; like monomials combine and a zero
  coefficient disappears. The polynomial is rendered canonically: monomials in
  order of degree and then of their factors, those with a coefficient of at
  most `2^(w-1)` added and the others subtracted with their magnitude, the
  constant last. Two's-complement addition, subtraction and multiplication are
  the ring's operations whatever the signedness, so every commutative-ring
  identity holds of them exactly; order is not cancellative under wrapping, so
  nothing crosses a comparison.
- **`not`**: `not(lit b) = lit(1 - b)`, `not(not(c)) = c`.
- **`select`**: a literal condition selects its operand; equal operands collapse
  to one; `select(not(c), t, f) = select(c, f, t)`.
- **`eq`, `ne`**: `a = b` exactly when `a - b` is zero in the ring, so `eq` is
  stated of the difference polynomial `p`. If `p` is a constant the result is
  the literal it decides; otherwise it is `eq(r, 0)` where `r` is whichever of
  the renderings of `p` and `-p` comes first in term order. `ne(a, b)` is
  `not(eq(a, b))`.
- **Order**: `gt(a, b) = lt(b, a)`, `le(a, b) = not(lt(b, a))`,
  `ge(a, b) = not(lt(a, b))`. `lt(a, b)` folds only where the type decides it:
  two literals by their values, `a` identical to `b` to 0, `b = min(T)` to 0,
  `a = max(T)` to 0.
- **Representability**: two literal operands are decided by their exact result;
  otherwise the operands of `add_fits` and `mul_fits` are put in term order.
- **`quot`, `rem`**: folded only where the total definition decides the value:
  a literal divisor of 0 (`quot` is 0, `rem` the dividend), two literals, a
  literal dividend of 0, a divisor of 1 (`quot` the dividend, `rem` 0), and a
  divisor of `-1` (`quot` is `sub_wrap(0, a)`, which wraps the least value to
  itself, and `rem` is 0).
- **`convert`** of a literal is the wrapped literal.

The **term order** (`compare`) is structural and total: by former, then a
variable's index, a literal's type and value, a call's definition id and
arguments, a primitive's operation, type and arguments, and an observation's
printed domain, position and arguments. Because printed types are distinct for
distinct types (section 2), two terms compare equal only when they are
identical. Nothing in the order depends on an address, a hash or construction
history.

Normalization of the operands of an equality is what `refl` decides, and is
bounded by `max_normalization_steps` (one step per unfolding, primitive and
polynomial operation), `max_term_depth`, `max_polynomial_terms` and
`max_monomial_degree`. Exhausting a bound rejects the evidence.

---

## 10. Evidence and the checking judgment

```text
e ::= refl
    | forall_intro(T, e)                     | forall_elim(P, e, t)
    | hyp(k)
    | implies_intro(P, e)                    | implies_elim(P, e1, e2)
    | eq_elim(T, a, b, C, e1, e2)
    | cond_elim(T, c, t, f, M, e1, e2)
    | linear(F1:e1, ..., Fn:en; certificate)
    | and_intro(e1, e2)                      | and_elim(P, e, side)
    | or_intro(e, side)                      | or_elim(P, e, e1, e2)
    | false_elim(e)
    | unsigned_induction(T, e1, e2)
```

An elimination restates the proposition it eliminates from (`P` in
`forall_elim`, `implies_elim`, `and_elim` and `or_elim`), because nothing else in
the evidence records it. A restatement is checked, never believed: it must be
well formed, the evidence must establish it, and what the rule derives from it
must be the goal.

The judgment `Σ; Γ; H ⊢ e : P` (`check_under`) has `Γ` the binder types as in
section 5 and `H` the hypotheses: each a proposition together with the number of
binders `|Γ|` when it was introduced. `check(Σ, P, e)` first requires
`Σ; [] ⊢ P prop` and then `Σ; []; [] ⊢ e : P`: nothing is assumed to begin with,
and every premise a proof uses was introduced by the proof itself. Each step of
the judgment descends at most `max_term_depth` levels.

Propositions are compared by structural equality (`==`), never up to
definitional equality. Only two things normalize: `refl`, the two sides of the
equality it closes, and the arithmetic translation of rule 9, the terms of the
facts and goal it states as constraints.

---

## 11. The rules

The checker dispatches first on the elimination and structural forms, which
close a goal of any shape, and then on the goal for the introduction forms. Any
other combination is rejected.

```text
                 nf(a) and nf(b) identical
(1) refl         ---------------------------
                 Γ; H ⊢ refl : Eq(T, a, b)

                 Γ ⊢ Eq(T, a, b) prop    Γ; H ⊢ e1 : Eq(T, a, b)
                 Γ, T ⊢ C prop           Γ; H ⊢ e2 : C[b]         C[a] = P
(2) eq_elim      --------------------------------------------------------
                 Γ; H ⊢ eq_elim(T, a, b, C, e1, e2) : P

                 T = T'    Γ, T; H ⊢ e : Q
(3) forall_intro ---------------------------------------
                 Γ; H ⊢ forall_intro(T, e) : Forall(T', Q)

                 Γ ⊢ Forall(T, Q) prop    Γ; H ⊢ e : Forall(T, Q)
                 Γ ⊢ t : T                Q[t] = P
(4) forall_elim  ---------------------------------------------------
                 Γ; H ⊢ forall_elim(Forall(T, Q), e, t) : P

                 A = A'    Γ; H, (A, |Γ|) ⊢ e : B
(5) implies_intro -------------------------------------
                 Γ; H ⊢ implies_intro(A, e) : A' -> B

                 Γ ⊢ A -> B prop    Γ; H ⊢ e1 : A -> B    Γ; H ⊢ e2 : A    B = P
(6) implies_elim --------------------------------------------------------------
                 Γ; H ⊢ implies_elim(A -> B, e1, e2) : P

                 k < |H|    (A, n) = H[|H|-1-k]    shift(A, |Γ| - n, 0) = P
(7) hyp          -----------------------------------------------------------
                 Γ; H ⊢ hyp(k) : P

                 T integer    Γ ⊢ select(c, t, f) : T    Γ, T ⊢ M prop
                 M[select(c, t, f)] = P
                 Γ; H ⊢ e1 : pred(c, true) -> M[t]
                 Γ; H ⊢ e2 : pred(c, false) -> M[f]
(8) cond_elim    ----------------------------------------------
                 Γ; H ⊢ cond_elim(T, c, t, f, M, e1, e2) : P

                 P is Eq(...) or False    n <= max_arithmetic_facts
                 each Fi is Eq(...), Γ ⊢ Fi prop, Γ; H ⊢ ei : Fi
                 certificate refutes system(F1..Fn; not P)          (sections 12, 13)
(9) linear       -------------------------------------------------------------
                 Γ; H ⊢ linear(F1:e1, ..., Fn:en; certificate) : P

                 Γ; H ⊢ e1 : A    Γ; H ⊢ e2 : B
(10) and_intro   -------------------------------
                 Γ; H ⊢ and_intro(e1, e2) : A && B

                 Γ ⊢ A && B prop    Γ; H ⊢ e : A && B    (side ? B : A) = P
(11) and_elim    ------------------------------------------------------------
                 Γ; H ⊢ and_elim(A && B, e, side) : P

                 Γ; H ⊢ e : (side ? B : A)
(12) or_intro    ----------------------------------
                 Γ; H ⊢ or_intro(e, side) : A || B

                 Γ ⊢ A || B prop    Γ; H ⊢ e : A || B
                 Γ; H ⊢ e1 : A -> P    Γ; H ⊢ e2 : B -> P
(13) or_elim     ------------------------------------------
                 Γ; H ⊢ or_elim(A || B, e, e1, e2) : P

                 Γ; H ⊢ e : False
(14) false_elim  ------------------------
                 Γ; H ⊢ false_elim(e) : P

                 T = T' = int(w, unsigned)
                 Γ; H ⊢ e1 : Q[lit(T, 0)]
                 Γ; H ⊢ e2 : Forall(T, pred(lt(T, #0, lit(T, max(T))), true)
                                         -> Q -> shift(Q, 1, 1)[add_wrap(T, #0, lit(T, 1))])
(15) unsigned_induction ---------------------------------------------------------
                 Γ; H ⊢ unsigned_induction(T, e1, e2) : Forall(T', Q)
```

Notes on the rules:

- **`=` between propositions is structural equality.** In rules 2, 4, 6, 7, 8,
  11 the conclusion is derived by the kernel and then compared with the goal; a
  producer that restates the wrong thing can only fail.
- **Hypotheses (7).** A hypothesis exists only because an enclosing
  `implies_intro` put it there, and it is restated for the binders introduced
  since. No rule admits a premise on its own.
- **Implication elimination (6), `or_elim` (13), `cond_elim` (8)** check both
  pieces of evidence in the context standing where the rule is applied.
- **`False` (14) has no introduction.** Evidence for it can come only from a
  hypothesis supposing it, from an elimination yielding it, or from linear
  arithmetic refuting its facts with no goal taking part (`TRUST.md`
  TCB-CORE-017). A goal of `False` is closed by `false_elim` only from evidence
  that already establishes `False`.
- **Unsigned induction (15).** Both premises are stated by the kernel itself
  (`induction_base`, `induction_step` in `kernel/src/proof.cpp`), never taken
  from the evidence, so evidence cannot weaken the step, drop its range premise
  or widen the hypothesis. In the step, `Q` under the new binder is `P(n)`
  because the new binder stands where the goal's did, and
  `shift(Q, 1, 1)[n + 1]` is `P(n + 1)`: the body is lifted past the new binder
  with its own variable kept as the hole, which is then filled with `n + 1`. The
  range premise `n < max(T)` means `n + 1` wraps nothing. The rule is sound
  because `T` has exactly the values `0..max(T)`, each reached from 0 by
  finitely many successor steps below the maximum (`FOUNDATIONS.md` 74). A
  signed type, an abstract value and an indexed domain have no principle and
  are refused (`SPEC.md` INDUCT-004).
- **Dispatch order.** Rules 2, 4, 6, 7, 8, 9, 11, 13, 14 and 15 are tried by the
  form of the evidence; rules 3, 5, 10, 12 and 1 by the form of the goal
  (`Forall`, `->`, `&&`, `||`, `Eq`). A goal of `False` reached with any other
  evidence is rejected.

---

## 12. Linear arithmetic

Rule 9 translates its facts and the negation of its goal into a system of
integer linear constraints (`arithmetic_system` in `kernel/src/linear.cpp`)
whose integer solutions include every machine valuation that makes the facts
true and the goal false. The translation is the kernel's; the producer supplies
only the certificate.

**Variables.** A *value* variable stands for the machine value of a product of
opaque factors (a monomial of section 9) at an integer type `T`, and is
constrained to `range(T)`. A *wrap* variable stands for the integer number of
multiples of `2^w` separating a polynomial read over the integers from the
machine value it denotes. The same normal term at the same type always gets the
same variable.

**Values.** `value(t, T)` normalizes `t`, reads it as a polynomial of `T` and
states:

- a constant polynomial: its value;
- a single monomial with coefficient 1 and no constant: its value variable;
- otherwise: `Σ ci·vi + c0 - 2^w·k`, where each `ci` is the coefficient's
  representative closest to zero, `vi` the monomial's value variable, `k` a
  fresh wrap variable, and the whole is constrained to `range(T)`, which pins
  `k`. This is exact for two's-complement arithmetic: the machine value is the
  integer reading reduced into the type.

**Facts.** A fact `Eq(T, a, b)` states `value(a) = value(b)` as two
inequalities, except at `u1` when one side normalizes to a literal, where it
states that the other side's condition *evaluates* to that literal:

- a literal condition states nothing, or `1 <= 0` when it is the wrong literal;
- `not(c)` flips what is stated of `c`;
- `lt`, `le`, `gt`, `ge` state the order of the operands' values (`a + 1 <= b`
  and so on) or its negation; `eq` states equality and `ne` the disjunction
  `a + 1 <= b  ∨  b + 1 <= a`, and the reverse when negated;
- a representability test whose exact result is linear (a sum, a difference, or
  a product with a constant factor) states that result within `range(T)`, or
  when false the disjunction of below `min(T)` and above `max(T)`; a product of
  two unknowns is decided only where the ranges its operands always lie in (a
  value's type, or the narrower type a widening conversion took it from) keep
  every product within the type, in which case holding states nothing and
  failing states `1 <= 0`;
- any other condition is a `u1` value equal to 1 or to 0.

**The negated goal.** `False` states nothing: the facts alone must be refuted.
`Eq(T, a, b)` states the negation of what the corresponding fact would state:
`value(a) ≠ value(b)` as a disjunction, or at `u1` with a literal side, that the
condition evaluates to the other literal.

**RFC 0019 primitives as factors.** When a value variable's single factor is a
`convert` or a `quot`/`rem`, what defines it is stated once:

- `convert` to `T` of an operand of type `S`: if `range(S)` is within
  `range(T)` the value equals the operand's (and the variable's range is
  recorded as `range(S)` for the product rule above); otherwise the value is the
  operand's minus `2^w·k` for a fresh wrap variable `k`.
- `quot`/`rem` by a constant `c` with `|c| >= 2`: `dividend = c·q + r`,
  `r <= |c| - 1`, and for a signed type `r >= -(|c| - 1)` and `r` of the sign of
  the dividend (two disjunctions). The quotient cannot leave the type.
- `quot`/`rem` by an unknown divisor `d`: only the remainder is bounded —
  `d <= 0 ∨ r <= d - 1`; for an unsigned type `r <= dividend`; for a signed type
  `d <= 0 ∨ r >= 1 - d`, and `r` of the dividend's sign. Each holds for a zero
  divisor too, whose remainder is the dividend. The quotient is left unknown.

Every constraint is implied by the machine semantics of section 4 for the
valuation it describes, so a system with no integer solution means no machine
valuation makes the facts true and the goal false.

---

## 13. Certificate checking

A certificate refutes a system by a tree whose leaves are Farkas combinations.
The checker (`refutes`) keeps the *standing* constraints — the system's own and
those added by the splits and cases above the node — and accepts:

- **`FarkasSum`**: a non-empty list of `(position, multiplier)` naming standing
  constraints in strictly increasing position, each multiplier positive. The
  weighted sum of the constraints `Σ mi·(ei <= 0)` must have every variable's
  coefficient zero and a positive constant: then the constraints cannot hold
  together.
- **`IntegerSplit`**: an integer linear form `f` (variables in strictly
  increasing order, each once, each with a non-zero 64-bit coefficient) and two
  certificates, one checked with `f <= 0` added and one with `f >= 1`
  (`-f + 1 <= 0`) added. Every integer assignment satisfies one of the two.
- **`DisjunctionCases`**: a disjunction of the system named by its index and two
  certificates, one checked with each member added.

All arithmetic is 128-bit and overflow-checked; an overflow rejects. A
certificate has at most `max_certificate_nodes` nodes and nests at most
`max_term_depth` deep (`TRUST.md` TCB-CORE-006, TCB-CORE-009). The search that
finds certificates (`compiler/refutation`, Fourier–Motzkin elimination with
integer splits) is untrusted: when it finds nothing the claim is unproven, never
refuted (`SPEC.md` CASE-015).

---

## 14. Limits and resource failure

| Limit (`CoreLimits`)       | Default   | Bounds                                               |
| -------------------------- | --------- | ---------------------------------------------------- |
| `max_normalization_steps`  | `2^20`    | unfoldings and polynomial work in one normalization  |
| `max_term_depth`           | 512       | nesting of terms, propositions, evidence, certificates |
| `max_polynomial_terms`     | 256       | monomials in one polynomial                          |
| `max_monomial_degree`      | 64        | factors in one monomial                              |
| `max_arithmetic_facts`     | 256       | facts one arithmetic step uses                       |
| `max_certificate_nodes`    | `2^14`    | nodes in one certificate                             |

Types are bounded separately (section 2). Every bound, when exceeded, rejects
the evidence (`TRUST.md` TCB-CORE-007): exhausting a resource is never success
and never an assumption. The compiler checks every obligation with the defaults.

---

## 15. What the kernel does not contain

- **No axioms.** There is no axiom or assumption former and no way to admit a
  proposition without evidence. Trusted laws are premises closed over the goal
  (section 1); runtime validation sites, unsafe blocks, standard-library models
  and imported contracts are reported beside a claim and never enter the kernel
  as facts (`TRUST.md` 25–31).
- **No memory.** Places, versions, regions, capabilities, lifetimes and storage
  generations are correspondence-layer notions (`TRUST.md` 14, 15); none is a
  term or a proposition, and a capability has no path to the kernel's language.
- **No recursion.** Definitions are acyclic (section 6); loops and recursive
  functions are verified through obligations the kernel decides as ordinary
  propositions, never unfolded.
- **No solver and no search.** The kernel checks evidence; it never looks for
  it. The only procedures it runs are normalization, the arithmetic translation
  and certificate checking, each fully stated above.
- **No existential quantifier, no induction over signed integers, structures or
  the `@` domains, and no mathematical integers.** These are `SPECIFIED` or not
  started (`STATUS.md`, Proof system status).
- **No classical principle.** Nothing grants `P || not P`.

---

## 16. Testing

The kernel's tests look for acceptance of something false, since a rejection
proves nothing about soundness (`TRUST.md` TCB-TEST-001).

- **Rejection tests.** `tests/kernel/check_test.cpp`,
  `adversarial_kernel_test.cpp`, `malformed_test.cpp`, `induction_test.cpp`,
  `arithmetic_test.cpp`, `definedness_test.cpp`, `paths_test.cpp`,
  `value_model_test.cpp` and `evidence_regression_test.cpp` state, rule by rule,
  valid evidence that must be accepted and the nearest invalid evidence that
  must be rejected: wrong binders, wrong sides, forged hypotheses, capture,
  restatements that differ, malformed terms, overflowing certificates, a signed
  induction binder, and induction's premises pinned to exactly the ones the
  kernel must state, range premise included.
- **An independent model.** `tests/support/kernel/kernel_model` gives every
  well-formed term a value and every proposition a truth value by evaluation,
  never by the kernel's own normalization, substitution or arithmetic. Machine
  integers are the machine's; each abstract or indexed type is a small carrier
  whose observations are seeded pseudo-random functions. Integer types up to 4
  bits are enumerated and wider ones sampled at their edges and at seeded
  values. Truth is three-valued: a quantifier it could not enumerate or an
  exhausted budget gives `Unknown`, never a guess, and `False` is reported only
  with a counterexample.
- **The oracle.** `tests/support/kernel/kernel_generator` builds derivations
  rule by rule, each premise derived or supposed, with deliberate defects (a
  wrong argument, a wrong side, a swapped case, a restatement that differs, a
  claimed conclusion the step does not give), and arithmetic and automation
  samples whose certificates the untrusted refutation search and automation
  propose. `kernel_oracle` checks that the verdict is deterministic, that an
  acceptance carries its goal, and that an accepted goal is not `False` in any
  interpretation of the model; for terms, that normalization keeps a term's
  type and value and is idempotent, and that substitution and shifting mean
  what evaluation in an environment means.
- **In-suite property runs.** `tests/kernel/model_oracle_test.cpp`,
  `substitution_property_test.cpp` and `proof_fuzz_test.cpp` run the generator
  from fixed seeds, so every run is the same run.
- **Fuzz targets.** `tests/fuzz/kernel_proof`, `kernel_arithmetic` and
  `kernel_terms` run the same oracle on derivations, arithmetic and terms
  decoded from bytes; `kernel_certificate` gives the certificate checker
  systems and certificates decoded directly from bytes and searches a box of
  integer points for one an accepted certificate should have excluded. Every
  build replays each corpus, and CI's Fuzzing job searches under AddressSanitizer
  and UndefinedBehaviorSanitizer (`docs/CI.md`, "Fuzzing").
- **Mutation testing.** `scripts/test-mutations.sh` disables one check of the
  kernel at a time — each structural comparison of a derived conclusion with the
  goal, capture-safe shifting, hypothesis scope, the certificate check, each
  premise's evidence — in a disposable copy and requires a test to fail
  (`AGENTS.md` 24).

A finding in any of these is a soundness defect and follows `SECURITY.md`. The
absence of findings is evidence of care, not a proof: the model is finite, and
generated derivations cover what the generator can build.

---

## 17. Mechanization plan

The core is mechanized in part, in Coq 8.18.0, in `formal/coq`
(`tools/formal/check.sh`, CTest `formal_kernel_model`, CI job *Formal model*).
The model states `cppl-core-0.9.0` (`core_version` in `Syntax.v`), and
`tests/architecture/formal_model.sh` fails if the kernel's core version or its
number of term formers, primitives or evidence formers differs from the model's,
so a core change cannot leave the model describing another core
(`TRUST.md` TCB-META-001). Every theorem below is audited by `Print Assumptions`
to rest on nothing but Coq's kernel: no axiom and no admitted proof
(TCB-META-003).

| Milestone | State | Where |
| --- | --- | --- |
| M1 syntax and semantics | done | `Syntax.v`, `Semantics.v`: types, terms, propositions, evidence; machine integers, every primitive, `wrap` in range and exact on representable values, `rem` in range |
| M2 term operations | done | `Semantics.v` (`eval_shift`, `eval_inst`, `holds_pshift`, `holds_pinst`), `Typing.v` (typing preserved by both), `Normalize.v` (`nf_sound`: normalization keeps the type and the meaning of every typed term) |
| M3 arithmetic | certificate checking done; translation not started | `Certificate.v` (`check_certificate_sound`) |
| M4 the rules | done relative to M3 | `Checker.v` (`check_sound`, `check_consistent`), `Consistency.v`, `Normalize.v` (`check_sound_normalized`) |
| M5 reference checker | not started | |
| M6 re-checkable evidence | not started | |

`check_sound` is the soundness theorem of M4: if `check` accepts evidence `e`
for `P`, `P` holds in every interpretation and every environment. Its only
premises are that normalization preserves the meaning of a typed term (M2,
discharged by `Normalize.v` below) and that an accepted arithmetic step's facts
entail its goal (M3), stated as hypotheses. Everything else the rules do is defined in the model and proven:
typing and well-formedness, structural comparison, capture-safe substitution
and shifting, the hypothesis context and its scope, the premises the kernel
states for conditional elimination and for induction, and the goal staying well
formed wherever the checker reaches it. `syntactic_consistency` discharges both
premises for the checker whose reflexivity compares terms as written and which
has no arithmetic step, which is rules 2 to 8 and 10 to 15 exactly: no
evidence, well formed or not, establishes `False` in it, unconditionally.
`check_certificate_sound` proves the certificate checker of section 13: an
accepted certificate leaves the system with no integer solution. What remains
of rule 9 is the translation of section 12.

`Normalize.v` models the normalization of section 9 (`normalize_impl` in
`kernel/src/context.cpp`, `normalize_primitive` and the polynomial reader in
`kernel/src/arithmetic.cpp`): definitions unfolded into their bodies, operands
normalized, arithmetic of one type read into a polynomial modulo `2^w` over its
opaque subterms and rendered back, and comparisons, negation, selection,
representability, division and conversion folded where the kernel folds them.
`nf_sound` proves that it keeps the type and the meaning of every typed term,
and `check_sound_normalized` is `check_sound` with reflexivity deciding equality
by it, so the M2 premise is discharged. Its remaining conditions are on the
interpretation, not the checker: it gives each admitted definition the meaning
of its body, as it already gives each observation and call a value of its type.
Termination is by construction: unfolding is bounded by fuel, where the kernel
bounds it by the limits of section 14. The model's term order does not compare
the domains of observations and elements, so its normal forms can differ in
placement from the kernel's; that bears on which reflexivity steps the two
accept, not on soundness, since placement never changes a value.

The model omits the resource limits of section 14. They only reject, so the
model accepts at least what the kernel accepts, and a bound on what it accepts
bounds the kernel's. Its arithmetic is unbounded where the kernel's is 128-bit
and rejects on overflow, with the same effect.

What the model does not establish is that `kernel/src/check.cpp` implements it.
The model's checker is transcribed rule by rule from `check_under`, with the
C++ function each definition restates named beside it, and the drift test
catches a changed set of formers, not a changed rule. The C++ kernel therefore
stays in the logical TCB (TCB-META-002) until M5 runs an extracted checker
beside it and M6 lets it re-check a build's evidence.

The milestones as planned:

**M1 — Syntax and semantics.** Types, terms, propositions and evidence of
sections 2, 4, 7 and 10 as inductive types; the machine semantics of sections 3
and 4 as functions on the integers; the model of the kernel's
`tests/support/kernel/kernel_model` generalized to every interpretation of the
abstract sorts: `⟦t⟧ρ` for terms and `⟦P⟧ρ` for propositions. Exit: the model's
machine operations proven equal to their definitions for every width 1..64.

**M2 — Term operations.** The substitution and shifting lemmas of section 8
(`⟦t[a]⟧ρ = ⟦t⟧(ρ, ⟦a⟧ρ)`, shifting as weakening) and meaning preservation and
termination of normalization (`⟦nf(t)⟧ρ = ⟦t⟧ρ`, section 9), including the ring
normal form modulo `2^w`, the canonical comparisons and every fold. Exit: the
reflexivity rule proven sound.

**M3 — Arithmetic.** Soundness of the translation of section 12 (a machine
valuation making the facts true and the goal false extends to an integer
solution of the system) and of the certificate checker of section 13 (an
accepted certificate leaves no integer solution). Exit: rule 9 proven sound.

**M4 — The rules.** The soundness theorem for the whole judgment: if
`check(Σ, P, e)` accepts, then `⟦P⟧ρ` holds in every interpretation and every
environment. Rule 15 needs finiteness of the unsigned type; rules 2, 4, 7, 8 need
M2; rule 9 needs M3. Exit: consistency relative to the calculus — `False` has no
closed evidence.

**M5 — An executable reference checker.** A checker extracted from M4 run
beside the C++ kernel on the same inputs: the in-suite generator, every fuzz
corpus and every obligation the test suite produces. A disagreement is a defect
in one of the two. This is differential evidence about the C++ kernel, not a
replacement for it.

**M6 — Re-checkable evidence.** A versioned, serialized form of accepted
evidence (the *Serialized proof certificate format* of `STATUS.md`, not started),
so the extracted checker can re-check every obligation of a build. Only when a
build's claims are re-checked that way does trust move from the C++ kernel to
the mechanized checker and its proof assistant, and `TRUST.md` 5 records the
change when it happens.

The correspondence from source to core (`TRUST.md` 7–19) and erasure (`TRUST.md`
29) are outside this plan; each needs its own formal account before any part of
it can leave the correspondence or runtime TCB.

---

## 18. Changing the core

A change to anything this document states — a type, term or proof former, a
primitive's meaning, a typing or well-formedness condition, substitution,
normalization, a rule, the arithmetic translation, certificate checking or a
limit's effect — changes the formal core. It needs:

- an update to this document in the same commit, and to `FOUNDATIONS.md`,
  `SPEC.md` and `TRUST.md` where they state the concept;
- new versions in `kernel/include/cppl/kernel/version.hpp`, which also bind
  verification interfaces (`TRUST.md` TCB-XTU-008);
- valid, invalid and malformed evidence for the change, the model and generator
  of section 16 extended to cover it, and mutation entries for each new check;
- the mechanized model of section 17 changed to state the new core, with its
  proofs, so that `tools/formal/check.sh` and `architecture_formal_model` pass;
- an RFC when the change is substantial, and the soundness review of
  `.agents/skills/cppl-soundness-review`.

The `static_assert`s on the term and proof formers make an added alternative
fail to compile until every site deciding its meaning has been reviewed.
