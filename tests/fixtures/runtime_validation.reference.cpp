// Ordinary C++: `runtime_validation.cpp` erased by hand as SPEC.md 36.3, 36.5
// and Annex M say it erases. Each refinement is the alias of its base type and
// every contract and loop clause is gone; every ordinary check stays exactly as
// written, and every validation expression calls the validator its
// refinement lowers to beside the alias (SPEC.md ERASE-012, RUNTIMECHECK-009,
// RUNTIMECHECK-021).
#include <cstdio>
#include <cstdlib>
#include <vector>

using Percentage = int;
[[maybe_unused]] static inline bool __cppl_validate_0(int self) {
    return static_cast<bool>(self >= 0 && self <= 100);
}
using Positive = int;
[[maybe_unused]] static inline bool __cppl_validate_1(int self) {
    return static_cast<bool>(self > 0);
}
using Small = unsigned;
template <unsigned n> using Index = unsigned;

struct Reading {
    Positive level;
    int raw;
};

// SPEC: RUNTIMECHECK-002, RUNTIMECHECK-005, RUNTIMECHECK-007
// The `if` is the validation. The local enters Percentage only on the branch
// where it held, and the failure path returns a value of its own.
int percentage_or_zero(int raw) {
    if (raw >= 0 && raw <= 100) {
        Percentage p = raw;
        return p;
    }
    return 0;
}

// SPEC: RUNTIMECHECK-004, RUNTIMECHECK-007
// The failure path leaves before the crossing.
int positive_or_one(int raw) {
    if (raw <= 0) {
        return 1;
    }
    Positive p = raw;
    return p;
}

// SPEC: RUNTIMECHECK-010
// A value entering a refined result on a checked path.
Positive checked_result(int raw) {
    if (raw > 0) {
        return raw;
    }
    return 1;
}

int positive_identity(Positive p) {
    return p;
}

// SPEC: RUNTIMECHECK-010
// A value entering a callee's refined parameter on a checked path.
int checked_argument(int raw) {
    if (raw > 0) {
        return positive_identity(raw);
    }
    return 1;
}

// SPEC: RUNTIMECHECK-010
// A conditional initializer: the arm the condition selects is checked, the
// other enters statically, and the crossing is a site because one route
// needed the check.
int checked_conditional(int raw) {
    Positive p = raw > 0 ? raw : 1;
    return p;
}

// SPEC: RUNTIMECHECK-010
// A loop's condition is a runtime check like an `if`'s.
unsigned last_small(unsigned n) {
    unsigned i = 0u;
    unsigned last = 0u;
    while (i < 10u) {
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
unsigned checked_index(unsigned raw) {
    if (raw < 4u) {
        Index<4> k = raw;
        return k;
    }
    return 0u;
}

// SPEC: RUNTIMECHECK-010
// A refined member written on a checked path.
int checked_member(int raw) {
    Reading reading{1, raw};
    if (raw > 0) {
        reading.level = raw;
    }
    return reading.level;
}

// SPEC: RUNTIMECHECK-010, STDMODEL-020
// An element entering a vector local's content invariant on a checked path.
unsigned checked_elements(int raw) {
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
int static_under_check(int raw) {
    if (raw > 0) {
        Positive p = 5;
        return p;
    }
    return 5;
}

// SPEC: RUNTIMECHECK-011
// What the precondition states of every call is not a runtime check, though a
// check selects the path the crossing is on.
int from_precondition(int raw) {
    if (raw > 100) {
        return 100;
    }
    Positive p = raw;
    return p;
}

void cap(int& value) {
    if (value > 100) {
        value = 100;
    }
}

void clobber(int* target);

// SPEC: RUNTIMECHECK-004
// A write, a call's effect and an unsafe block each give the local a
// version the first check says nothing of, so the value is checked again
// before it enters. Twin of `runtime_check_stale_after_write`,
// `runtime_check_stale_after_call` and `runtime_check_stale_after_unsafe`.
int rechecked(int raw) {
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
    {
        clobber(&value);
    }
    if (value <= 0) {
        return 4;
    }
    Positive p = value;
    return p;
}

// SPEC: RUNTIMECHECK-014
// No site of its own: it rests on the one in the body of the function it
// calls.
int through_call(int raw) {
    return positive_or_one(raw);
}

void clobber(int* target) {
    *target = *target - 1;
}

int validated_percentage(int raw) {
    if (__cppl_validate_0(raw)) {
        Percentage p = raw;
        return p;
    }
    return 0;
}

int validated_or_one(int raw) {
    if (!__cppl_validate_1(raw)) {
        return 1;
    }
    Positive p = raw;
    return p;
}

int validated_below(int raw) {
    if (raw < 1000 && __cppl_validate_1(raw)) {
        Positive p = raw;
        return p;
    }
    return 1;
}

int validated_later(int raw) {
    const bool positive = __cppl_validate_1(raw);
    if (positive) {
        Positive p = raw;
        return p;
    }
    return 1;
}

int revalidated(int raw) {
    int value = raw;
    if (!__cppl_validate_1(value)) {
        return 1;
    }
    value = value - 1;
    if (!__cppl_validate_1(value)) {
        return 2;
    }
    Positive p = value;
    return p;
}

int through_validation(int raw) {
    return validated_or_one(raw);
}

int validated_halvings(int raw) {
    int x = raw;
    int steps = 0;
    while (__cppl_validate_1(x)) {
        Positive p = x;
        x = p / 2;
        if (steps < 100) {
            steps = steps + 1;
        }
    }
    return steps;
}

bool is_percentage(int value) {
    if (value >= 0 && value <= 100) {
        return true;
    }
    return false;
}

int checked_by_helper(int raw) {
    if (is_percentage(raw)) {
        Percentage p = raw;
        return p;
    }
    return 0;
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
