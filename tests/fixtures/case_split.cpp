#include <cstdio>
#include <optional>

// Case splits on a runtime path (SPEC.md 20.7). Each arm continues the path in
// one case of the subject's value where the split is written; nothing runs, and
// the program keeps an empty statement where each split stood. The refused
// halves of the matched pairs below are in `fixtures/negative/`, driven by
// `negative/case_splits.sh`.
enum class Mode : unsigned { idle = 0u, busy = 1u };

struct Slot {
    Mode mode;
    unsigned count;
};

proof same(unsigned x)
    proves (x == x)
{
    refl;
}

// SPEC: CASE-017, CASE-018
// A contract the split establishes and arithmetic alone does not: in each named
// case the result is a constant, and the residual case contradicts the
// precondition. Without the split the same function is refused
// (`split_needed_for_contract.cpp`).
verified unsigned settled(Mode m)
    expects (static_cast<unsigned>(m) <= 1u)
    ensures (result * result == result)
{
    cases m {
        Mode::idle => {
        }

        Mode::busy => {
        }

        omit unnamed by contradiction same(0u);
    }
    return static_cast<unsigned>(m);
}

// SPEC: CASE-020
// A split reads the version current where it is written. After the write,
// `busy` is what is known, and a second split omits `idle` on that knowledge.
// Omitting `busy` instead is refused (`split_stale_after_write.cpp`).
verified void rewritten(Mode& m)
    expects (m == Mode::idle)
{
    cases m {
        Mode::idle => {
        }

        omit Mode::busy by contradiction same(0u);

        omit unnamed by contradiction same(0u);
    }
    m = Mode::busy;
    cases m {
        Mode::busy => {
        }

        omit Mode::idle by contradiction same(0u);

        omit unnamed by contradiction same(0u);
    }
}

// SPEC: CASE-020
// Another reference is not written here, so nothing may have changed `m`. The
// same function with a write through `n` is refused
// (`split_alias_write.cpp`).
verified void unaliased(Mode& m, Mode& n)
    expects (m == Mode::idle)
{
    cases m {
        Mode::idle => {
        }

        omit Mode::busy by contradiction same(0u);

        omit unnamed by contradiction same(0u);
    }
}

// SPEC: CASE-020
// A write through a local reference is a write to what it refers to.
verified void through_reference(Mode& m)
    expects (m == Mode::idle)
{
    Mode& r = m;
    r = Mode::busy;
    cases m {
        Mode::busy => {
        }

        omit Mode::idle by contradiction same(0u);

        omit unnamed by contradiction same(0u);
    }
}

verified void make_busy(Mode& m)
    ensures (m == Mode::busy)
{
    m = Mode::busy;
}

// SPEC: CASE-020
// What is known of `m` after the call is the callee's postcondition. Omitting
// `busy` here instead is refused (`split_after_call.cpp`).
verified void after_call(Mode& m)
    expects (m == Mode::idle)
{
    make_busy(m);
    cases m {
        Mode::busy => {
        }

        omit Mode::idle by contradiction same(0u);

        omit unnamed by contradiction same(0u);
    }
}

// SPEC: CASE-017, CASE-018
// A component bound by `decompose` is the member itself, so the entry fact about
// the member rules the other cases out (`split_member_binder.cpp`).
verified unsigned member_binder(const Slot& s)
    expects (s.mode == Mode::idle)
    ensures (result == 0u)
{
    decompose s {
        components(mode, count) => {
            cases mode {
                Mode::idle => {
                }

                omit Mode::busy by contradiction same(0u);

                omit unnamed by contradiction same(0u);
            }
        }
    }
    return static_cast<unsigned>(s.mode);
}

// SPEC: CASE-020
// A member of a local aggregate is tracked on its own: the write to it is seen
// by the next split of it (`split_member_write.cpp`).
verified unsigned member_write(unsigned x)
    ensures (result == x)
{
    Slot s{Mode::idle, x};
    cases s.mode {
        Mode::idle => {
        }

        omit Mode::busy by contradiction same(0u);

        omit unnamed by contradiction same(0u);
    }
    s.mode = Mode::busy;
    cases s.mode {
        Mode::busy => {
        }

        omit Mode::idle by contradiction same(0u);

        omit unnamed by contradiction same(0u);
    }
    return s.count;
}

