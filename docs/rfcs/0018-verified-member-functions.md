# RFC 0018: Verified member functions

## Status

Accepted, prototype implemented for statically bound member functions, and
revised after the production review recorded under "Revision" below. Virtual
functions, constructors, destructors and members of class templates remain
refused.

## Summary

A non-static member function whose dispatch target is statically determined is
a verified callable whose implicit object is a receiver place. The object is not
a new kind of value: each subobject the implementation models is a place
projected from the receiver by the ordinary member and element steps, and the
body reads and writes members through the one read and write path functions
already use. This implementation lowers the receiver to one reference parameter
per scalar place, standing before the written ones; the lowering is proof
bookkeeping, not source semantics, and not a runtime parameter. A call on an
object passes the places of the object it names as the callee's object, and
everything the obligation layer and kernel already do for reference parameters,
aliasing, call post-states, refinements and termination applies unchanged. No
term, rule or axiom is added.

## Revision

The first text of `SPEC.md` F.5.1 tied the rules to this implementation. A
production review asked for the source-language rule and the implementation to
be separated, and for two semantic corrections. The rules CLASS-008 to CLASS-015
now read as the review stated them, and the implementation follows:

- **The receiver is a place (CLASS-008).** Flattening `this` into one parameter
  per scalar is an internal lowering an implementation MAY use, and MUST keep the
  object's identity and alias relations. This one does: every place is projected
  from one root by the numbering a member access resolves to, a caller passes the
  places of one object as one object, and the places are external storage the
  common alias model relates to every other access path.
- **A ref-qualifier constrains the call, not the places (CLASS-009).** The first
  version bound every place of an `&&` member function as an rvalue reference.
  Inside the body a member is storage like any other; the qualifier decides which
  receivers the call may be made on, which Clang's overload resolution settles.
  A verified caller now makes such a call on a named object through `std::move(o)`,
  `std::forward<T>(o)` or `static_cast<T&&>(o)`, each of which designates `o`
  itself; a copy of `o` is a temporary, and stays refused.
- **Validity is not charged again at a return (CLASS-010, REFINE-060 to
  REFINE-062).** A refined place received by reference used to owe its predicate
  at every normal return. Now each value entering the place is charged where it
  enters, and the return owes nothing for a version so charged. Every route was
  audited: a direct write charges the place; a write through another access path
  that may reach the place charges the place's refinement for the value written,
  and leaves it valid exactly when it was valid before; a call's effect is
  charged at the caller; an unsafe block, a loop head, a call writing another
  argument, and any route not listed charge nothing, so the version they leave is
  not derived valid, and the return is charged for it. The accounting lives in
  the bridge (`valid_versions`), absent by default, and is a stated trust delta
  (`TRUST.md` TCB-OBJ-009). It applies to a refined reference parameter of any
  function, which the same REFINE rules govern.
- **Disjointness is qualified (CLASS-010).** Two scalar places of one object are
  disjoint because C++ never lets two scalar objects that are not bit-fields
  overlap, `[[no_unique_address]]` included. Reference members, bit-fields,
  members of anonymous unions and structs, and volatile members have no place and
  are refused where a body names them.
- **Aliasing follows the common model (CLASS-010, CLASS-011).** The first version
  filtered the places a call's write reaches by modeled type, which the common
  alias model does not do and `AGENTS.md` forbids. A call's writes now invalidate
  exactly what `BodyLowering::may_alias` does not keep apart, and a place the
  callee only reads keeps its version when no write of the call can reach it.
- **The receiver is any place the model forms (CLASS-011, CLASS-015).** The object
  a pointer parameter designates is a receiver, formed as dereference places under
  `readable(p)`, and `writable(p)` for a callee that may write. Receivers this
  implementation forms no place for -- a mutating call on an object a parameter
  designates by reference, an element of an array of class type at a term, a
  temporary -- are limitations recorded in `docs/STATUS.md`, not language rules.
- **Static members (CLASS-012).** `pure` applies to a static member function, which
  is then a definition a contract may use.
- **Erasure (CLASS-013).** The lowered places are not runtime parameters; the
  erasure-equivalence test now also requires each member function to keep the
  mangled name of its written signature and the program to define exactly the
  symbols its hand erasure defines.
