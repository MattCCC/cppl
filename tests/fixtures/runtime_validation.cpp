// Runtime validation (SPEC.md 28, RUNTIMECHECK-001 to RUNTIMECHECK-021).
//
// Two ways an unknown runtime value comes to satisfy a refinement, kept apart.
// An ordinary C++ condition selects a path, and the crossing on it is proven
// from the path's facts: that is a static proof, PROVEN, and adds no runtime
// code (RUNTIMECHECK-002, RUNTIMECHECK-010). A validation expression,
// `validate<R>(e)`, asks the program to test the value against R's predicate
// at run time: what its success establishes is RUNTIME-CHECKED at that site,
// and every claim about the body rests on it (RUNTIMECHECK-011 to
// RUNTIMECHECK-014). Driven by `e2e/runtime_validation.sh`, which pins that
// the first kind are no sites and the second are, what rests on each, that
// every check and every validation survives erasure, and what the program does
// with valid and invalid input. Refused twins are in
// `negative/runtime_validation.sh`.
#include <cstdio>
#include <cstdlib>
#include <vector>

type Percentage = int where (self >= 0 && self <= 100);
type Positive = int where (self > 0);
type Small = unsigned where (self < 10u);
type Index(unsigned n) = unsigned where (self < n);

struct Reading {
    Positive level;
    int raw;
};

// SPEC: RUNTIMECHECK-002, RUNTIMECHECK-005, RUNTIMECHECK-007, RUNTIMECHECK-010
// The `if` selects the path, and the crossing on it is proven from the path's
// facts: PROVEN, no runtime validation site. The failure path returns a value
// of its own.
verified int percentage_or_zero(int raw)
    ensures (result >= 0 && result <= 100)
{
    if (raw >= 0 && raw <= 100) {
        Percentage p = raw;
        return p;
    }
    return 0;
}

// SPEC: RUNTIMECHECK-004, RUNTIMECHECK-007
// The failure path leaves before the crossing.
verified int positive_or_one(int raw)
    ensures (result > 0)
{
    if (raw <= 0) {
        return 1;
    }
    Positive p = raw;
    return p;
}

// SPEC: RUNTIMECHECK-010
// A value entering a refined result on a path a condition selects: proven
// from the path's facts.
verified Positive checked_result(int raw) {
    if (raw > 0) {
        return raw;
    }
    return 1;
}

verified int positive_identity(Positive p)
    ensures (result == p)
{
    return p;
}

// SPEC: RUNTIMECHECK-010
// A value entering a verified callee's refined parameter on a path a
// condition selects: proven from the path's facts.
verified int checked_argument(int raw)
    ensures (result > 0)
{
    if (raw > 0) {
        return positive_identity(raw);
    }
    return 1;
}

// SPEC: RUNTIMECHECK-010
// A conditional initializer: each arm is proven under its route's facts.
verified int checked_conditional(int raw)
    ensures (result > 0)
{
    Positive p = raw > 0 ? raw : 1;
    return p;
}

// SPEC: RUNTIMECHECK-010
// A loop's condition is a path fact like an `if`'s.
verified unsigned last_small(unsigned n)
    ensures (result < 10u)
{
    unsigned i = 0u;
    unsigned last = 0u;
    while (i < 10u)
        invariant (last < 10u)
        decreases (10u - i)
    {
        if (i >= n) {
            return last;
        }
        Small s = i;
        last = s;
        i = i + 1u;
    }
    return last;
}

// SPEC: RUNTIMECHECK-010
// An indexed refinement, entered at its index on a path a condition selects.
verified unsigned checked_index(unsigned raw)
    ensures (result < 4u)
{
    if (raw < 4u) {
        Index<4> k = raw;
        return k;
    }
    return 0u;
}

// SPEC: RUNTIMECHECK-010
// A refined member written on a path a condition selects.
verified int checked_member(int raw)
    ensures (result > 0)
{
    Reading reading{1, raw};
    if (raw > 0) {
        reading.level = raw;
    }
    return reading.level;
}

// SPEC: RUNTIMECHECK-010, STDMODEL-020
// An element entering a vector local's content invariant on a path a condition
// selects.
verified unsigned checked_elements(int raw)
    ensures (result > 0u)
{
    std::vector<Positive> values;
    if (raw > 0) {
        values.push_back(raw);
    }
    values.push_back(3);
    return static_cast<unsigned>(values.size());
}

// SPEC: RUNTIMECHECK-010
// The condition selects the path, and the crossing does not even need it.
verified int static_under_check(int raw)
    ensures (result == 5)
{
    if (raw > 0) {
        Positive p = 5;
        return p;
    }
    return 5;
}

// SPEC: RUNTIMECHECK-010
// What the precondition states, with a condition selecting the path.
verified int from_precondition(int raw)
    expects (raw > 0)
    ensures (result > 0)
{
    if (raw > 100) {
        return 100;
    }
    Positive p = raw;
    return p;
}

verified void cap(int& value)
    ensures (value <= 100)
{
    if (value > 100) {
        value = 100;
    }
}

unsafe void clobber(int* target);

