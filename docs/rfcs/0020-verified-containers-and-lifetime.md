# RFC 0020: Verified standard containers, views and storage generations

## Status

Accepted, implemented as the subset stated in §9. The normative text is
`SPEC.md` Annex J, J.17 (`STDMODEL-010` onward); what is trusted is
`TRUST.md` §28.1.

## Summary

Verified code may use `std::array`, `std::vector`, `std::basic_string<char>`
and dynamic-extent `std::span` through a small set of public operations, with
their element accesses bounded, their mutations modeled, and every view,
element reference and element place tied to the **storage generation** of the
container that owns the storage.

Nothing about libc++ or libstdc++ is verified. What their public operations do
is stated once, abstractly, as **library summaries**: trusted statements about
the one observable fact the model keeps of a sequence, its length. Each is a
recorded trusted assumption, listed in `TRUST.md` and named in the trust closure
of every claim that uses it. No kernel rule, axiom or term former is added.

## Motivation

The downstream parser-invariant migration (ScanPrice) needs, at minimum:

```cpp
verified std::size_t skip_digits(std::span<const char> in, std::size_t at)
    expects (readable(in) && at <= in.size())
    ensures (result <= in.size())
{
    std::size_t i = at;
    while (i < in.size())
        invariant (at <= i && i <= in.size())
        decreases (in.size() - i)
    {
        if (in[i] < '0' || in[i] > '9') { return i; }
        ++i;
    }
    return i;
}
```

