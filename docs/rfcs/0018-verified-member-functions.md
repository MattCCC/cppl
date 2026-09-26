# RFC 0018: Verified member functions

## Status

Accepted, prototype implemented for statically bound member functions.
Virtual functions, constructors, destructors and members of class templates
remain refused.

## Summary

A non-static member function that C++ binds statically is a verified callable
whose implicit object is storage. The object is not a new kind of value: each
scalar subobject the implementation models is a place, and the callable takes
each such place as a reference parameter standing before its written ones. The
body then reads and writes members through the one read and write path
functions already use, a call on an object passes the object's places as the
callee's object, and everything the obligation layer and kernel already do for
reference parameters, aliasing, call post-states, refinements and termination
applies unchanged. No term, rule or axiom is added.

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
- Calling a member function inside a contract. A contract names members, not
  member function results.
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

**Implicit object.** For a member function of class `C`, the implementation
enumerates the scalar places of a `C` object it models: every data member of
scalar type, every member of a member of class type, and every element of a
member array, in declaration order, each identified by its path from the
object. The callable's parameters are those places followed by the written
parameters (CLASS-008). The places are rooted at the canonical declaration of
`C`, so every member function of `C` and every caller agrees on them.

**Passing.** A `const` member function takes each place as a `const` reference,
except a place reached through a `mutable` member; a member function without
`const` takes each place as writable; `&&` takes them as rvalue references
(CLASS-009). The qualifiers change what the body may write, never what another
path may write: the object is external storage and may alias any reference
argument.

**Reads and writes.** A member named in the body, through implicit `this`,
`this->m` or `(*this).m`, resolves to its place and is read and written through
the existing path (CLASS-010). A write versions exactly that place, owes the
member's refinement, and invalidates every place that may alias it. Distinct
members are distinct places, so a write to `limit` leaves `value`'s fact
standing; a write through a reference parameter, a call that may write, or an
unsafe block invalidates every place of the object it may reach, and a write to
a member invalidates what a reference parameter or an object passed by
reference may hold of it.

**Contracts.** A precondition names each member's entry value, a postcondition
its normal-return value (CONTRACT-009). A refined member's predicate is assumed
on entry and owed at every normal return, as for a refined reference parameter.

**Calls.** A call `o.f(args)`, `f(args)` or `this->f(args)` names its object as
storage: the caller's implicit object, a local, a parameter, or a member or an
element at a constant of one (CLASS-011). The object's places are the callee's
leading arguments; the call owes the callee's preconditions at them; a callee
that may write its object, or writes through any reference argument, gives
every place of the object a post-call version, about which the caller knows
exactly the callee's postcondition. A place passed both as part of the object
and as a reference argument is one storage with one post-call version. A call
into the caller's recursion group owes the measure at the object's places as
at every other argument (TERMINATION-007).

**Static member functions** have no implicit object and are functions
(CLASS-012).

**Interaction.** Equality, normalization and the kernel are unchanged. A member
function is never a pure definition that a proposition may unfold; its
contract is its only interface, as for any verified function with reference
parameters.

## Runtime semantics

Unchanged. The member function, its class and every call to it execute as the
C++ that remains after erasure.

## C++ interoperability

- **Ordinary C++.** Unverified member functions, and unverified callers of
  verified ones, are ordinary C++. A verified member function called from
  unverified code is an ordinary call; its precondition is not checked there,
  since verification adds no runtime check (SPEC.md 57). A member function
  whose ref-qualifier is `&&` is verified, but a verified caller cannot yet
  name an rvalue object for it, so it is called from unverified code.
- **Overload resolution.** Clang resolves every call; the bridge follows the
  resolved declaration. `const`/non-`const` and `&`/`&&` overloads are distinct
  callables.
- **Templates.** Member function templates and members of class templates are
  refused (CLASS-015).
- **ABI and layout.** Unchanged (CLASS-013); the erasure equivalence test
  compiles the program and a hand-erased reference in C++17, C++20 and C++23,
  requires identical assembly at `-O0` and `-O2` and identical output, and both
  assert the classes' size, alignment and a member offset.
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
- a write of a refined member that does not satisfy its refinement;
- a recursive member call not made at a smaller measure;
- `verified` on a virtual function (explicit, `override`, `final`, or implicit
  override) and a call to a virtual function from verified code;
- a verified constructor or destructor;
- a member function template or a member of a class template;
- a member function of a union or of a class with a base subobject;
- `this` as a value, a member whose storage is not modeled, a call through a
  pointer to member function, and a call on an object not named as storage.

## Trust impact

No kernel rule, axiom or proposition is added. The trusted delta is
correspondence code in the Clang bridge: enumerating the object's places,
resolving member accesses and calls to them, choosing the passing from the
qualifiers, and the virtual-dispatch refusals (`TRUST.md` TCB-OBJ-006 to
TCB-OBJ-008, TCB-VIRTUAL-004). A mistake there could attribute a write to the
wrong place; the matched pairs and the mutation checks registered for it guard
exactly those decisions.

## Erasure

The contract clauses and `verified` erase to blanks (ERASE-004). The class
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