// SPEC: RUNTIMECHECK-004
// A write, a verified call's effect and an unsafe block each give the local a
// version the first condition says nothing of, so the value is tested again
// before it enters. Twin of `runtime_check_stale_after_write`,
// `runtime_check_stale_after_call` and `runtime_check_stale_after_unsafe`.
verified int rechecked(int raw)
    ensures (result > 0)
{
    int value = raw;
    if (value <= 0) {
        return 1;
    }
    value = value - 1;
    if (value <= 0) {
        return 2;
    }
    cap(value);
    if (value <= 0) {
        return 3;
    }
    unsafe {
        clobber(&value);
    }
    if (value <= 0) {
        return 4;
    }
    Positive p = value;
    return p;
}

// SPEC: RUNTIMECHECK-010
// Proven through a contract proven from path facts: nothing to rest on.
verified int through_call(int raw)
    ensures (result > 0)
{
    return positive_or_one(raw);
}

// SPEC: RUNTIMECHECK-011, RUNTIMECHECK-012, RUNTIMECHECK-018
// An explicit validation: the program tests the value against Percentage's
// predicate at run time, and the crossing on the path where the test held
// rests on it. Twin of `validation_failure_path`.
verified int validated_percentage(int raw)
    ensures (result >= 0 && result <= 100)
{
    if (validate<Percentage>(raw)) {
        Percentage p = raw;
        return p;
    }
    return 0;
}

// SPEC: RUNTIMECHECK-007, RUNTIMECHECK-011
// The failure path leaves first; what follows is the path where the test held.
verified int validated_or_one(int raw)
    ensures (result > 0)
{
    if (!validate<Positive>(raw)) {
        return 1;
    }
    Positive p = raw;
    return p;
}

// SPEC: RUNTIMECHECK-011, RUNTIMECHECK-017
// A validation as an operand of `&&`: the crossing is where both held.
verified int validated_below(int raw)
    ensures (result > 0 && result < 1000)
{
    if (raw < 1000 && validate<Positive>(raw)) {
        Positive p = raw;
        return p;
    }
    return 1;
}

// SPEC: RUNTIMECHECK-011, RUNTIMECHECK-019
// A validation's result is an ordinary bool, which a local may hold.
verified int validated_later(int raw)
    ensures (result > 0)
{
    const bool positive = validate<Positive>(raw);
    if (positive) {
        Positive p = raw;
        return p;
    }
    return 1;
}

// SPEC: RUNTIMECHECK-012
// A write gives the local a version the validation said nothing of, so it is
// validated again. Twin of `validation_stale_after_write`.
verified int revalidated(int raw)
    ensures (result > 0)
{
    int value = raw;
    if (!validate<Positive>(value)) {
        return 1;
    }
    value = value - 1;
    if (!validate<Positive>(value)) {
        return 2;
    }
    Positive p = value;
    return p;
}

// SPEC: RUNTIMECHECK-014
// No site of its own: it rests on the one in the body of the function it
// calls.
verified int through_validation(int raw)
    ensures (result > 0)
{
    return validated_or_one(raw);
}

// SPEC: RUNTIMECHECK-011, RUNTIMECHECK-012, RUNTIMECHECK-019
// A validation as a loop's condition is tested at every iteration. The body
// may use its fact; the body's write makes the next test a new one.
verified int validated_halvings(int raw)
    ensures (result >= 0)
{
    int x = raw;
    int steps = 0;
    while (validate<Positive>(x))
        invariant (steps >= 0)
    {
        Positive p = x;
        x = p / 2;
        if (steps < 100) {
            steps = steps + 1;
        }
    }
    return steps;
}

// SPEC: RUNTIMECHECK-006, RUNTIMECHECK-010
// A checked helper: its proven postcondition is a fact of the path its call
// selects, so the crossing is proven statically, like any other from a
// verified callee's contract. No site.
verified bool is_percentage(int value)
    ensures (result <-> (value >= 0 && value <= 100))
{
    if (value >= 0 && value <= 100) {
        return true;
    }
    return false;
}

verified int checked_by_helper(int raw)
    ensures (result >= 0 && result <= 100)
{
    if (is_percentage(raw)) {
        Percentage p = raw;
        return p;
    }
    return 0;
}

unsafe void clobber(int* target) {
    *target = *target - 1;
}

int main(int argc, char** argv) {
    const int raw = argc > 1 ? std::atoi(argv[1]) : 0;
    const unsigned wide = raw < 0 ? 0u : static_cast<unsigned>(raw);
    std::printf("%d %d %d %d %d %u %u %d %u %d %d %d %d\n", percentage_or_zero(raw), positive_or_one(raw),
                checked_result(raw), checked_argument(raw), checked_conditional(raw), last_small(wide),
                checked_index(wide), checked_member(raw), checked_elements(raw), static_under_check(raw),
                from_precondition(raw > 0 ? raw : 1), rechecked(raw), through_call(raw));
    std::printf("%d %d %d %d %d %d %d\n", validated_percentage(raw), validated_or_one(raw), validated_below(raw),
                validated_later(raw), revalidated(raw), through_validation(raw), validated_halvings(raw));
    std::printf("%d\n", checked_by_helper(raw));
    return 0;
}
