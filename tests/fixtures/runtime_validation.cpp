// Runtime-checked refinement construction (SPEC.md 28, RUNTIMECHECK-001 to
// RUNTIMECHECK-015; RFC 0021).
//
// An unknown runtime value enters a refined type through ordinary C++: a
// condition the program evaluates, and a crossing on the path where it held.
// The crossing is proven like any other, under that path; what makes it a
// runtime validation site is that nothing proven of every execution would
// establish it without the condition. Driven by `e2e/runtime_validation.sh`,
// which pins which crossings are sites, what rests on each, that each check
// survives erasure, and what the program does with valid and invalid input.
// The refused twin of each accepted form is in `negative/runtime_validation.sh`.
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

// SPEC: RUNTIMECHECK-002, RUNTIMECHECK-005, RUNTIMECHECK-007
// The `if` is the validation. The local enters Percentage only on the branch
// where it held, and the failure path returns a value of its own.
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
// A value entering a refined result on a checked path.
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
// A value entering a verified callee's refined parameter on a checked path.
verified int checked_argument(int raw)
    ensures (result > 0)
{
    if (raw > 0) {
        return positive_identity(raw);
    }
    return 1;
}

// SPEC: RUNTIMECHECK-010
// A conditional initializer: the arm the condition selects is checked, the
// other enters statically, and the crossing is a site because one route
// needed the check.
verified int checked_conditional(int raw)
    ensures (result > 0)
{
    Positive p = raw > 0 ? raw : 1;
    return p;
}

// SPEC: RUNTIMECHECK-010
// A loop's condition is a runtime check like an `if`'s.
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
// An indexed refinement, entered at its index on a checked path.
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
// A refined member written on a checked path.
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
// An element entering a vector local's content invariant on a checked path.
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

// SPEC: RUNTIMECHECK-011
// The check selects the path, but the crossing does not need it: a literal
// is positive on every execution, so this is not a runtime validation site.
verified int static_under_check(int raw)
    ensures (result == 5)
{
    if (raw > 0) {
        Positive p = 5;
        return p;
    }
    return 5;
}

// SPEC: RUNTIMECHECK-011
// What the precondition states of every call is not a runtime check, though a
// check selects the path the crossing is on.
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

// SPEC: RUNTIMECHECK-014
// No site of its own: it rests on the one in the body of the function it
// calls.
verified int through_call(int raw)
    ensures (result > 0)
{
    return positive_or_one(raw);
}

int main(int argc, char** argv) {
    const int raw = argc > 1 ? std::atoi(argv[1]) : 0;
    const unsigned wide = raw < 0 ? 0u : static_cast<unsigned>(raw);
    std::printf("%d %d %d %d %d %u %u %d %u %d %d %d\n", percentage_or_zero(raw), positive_or_one(raw),
                checked_result(raw), checked_argument(raw), checked_conditional(raw), last_small(wide),
                checked_index(wide), checked_member(raw), checked_elements(raw), static_under_check(raw),
                from_precondition(raw > 0 ? raw : 1), through_call(raw));
    return 0;
}
