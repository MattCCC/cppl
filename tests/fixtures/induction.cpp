// Induction over unsigned machine integers (GRAMMAR.md 5.8, SPEC.md 21,
// INDUCT-001 to INDUCT-005).
//
// `induction x { zero => ... successor(pred) => ... }` proves a claim about
// every value of an unsigned parameter from the claim at 0 and from the claim
// at `pred + 1`, where the arm supposes `pred < max` and the claim at `pred`.
// The kernel states both cases itself and checks them with one rule
// (FOUNDATIONS.md 74); nothing here adds an assumption, and every proof below
// has a refused twin in `fixtures/negative/induction_*.cpp` or
// `negative/induction.sh` that removes one premise it needs.
#include <cstdio>

pure unsigned add(unsigned x, unsigned y) {
    return x + y;
}

// A product the core states but no linear reasoning decides.
pure unsigned scaled_square(unsigned k, unsigned m) {
    return k * m * m;
}

// The short form leaves each case to automation, which must still produce
// evidence the kernel checks for both (INDUCT-005).
law add_zero(unsigned x)
    proves (add(x, 0u) == x);

proof add_zero_by_induction(unsigned x)
    proves (add_zero(x))
{
    induction x;
}

// The explicit form. Each arm proves the goal for its own case, and `assume`
// names the premises the successor case supplies: the range premise, which is
// why the step never relies on `pred + 1` wrapping, and the hypothesis.
law zero_add(unsigned x)
    proves (add(0u, x) == x);

proof zero_add_by_induction(unsigned x)
    proves (zero_add(x))
{
    induction x {
        zero => {
            refl;
        }

        successor(pred) => {
            assume below : pred < 4294967295u;
            assume hypothesis : add(0u, pred) == pred;
            refl;
        }
    }
}

// A claim the hypothesis is needed for. The premise says only that each value
// agrees with the next; that the value at `n` agrees with the value at 0 follows
// by induction, rewriting with the premise at the predecessor and closing with
// the hypothesis. The refused twin, `induction_without_hypothesis.cpp`, closes
// the successor case without the hypothesis.
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

// The subject need not be the last parameter. Parameters after it stay
// quantified inside each case, so the hypothesis holds for every one of them;
// in the zero arm the subject's quantifier is gone and `k` is still `k`.
law scaled_square_at_zero(unsigned n, unsigned k)
    proves (scaled_square(k, n) == k * n * n);

proof scaled_square_by_induction(unsigned n, unsigned k)
    proves (scaled_square_at_zero(n, k))
{
    induction n {
        zero => {
            refl;
        }

        successor(pred) => {
            assume hypothesis : forall (unsigned j) { scaled_square(j, pred) == j * pred * pred };
            refl;
        }
    }
}

// Nested induction: each arm may itself be proved by induction over another
// parameter, whose quantifier still stands in that arm's goal.
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

// A Law's premise is part of what is proved at each value, so each case
// supposes it at its own value.
law bounded_sum(unsigned n)
    expects (n < 100u)
    proves (add(n, n) < 200u);

proof bounded_sum_by_induction(unsigned n)
    proves (bounded_sum(n))
{
    induction n;
}

// A proof of a proposition stated directly, rather than of a Law, is proved the
// same way.
proof sixty_four_bit(unsigned long long v)
    proves (add(0u, 1u) + v - v == 1u)
{
    induction v;
}

int main() {
    std::printf("%u %u %u %u\n", add(2u, 0u), add(0u, 3u), scaled_square(2u, 3u), add(40u, 2u));
}
