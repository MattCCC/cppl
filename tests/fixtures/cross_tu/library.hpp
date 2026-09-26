// The public contracts of library.cpp (SPEC.md TU-001, TU-002, TUBOUND-002).
//
// library.cpp proves each of them and records what it proved in its
// verification interface. A unit that only includes this header may use a
// contract exactly when it imports that interface, and only as it states the
// contract itself from these declarations (SPEC.md TUBOUND-003, TUBOUND-004).
#pragma once

type Small = unsigned where (self < 4u);

// A precondition every caller owes, and a postcondition every caller may use.
verified unsigned clamp4(unsigned x)
    expects (x < 100u)
    ensures (result < 4u);

// A refined result: a caller learns the refinement's predicate.
verified Small small_of(unsigned x)
    expects (x < 100u);

// Total: its loop states a measure.
verified unsigned count_to(unsigned n)
    ensures (result == n);

// Partial: its loop states none, so it holds only if it returns.
verified unsigned count_up(unsigned n)
    ensures (result == n);

// Proven only relative to a trusted law of library.cpp.
verified unsigned never_seven(unsigned x)
    expects (x < 10u)
    ensures (result != 7u);

// Proven across an unsafe block of library.cpp.
verified unsigned sensor()
    ensures (result <= 100u);

// Writes through a reference: the caller's storage takes the post-state the
// contract states and nothing else.
verified void bump(unsigned& counter)
    expects (counter < 50u)
    ensures (counter < 51u);

// Two overloads are two functions with two contracts.
verified unsigned step(unsigned x)
    ensures (result == x);
verified unsigned long step(unsigned long x)
    ensures (result == x + 1ul);

// An explicit specialization is a function of its own; the primary template
// is not verified anywhere.
template <unsigned N> unsigned bound(unsigned x);

template <>
verified unsigned bound<4u>(unsigned x)
    ensures (result < 4u);