- **Qualified definitions (CLASS-008).** A member's out-of-class definition may
  not restate its contract, which is stricter than the identical repetition
  CONTRACT-005 and TU-003 permit for other functions. The asymmetry is kept as the
  review stated it.

Auditing the return charge found a defect in the obligation layer, fixed on its
own: the post-state a return hands back was stated before the calls in the
returned value were in scope, so `ensures (r == 5u)` was proven of a function
that never wrote `r` and returned a call whose result was 5
(`negative/post_state_after_returned_call.cpp`).

## Motivation

Before this RFC `verified` was refused on any member function ("'verified' is
applied outside namespace scope"), so the ordinary shape of C++ -- a class
whose operations keep a property of its members -- could not be verified at
all:

```cpp
class Account {
public:
    verified void deposit(unsigned amount)
        expects (amount <= limit_ && balance_ <= limit_ - amount)
        ensures (balance_ <= limit_)
    {
        balance_ = balance_ + amount;
    }

private:
    unsigned balance_;
    unsigned limit_;
};
```

The implicit object is storage the caller owns and passes by reference. The
language already verifies functions over caller storage passed by reference
(VERIFIED-025, VERIFIED-030, VERIFIED-032), including aliasing between
reference parameters and the versions a call leaves behind. A member function
is that same function with one argument written in front of the call instead of
inside its parentheses. Modeling it as such keeps one storage model and one
call rule, where a separate "object state" model would duplicate both and
could disagree with them.

## Goals

- Verify statically bound member functions: `const`, mutating, overloaded,
  cv- and ref-qualified, with parameters, returns, `expects`, `ensures`,
  `decreases`, refined members and nested members.
- Compose member functions with member functions and free functions in both
  directions.
- One storage model: member places are `Place`s and are versioned, aliased and
  invalidated exactly as reference parameters are.
- No assumed disjointness between the implicit object and any argument.
- Fail closed on virtual dispatch, lifetime boundaries and anything else not
  modeled, at the declaration or expression that asks for it.
- Erasure leaves the class exactly as written: same members, layout, ABI.

## Non-goals

- Override substitutability (CONTRACT-014, CONTRACT-015). Until it is checked,
  virtual functions and virtual calls are refused (CLASS-014).
- Constructors and destructors: their object's lifetime begins or ends, which
  the storage model does not yet state (CONTRACT-011, CONTRACT-012).
- `old(...)`. A mutating call's post-state is what its postcondition states and
  nothing else; relating it to the entry state needs `old`, which is a separate
  proposal for every function, not only member functions.
- Calling a member function with an implicit object inside a contract. A
  contract names members, not such a function's results; a `static pure` member
  function is a definition like any pure function.
- Member function templates and members of class templates (TEMPLATE-001).
- Any mechanism of its own across translation units. A member function crosses
  through the verification interface of RFC 0017 as a function does.

## Proposed syntax

None. `verified`, `expects`, `ensures` and `decreases` are written on a member
function declaration exactly as on a function:

```cpp
struct Counter {
    unsigned value;

    verified unsigned get() const
        ensures (result == value)
    {
        return value;
    }

    verified void reset()
        ensures (value == 0u)
    {
        value = 0u;
    }
};
```

A member function defined outside its class inherits the contract of its
in-class declaration and does not restate it (CONTRACT-005); `verified` on the
qualified out-of-line declarator is refused because it would redeclare a
function declared elsewhere.

## Static semantics

**Implicit object.** For a member function of class `C`, the implicit object is
a receiver place, and each subobject the implementation models is a place
projected from it: every data member of scalar type, every member of a member of
class type, and every element of a member array, each identified by its path
from the object (CLASS-008). This implementation lowers the receiver to the
scalar places in declaration order, the callable's parameters being those places
followed by the written parameters. The places are rooted at the canonical
declaration of `C`, so every member function of `C` and every caller agrees on
them.

**Qualifiers.** A `const` member function observes each place as `const`,
except a place reached through a `mutable` member; one without `const` may write
every place (CLASS-009). A ref-qualifier constrains the value category of the
receiver the call may be made on, which Clang's overload resolution enforces;
inside the body the members are ordinary places. No qualifier changes what
another path may write: the object is external storage. A volatile-qualified
member function is refused.

**Reads and writes.** A member named in the body, through implicit `this`,
`this->m` or `(*this).m`, resolves to its place and is read and written through
the existing path (CLASS-010). A write versions exactly that place, is charged
the refinement of the value entering it, and invalidates every place the common
alias model does not keep apart from it. Two scalar places of one object are
apart, so a write to `limit` leaves `value`'s fact standing; a write through a
reference parameter or a pointer, a call that may write, or an unsafe block
invalidates every place of the object it may reach, and a write to a member
invalidates what a reference parameter or an object passed by reference may
hold of it. A member whose storage may overlap another place or change unseen
-- a reference member, a bit-field, a member of an anonymous union or struct, a
volatile member -- has no place.

**Contracts and validity.** A precondition names each member's entry version, a
postcondition its normal-return version (CONTRACT-009). A refined member's
predicate holds of its entry version; every operation establishing a later
version is charged the predicate or leaves the version not derived valid, and a
normal return is charged the predicate only for a version not derived valid
(REFINE-060 to REFINE-062).

**Calls.** A call `o.f(args)`, `f(args)`, `this->f(args)`, `std::move(o).f()` or
`p->f(args)` supplies the place its receiver expression resolves to (CLASS-011):
the caller's implicit object, a local, a parameter, a member or an element at a
constant of one, or what a pointer parameter designates, under its stated
capability. The object's places are the callee's leading arguments; the call
owes the callee's preconditions at them. Each place the callee may write, and
each place it only reads that the common alias model does not keep apart from
one it writes, gets a post-call version, about which the caller knows exactly
the callee's postcondition; a place no write of the call can reach keeps its
version. A place passed both as part of the object and as a reference argument
is one storage with one post-call version. A call into the caller's recursion
group owes the measure at the object's places as at every other argument
(TERMINATION-007).

**Static member functions** have no implicit object and are functions; a
`static pure` one is a definition a contract may use (CLASS-012).

**Interaction.** Equality, normalization and the kernel are unchanged. A member
function with an implicit object is never a pure definition that a proposition
may unfold; its contract is its only interface, as for any verified function
with reference parameters.

## Runtime semantics

Unchanged. The member function, its class and every call to it execute as the
C++ that remains after erasure.

## C++ interoperability

- **Ordinary C++.** Unverified member functions, and unverified callers of
  verified ones, are ordinary C++. A verified member function called from
  unverified code is an ordinary call; its precondition is not checked there,
  since verification adds no runtime check (SPEC.md 57). A member function
  whose ref-qualifier is `&&` is verified, and a verified caller makes the call
  on a named object with `std::move`, `std::forward` or `static_cast<T&&>`.
- **Overload resolution.** Clang resolves every call; the bridge follows the
  resolved declaration. `const`/non-`const` and `&`/`&&` overloads are distinct
  callables.
- **Templates.** Member function templates and members of class templates are
  refused (CLASS-015).
- **ABI and layout.** Unchanged (CLASS-013); the erasure equivalence test
  compiles the program and a hand-erased reference in C++17, C++20 and C++23,
  requires identical assembly at `-O0` and `-O2` and identical output, requires
  each member function to keep the mangled name of its written signature and
  both programs to define the same symbols, and both assert the classes' size,
  alignment and a member offset.
- **Virtual functions.** Refused, together with every call to a function that
  has overrides, qualified or not (CLASS-014). Clang's own answer to "is this
  virtual" (declared or implicit override) is the authority.
- **Constructors and destructors.** Refused with `verified` (CLASS-015). An
  implicit or unverified constructor is ordinary C++ and runs as written.
- **Translation units.** A member function defined in another unit is used
  through that unit's verification interface (RFC 0017). The record is keyed
  by Clang's USR, which distinguishes `const` and ref-qualified overloads, and
  the statement it records includes the implicit object's places and how the
  function binds them, so a caller whose class declares another member,
  another `mutable` member or another contract is refused. An out-of-line
  definition cannot restate the contract, so a body that needs loop clauses is
  defined in the class.

## Safety

These programs must be rejected, each with a diagnostic at its source:

- a postcondition false of the post-state;
- a fact about a member read after a call, a reference write or an unsafe
  block that may have changed it, including through a `const` call on an
  object whose member is aliased by a reference argument;
- a `const` member function relied on not to write a `mutable` member;
- a value entering a refined member that does not satisfy its refinement,
  whether by assignment, through a reference that may be the member, or by a
  call's effect, and a version no route charged handed back at a return;
- a recursive member call not made at a smaller measure;
- `verified` on a virtual function (explicit, `override`, `final`, or implicit
  override) and a call to a virtual function from verified code;
- a verified constructor or destructor, and a volatile member function;
- a member function template or a member of a class template;
- a member function of a union or of a class with a base subobject;
- `this` as a value, a member whose storage may overlap another place or change
  unseen, a call through a pointer to member function;
- a call on what a pointer designates without the capability it needs, and a
  call on an object for which no sound place is formed.

## Trust impact

No kernel rule, axiom or proposition is added. The trusted delta is
correspondence code in the Clang bridge: lowering the receiver to its places,
resolving member accesses and calls to them, choosing the binding from the
constness, forming a pointer's receiver under its capability, the
virtual-dispatch refusals, and the validity accounting that lets a return owe
nothing for a version charged where it was established (`TRUST.md` TCB-OBJ-006
to TCB-OBJ-009, TCB-VIRTUAL-004). A mistake there could attribute a write to the
wrong place or count an uncharged version as valid; the matched pairs and the
mutation checks registered for it guard exactly those decisions.

## Erasure

The contract clauses and `verified` erase to blanks (ERASE-002, ERASE-003). The class
keeps its members, member functions, signatures, qualifiers and layout. The
analysis probes the projection adds for a member function's clauses are
confined to analysis text and never reach the runtime program.

## Diagnostics

Refusals name what is not modeled at the declaration or expression that asks
for it:

```text
methods_virtual_function.cpp:10:5: error [unsupported-semantics]: a verified virtual function is not verified by this implementation
methods_virtual_call.cpp:14:23: error [unsupported-semantics]: verified function 'Shape::dispatched' has a body this implementation cannot state as a value: a virtual call dispatches on the object's dynamic type
methods_unmodeled_receivers.cpp:9:23: error [unsupported-semantics]: verified member function 'Word::get' is not verified by this implementation: its implicit object is not one this implementation models: its class is a union
```

A false claim is a kernel rejection on the return path of the member function,
named `Class::function path N`.

## Alternatives considered

- **The object as one aggregate value.** Passing `*this` as a single abstract
  value would need a second write model (functional update of a record) beside
  the place model, and aliasing between the object and a reference argument
  would have to be restated for it. Flattening to places reuses the one model.
- **Assuming the object disjoint from reference arguments.** Unsound: `c.f(c.m)`
  is ordinary C++.
- **Checking virtual calls against the static type's contract.** Sound only
  with override substitutability checked for every override, which is not yet
  implemented; refusing is the fail-closed choice until it is.

## Drawbacks

- Without `old`, a mutating callee's postcondition must restate every member
  the caller still needs, since the others are forgotten across the call.
- The places of a large object are all passed, so every call on it versions
  all of them; precision follows only from postconditions.
- Classes with bases, unions and templates are out of reach for now.
- A write through a reference that may be a refined member is charged the
  member's refinement even where the reference is not the member, which costs a
  false rejection and never soundness.
- A mutating call on an object a parameter designates by reference, and a call
  on an element of an array of class type at a term, have no place formed for
  their receiver yet.

## Testing strategy

`tests/e2e/verified_methods.sh` verifies `tests/fixtures/verified_methods.cpp`
and runs it, and verifies a caller of `tests/fixtures/methods_cross_tu/` through
the defining unit's interface; `tests/negative/verified_methods.sh` refuses each
`tests/fixtures/negative/methods_*.cpp`, each the other half of a matched pair
with a function in the positive fixture; `tests/e2e/erasure_equivalence.sh`
compares the erased `tests/fixtures/equivalence/methods.cpp` with its
reference for output, assembly and layout. Mutation checks in
`scripts/test-mutations.sh` remove the decisions the trust delta depends on
and require these tests to fail.

## Compatibility

Programs that did not use `verified` on member functions are unaffected.
Programs that did were refused before and are verified or refused now.

## Unresolved questions

- `old(...)` over the implicit object and over reference parameters.
- Override substitutability, after which virtual functions and calls through a
  base can be admitted.
- Constructors and destructors as lifetime boundaries of the object's places.
- Member function calls in contracts, which would need member functions as
  pure definitions.
- Places for a by-reference aggregate parameter's members, which would admit a
  mutating call on the object it designates, and for members of an element
  selected at a term.