// SPEC: CASE-020
// Constant subscripts name their own elements. A write at a subscript that is a
// term may be any element, so after it nothing is known of `modes[0]`, and every
// case of it has an arm (`split_symbolic_element.cpp`).
verified unsigned elements(unsigned i)
    expects (i < 2u)
    ensures (result == 1u)
{
    Mode modes[2] = {Mode::idle, Mode::idle};
    modes[1] = Mode::busy;
    cases modes[0] {
        Mode::idle => {
        }

        omit Mode::busy by contradiction same(0u);

        omit unnamed by contradiction same(0u);
    }
    cases modes[1] {
        Mode::busy => {
        }

        omit Mode::idle by contradiction same(0u);

        omit unnamed by contradiction same(0u);
    }
    modes[i] = Mode::busy;
    cases modes[0] {
        Mode::idle => {
        }

        Mode::busy => {
        }

        unnamed(value) => {
        }
    }
    return 1u;
}

// SPEC: CASE-020
// A split inside a loop reads the version current in that iteration. `m` is not
// written in the loop, so the entry fact stands at every iteration. Were it
// written there, the head would know nothing of it (`split_loop_carried.cpp`).
verified unsigned iterated(Mode m, unsigned n)
    expects (m == Mode::idle)
    ensures (result == n)
{
    unsigned i = 0u;
    while (i < n)
        invariant (i <= n)
    {
        cases m {
            Mode::idle => {
            }

            omit Mode::busy by contradiction same(0u);

            omit unnamed by contradiction same(0u);
        }
        i = i + 1u;
    }
    return i;
}

// SPEC: CASE-017, CASE-018
// A payload bound by `some` splits again, generically, and a pointer's null
// case is ruled out by the entry fact about it.
verified unsigned nested(std::optional<Mode> o, const int* p)
    expects (p != nullptr)
    ensures (result == 1u)
{
    cases o {
        some(inner) => {
            cases inner {
                Mode::idle => {
                }

                Mode::busy => {
                }

                unnamed(value) => {
                }
            }
        }

        none => {
        }
    }
    cases p {
        omit null by contradiction same(0u);

        non_null => {
        }
    }
    return 1u;
}

// SPEC: CASE-019
// A claim in an arm ends that arm's path, and its arguments may name the arm's
// binders: `count` is the member it binds. The branch it stands on contradicts
// the precondition on that member.
verified unsigned binder_in_claim(const Slot& s)
    expects (s.count == 1u)
    ensures (result == 1u)
{
    if (s.count == 2u) {
        decompose s {
            components(mode, count) => {
                contradiction same(count);
            }
        }
    }
    return s.count;
}

// SPEC: CASE-017, ERASE-016
// A split as the body of an unbraced `if` leaves an empty statement there, so
// the `return` after it runs on both paths.
verified unsigned unbraced(Mode m, bool flag)
    ensures (result == 1u)
{
    if (flag)
        cases m {
            Mode::idle => {
            }

            Mode::busy => {
            }

            unnamed(value) => {
            }
        }
    return 1u;
}

// SPEC: CASE-017
// Each specialization of a template splits its own subject.
template <typename T>
verified unsigned generic(T m)
    ensures (result == 1u)
{
    cases m {
        T::idle => {
        }

        T::busy => {
        }

        unnamed(value) => {
        }
    }
    return 1u;
}

// None of the splits runs: the program is the one written without them.
int main() {
    Mode m = Mode::idle;
    rewritten(m);
    Mode n = Mode::idle;
    Mode o = Mode::busy;
    unaliased(n, o);
    Mode r = Mode::idle;
    through_reference(r);
    Mode c = Mode::idle;
    after_call(c);
    const int x = 4;
    std::printf("%u %u %u %u %u %u %u %u %u %u %u %u\n", settled(Mode::busy), static_cast<unsigned>(m),
                static_cast<unsigned>(r), static_cast<unsigned>(c), member_binder(Slot{Mode::idle, 3u}),
                member_write(5u), elements(1u), iterated(Mode::idle, 3u), nested(Mode::busy, &x),
                binder_in_claim(Slot{Mode::busy, 1u}), unbraced(Mode::busy, false), generic(Mode::idle));
}