and a body that collects results into a `std::vector` it owns. (The character
comparisons there are integer promotions, which verified arithmetic does not
yet model; everything else in the example is this RFC's.) Today every one
of those operations is refused: a member call is not an ordinary function, a
container local has no modeled value, and there is no lifetime model that could
make a view sound. The storage model (RFC 0014) already has everything except a
notion of storage that can be *replaced*: before this RFC nothing a verified
body could do ended the lifetime of an object another name still designated.

## Goals

- Sound use of the listed operations, including after mutation.
- One mechanism for views, element references and element places: the storage
  generation, carried by the existing place/version machinery.
- Library behavior as explicit, reported trusted summaries over abstract state.
- Unsupported operations and unsupported types refused, never approximated.
- Zero runtime change: every container is the ordinary `std::` type.

## Non-goals

Iterators, `insert`/`erase`/`resize`/`emplace_back`/`at`/`front`/`back`,
`std::string_view`, static-extent spans, `subspan`, custom allocators,
`std::vector<bool>`, element types other than integers and `bool`, concurrency,
and verifying any library implementation.

---

# Part I — Model

## 1. Recognition

A type is a modeled sequence only by its Clang-resolved identity: a
specialization of the class template `vector`, `basic_string`, `span` or
`array` declared directly in namespace `std` (inline namespaces such as libc++'s
`__1` and libstdc++'s `__cxx11` are transparent), after alias expansion and
substitution. A user type spelled like one is not one (`AGENTS.md` 39,
`TCB-LIB-001`). Further conditions, each checked, each refused by name:

```text
std::vector<T, A>          A is std::allocator<T>; T is not bool
std::basic_string<C, T, A> C is char, T is std::char_traits<char>,
                           A is std::allocator<char>
std::span<T, E>            E is std::dynamic_extent
all                        T is a modeled integer or bool, possibly refined
```

## 2. Abstract value

A `vector`, `string` or `span` is modeled as an abstract value of a nominal
kernel value type (`FOUNDATIONS.md` 44) with exactly one total observation: its
**length**, of `size_type`. Nothing else about the object is modeled as a value:
not capacity, not the data pointer, not the element sequence. `size()`,
`length()` and `empty()` are that observation, and `size() == 0`.

`std::array<T, N>` keeps its existing product model (`SPEC.md` 20.4): `N`
components. `size()` is the literal `N` and `empty()` the literal `N == 0`.

Elements of a `vector`, `string` or `span` are not part of its value. They are
**element places** of the storage the container owns or the span views, read and
written through the ordinary storage model (§3). Reading one yields the value
its current version holds; it is never a projection of the container's value.

## 3. Places

A `vector` or `string` local, or a container parameter, is one tracked place:
the **root**, whose versions carry the container's abstract value. Its elements
are places projected from that root, `v[i]` a symbolic element and `v[3]` a
constant one, exactly as a built-in array's are (`SPEC.md` STORAGE-010), with
two differences:

1. A write to an element does not reach the root. The root's value is the
   length, and no element write changes the length. A write to the root
   (any operation of §6) reaches every element.
2. An element place is formed **at a generation** (§4) and names storage only
   while that generation is current.

A `span` local is a root of its own whose value is the span's length. Its
elements are the element places of the container it views, so `s[i]` and
`v[i]` at one index term are one place: a write through either is seen through
the other, and both owe the same element refinement. A `span` parameter's
elements are places of caller storage, may alias any other caller storage, and
are reachable only under a capability (§7).

A `std::array` local or by-value parameter is tracked as its `N` element places,
exactly as a built-in array is, and `a[i]` is the same element place a built-in
subscript names. An element of a `std::array` a reference designates is
refused: the reference is caller storage whose elements no alias analysis
reaches today.

A container handed to a verified call by value is copied into the parameter
under the copy summary and is unchanged by the call; handed by `const`
reference it is unchanged; handed by mutable reference it gets a new
generation (§4).

## 4. Storage generations

The version of a container's root is its **storage generation**. Every
operation that may reallocate, shrink, replace, move from or end the storage
establishes a new root version: `push_back`, `pop_back`, `clear`, `reserve`,
assignment, move construction from it, a verified call that takes it by
mutable reference, an unsafe block that may reach it, and a loop head for a
loop that does any of these. An element write does not.

Three things depend on a generation:

```text
element place     formed at the generation current where it is formed; after
                  the generation changes it is no longer the place `v[i]`
                  names, so the next access forms a new place and owes its
                  bound again, against the length current then
view              a span local records the generation of the container it
                  views; used at any other generation it is refused
element reference `T& r = v[i]` records the generation; used at any other
                  generation it is refused
```

The first is precision bookkeeping: an old element place is simply never
matched again. The second and third are lifetime rules: a stale view or
reference designates storage that may no longer exist, and using it is
undefined behavior, so it is a diagnostic naming the operation that may have
invalidated it. Comparing generations is comparing version numbers, decided per
path by the lowering (§8), with no merge: each path carries its own versions,
and a loop head gives every place the loop may reallocate a fresh one.

This is the generic lifetime dependency the storage model lacked. It is not a
container fact system: the generation is the root place's version, established
and invalidated by the one write path and the one alias analysis.

## 5. Scope, destruction and escape

A view or an element reference is formed only by a declaration's initializer,
is never reassigned, and is never returned; a span local is formed only over a
container, never from another span. A verified function whose result is a span
is refused. A container declared in a block is
destroyed at the block's end, after every view and reference declared in that
block or in blocks nested in it, and none declared outside can designate it. So
no view outlives the storage it was formed over by scope: the only way storage
ends while a view or reference is in scope is an operation of §4, which the
generation catches.

## 6. Operations and summaries

Each modeled operation is either an **observation**, a term over the current
root version, or a **summary**, a call whose effect is a new root version and
whose postcondition is supposed on the normal path, exactly as a verified
callee's is (`SPEC.md` VERIFIED-014). A mutator's summary takes the container
twice: once as the place it writes, once as the value it had before, so the
postcondition can relate the two without `old`.

```text
observation   v.size(), v.length(), s.size()      the length
              v.empty(), s.empty()                length == 0
              a.size(), a.empty()                 N, N == 0
              v[i], s[i], a[i]                    element place, owes i < length
construction  S v; S v{e0..ek-1}; S v(n); S v(n, x); std::string s = "lit";
                                                  length == 0, k, n, n, strlen
              S w = v; S w = std::move(v)         length == length(v);
                                                  a move writes v, of which
                                                  nothing is then known
              std::span<const T> s(v), = v, {v}   length == length(v)
mutation      v.push_back(x), s += c              length' == length + 1
                                                  and length < length'
              v.pop_back()                        requires length != 0;
                                                  length' == length - 1
                                                  and length' < length
              v.clear()                           length' == 0
              v.reserve(n)                        length' == length
              s.append(t), s += t                 length' == length + length(t),
                                                  length <= length',
                                                  length(t) <= length'
              v = w, v = std::move(w)             length' == length(w)
```

Every fact a summary states holds of every conforming implementation: a
successful `push_back` cannot wrap the length, since the length never exceeds
`max_size()`, which is below the largest `size_type`, and exceeding it throws.
A summary states nothing about capacity, so nothing about when reallocation
happens: every mutator is assumed to reallocate. `pop_back` owes its
precondition like any callee's. An operation that throws does not return
normally, and a normal-return postcondition is never supposed on an exceptional
exit (`SPEC.md` 33, `TCB-EXCEPT-001`); verified code has no handler, so the
exception leaves the function.

A value entering an element place, whether by construction, `push_back`, a
subscript write or a write through a span, is a refinement crossing into the
element type and owes its predicate through the one write path (`SPEC.md`
17.2). An element read supplies the element type's predicate exactly where the
accounting is closed (`SPEC.md` REFINE-060, REFINE-061): a container local whose
every element write was modeled, whose address never escaped, and that no
unsafe block names. A refined element type is therefore admitted only for
container locals, never for container parameters, whose element validity no
caller proof could establish; a writable view of a refined container is never
passed to a callee, which could write an unrefined value.

## 7. Spans are borrowed and hold no validity by existing

A `std::span` parameter states a region of caller storage and nothing about its
validity. Reading an element requires `readable(s)` and writing one
`writable(s)` in the function's `expects`, the capability of `SPEC.md` 12.10
applied to the span's whole extent. A span parameter is taken by value only.

Because a contract states one `expects` clause, a capability may now be
conjoined there with ordinary predicates, `expects (readable(s) && i <
s.size())`. The formal projection reads the conjunction as shapes of both
kinds; elaboration puts the capabilities on the capability channel and the
predicates among the preconditions, and a caller owes both. The meaning is the
conjunction (`ARCHITECTURE.md` 25); any other combination of a capability with
a predicate, under a negation, a disjunction or an implication, is still
refused. The capability is owed at every verified
call exactly as a pointer's is (`TCB-CAP-009`): the caller passes a span
parameter it holds the same capability for, a live span local, or a container
converted to a span at the call, whose storage the caller owns or borrows by
reference.

What a capability promises is extended for storage that can now end: the
storage a capability designates is live for the call, and is not element
storage of a container the function can reach by mutable reference or by value.
A caller proves it where the call is made: a span or `data()` argument over a
container, passed in the same call as that container by mutable reference, is
refused. Inside the function, reallocating a container it reaches by mutable
reference therefore leaves every capability intact. Without this, a parser
reading a span and appending to an output vector could never be verified, since
the span could view the vector's own bytes.

`v.data()` of a `vector`, `string` or span is admitted only as an argument of a
verified call whose parameter carries a capability, and grants it over the
length: `readable(p, n)` there owes `n <= v.size()`, and `writable` requires a
container the caller may write. `data()` of a `std::array` is refused. A call
that may write through a span or data pointer it is handed leaves every element
of the container unknown afterwards, and its length unchanged.

## 8. Where this is decided

```text
bridge       recognition, element types and refinements, the root and element
             places, generations, liveness of views and references, the
             disjointness check at a call, the lowering of each operation into
             versions, bounds and library calls
formal       a capability conjoined with predicates projects as a conjunction
projection   of shapes
elaboration  `vir::Call::library` names the operation of a library call;
             `vir::Function::library_models` the models a function uses; a
             mixed `expects` split into its two channels
obligations  the library summaries (compiler/obligations/src/library.cpp),
             stated once per operation and specialization, supposed at each
             call on the path, preconditions owed; capabilities of span and
             data() arguments owed
trust        each claim closed over the models its function and its verified
             callees use (compiler/obligations/src/trust.cpp); the driver
             lists them and never counts one assumption-free
kernel       unchanged
```

The generation check is a flow analysis over version numbers, like capability
tracking (`TRUST.md` 15): decidable, deterministic, correspondence TCB, never a
kernel proposition. Bounds are still proved by the kernel from the length
observation (`SPEC.md` VERIFIED-038, STORAGE-005).

---

# Part II — Consequences

## 9. Implemented subset and refusals

Implemented: every row of §6, container locals, container parameters by value,
by `const&` and by `&` (unrefined elements), containers passed to verified
calls by value and by reference, span parameters under a capability (by value),
a capability conjoined with predicates in one `expects`, span locals over a
container local or parameter, element references, containers mutated in loops,
`size()` in contracts, loop invariants and measures, `data()` as a capability
argument. `tests/fixtures/containers.cpp` holds each, the accepted twin of a
refusal in `tests/fixtures/negative/container_*.cpp`.

Refused in verified code: every other member function and operator of a
modeled container (by name), a member call whose object is not a tracked
container, a mutator nested inside another expression, iterators and
range-`for`, a span built from anything but a tracked container (another span
included), a span assigned, a span parameter by reference, a refined element
type on a parameter, a copy that would introduce a refinement, a writable view
of a refined container passed to a call, a moved-from container read, a
container moved from while it is caller storage, self-assignment, an element of
a `std::array` a reference designates, `data()` of a `std::array` or anywhere
but a capability argument, and a returned span.

The motivating example's character comparisons wait on integer promotions in
verified arithmetic, which this RFC does not add.

## 10. Trust

```text
kernel rules added            0
axioms added                  0
term formers added            0
trusted assumptions added     the library summaries and observations of §6,
                              each named in the trust closure of every claim
                              that uses one
correspondence added          recognition, the place and generation model,
                              view liveness, the call disjointness check,
                              the split of a mixed `expects`
```

A claim resting on a library model is PROVEN relative to it, never
assumption-free, and the trust report lists it under `Library-model-dependent
claims` with each model it rests on, and whether it rests on it in its own
contract or body or through a verified call it makes. The closure is per model,
not per operation: every operation of a model is one trusted statement of
`TRUST.md` 28.1, listed there word for word.

## 11. Erasure and ABI

Nothing is added to the program: a container stays the ordinary `std::` type of
the selected library, a span capability erases with the contract, and a
generation is proof bookkeeping. `tests/fixtures/equivalence/containers.cpp`
compiles to the same code as its hand-erased counterpart, and the suite runs on
both libc++ and libstdc++ (`make ci-linux-gcc`).

## 12. Alternatives considered

**Model the element sequence as a value.** A kernel sequence type would let a
contract state element facts, but needs a term former for sequences of runtime
length and an update operation, which is a kernel change for precision this
slice does not need.

**Keep element facts across `push_back`.** The standard preserves element
values on reallocation, but every fact would have to move to new places whose
identity the model does not track; conservatively dropping them costs
precision, never soundness.

**Treat span parameters like reference parameters.** A reference binds a live
object; a span may dangle legitimately, so validity must be stated.

**Revoke capabilities on any caller-storage reallocation.** Sound, but it makes
the central parser shape unverifiable. The disjointness obligation at the call
keeps it sound and verifiable.
