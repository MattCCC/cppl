// Induction erases completely (SPEC.md INDUCT-005, ERASE-002): the laws, the
// proofs and every `induction` statement in them, both forms, nested and with
// premises named in the arms, leave the program. What stays is the functions,
// exactly as written, and `induction.reference.cpp` is this program erased by
// hand.
#include <cstdio>

pure unsigned add(unsigned x, unsigned y) {
    return x + y;
}

pure unsigned scaled_square(unsigned k, unsigned m) {
    return k * m * m;
}

law add_zero(unsigned x)
    proves (add(x, 0u) == x);

proof add_zero_by_induction(unsigned x)
    proves (add_zero(x))
{
    induction x;
}

law constant_from_steps(unsigned k, unsigned n)
    expects (forall (unsigned m) { scaled_square(k, m + 1u) == scaled_square(k, m) })
    proves (scaled_square(k, n) == scaled_square(k, 0u));

proof constant_from_steps_by_induction(unsigned k, unsigned n)
    proves (constant_from_steps(k, n))
{
    induction n {
        zero => {
            refl;
        }

        successor(pred) => {
            assume steps : forall (unsigned m) { scaled_square(k, m + 1u) == scaled_square(k, m) };
            assume hypothesis : (forall (unsigned m) { scaled_square(k, m + 1u) == scaled_square(k, m) }) ->
            scaled_square(k, pred) == scaled_square(k, 0u);
            rewrite steps(pred);
            apply hypothesis;
            exact steps;
        }
    }
}

law add_commutes(unsigned a, unsigned b)
    proves (add(a, b) == add(b, a));

proof add_commutes_by_induction(unsigned a, unsigned b)
    proves (add_commutes(a, b))
{
    induction a {
        zero => {
            induction b;
        }

        successor(p) => {
            induction b {
                zero => {
                    refl;
                }

                successor(q) => {
                    assume below : q < 4294967295u;
                    refl;
                }
            }
        }
    }
}

// A runtime loop beside the proofs: the program's own iteration is untouched by
// the proof-only induction over the same kind of value.
unsigned sum_below(unsigned n) {
    unsigned total = 0u;
    for (unsigned i = 0u; i < n; ++i) {
        total = add(total, i);
    }
    return total;
}

int main() {
    std::printf("%u %u %u\n", add(5u, 0u), scaled_square(3u, 2u), sum_below(10u));
}
