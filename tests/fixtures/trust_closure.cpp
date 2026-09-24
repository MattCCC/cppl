// Trust propagation and per-claim assumption closure (SPEC.md TRUSTED-002,
// PROOFSRC-005, STATUS-002; TRUST.md 35, 36).
//
// A trusted law is applied like a proven law, and whatever is derived from it
// is PROVEN relative to it: the trust report names every trusted law each
// proven claim rests on, directly or through the proofs it uses, and every
// trusted law nothing rests on. `tests/e2e/trust_closure.sh` checks the whole
// report against what is written here.
#include <cstdio>

enum class State : int { idle = -1, running = 3 };

pure unsigned zero() {
    return 0u;
}

// --- The assumptions ---------------------------------------------------------

// Used by several claims, directly and through chains of proofs.
trusted law sensor_identity(unsigned x)
    proves (x + zero() == x);

// Its premise is not assumed: whoever applies it still owes `x == 3u`.
trusted law device_bound(unsigned x)
    expects (x == 3u)
    proves (x + 1u == 4u);

// False. Everything derived from it is proven only relative to it, and the
// report says so for each of them (AGENTS.md 8).
trusted law broken_counter()
    proves (zero() == 1u);

// Used by nothing, and listed as such so an audit can remove it.
trusted law never_used(unsigned x)
    proves (x * 1u == x);

// A memory proposition, admitted as an explicit assumption (SPEC.md
// TRUSTED-003). It is not a proposition any proof goal can be, so no statement
// can use it: it is TRUSTED, listed with what it admits, and unused.
trusted law device_window(unsigned* registers, unsigned count)
    expects (count <= 64u)
    proves (readable(registers, count));

// --- Direct trust ------------------------------------------------------------

law identity_holds(unsigned x)
    proves (x + zero() == x)
{
    exact sensor_identity(x);
}

// --- Transitive trust: a chain in which only the first link names the law ----

proof first_link(unsigned y)
    proves (y + zero() == y)
{
    exact sensor_identity(y);
}

proof second_link(unsigned y)
    proves (y + zero() == y)
{
    exact first_link(y);
}

proof third_link(unsigned y)
    proves (y + zero() == y)
{
    exact second_link(y);
}

// --- More than one assumption, and a premise that is still owed -------------

law bound_after_identity(unsigned x)
    expects (x == 3u)
    proves (x + zero() + 1u == 4u)
{
    assume is_three : x == 3u;
    rewrite sensor_identity(x);
    apply device_bound(x);
    exact is_three;
}

// --- A chain that joins an assumption-free proof and a trust-dependent one ---

proof outright(unsigned y)
    proves (y + 0u == y)
{
    refl;
}

proof mixed(unsigned y)
    proves (y + 0u + zero() == y)
{
    rewrite outright(y);
    exact third_link(y);
}

// --- Through another law, and the same law reached more than once ----------

// `identity_holds` is a law whose own proof names the assumption; using the
// law carries it here.
proof through_a_law(unsigned y)
    proves (y + zero() == y)
{
    exact identity_holds(y);
}

// Named twice: one dependency, not two.
proof named_twice(unsigned y)
    proves (y + zero() + zero() == y)
{
    rewrite sensor_identity(y + zero());
    exact sensor_identity(y);
}

// Reached through `first_link` and named directly: one dependency, direct.
proof direct_and_through(unsigned y)
    proves (y + zero() + zero() == y)
{
    rewrite first_link(y + zero());
    exact sensor_identity(y);
}

// --- A false assumption proves anything, relative to it ---------------------

law anything_goes(unsigned x)
    proves (x == 5u)
{
    contradiction broken_counter;
}

// --- Omitted cases rest on the assumptions of the proof they are written in -

law omission_relies_on_assumption(State s)
    proves (Eq<State>(s, s))
{
    cases s {
        State::idle => {
            refl;
        }

        omit State::running by contradiction broken_counter;

        omit unnamed by contradiction broken_counter;
    }
}

// The accepted half of a matched pair. Its twin,
// `negative/trusted_law_only_where_named.cpp`, differs only in naming `truth`
// in the `State::running` arm, where `broken_counter` is not a standing premise
// and so contradicts nothing.
law only_where_named(State s)
    expects (zero() == 0u)
    proves (Eq<unsigned>(zero(), 1u))
{
    assume truth : zero() == 0u;
    cases s {
        State::idle => {
            exact broken_counter;
        }

        State::running => {
            contradiction broken_counter;
        }

        omit unnamed by contradiction broken_counter;
    }
}

// --- A proof of one instance of a law is a claim of its own -----------------

proof identity_at_three()
    proves (identity_holds(3u))
{
    exact sensor_identity(3u);
}

// --- Trust reaches a verified body, and every function that calls it --------

proof counter_is_one()
    proves (zero() == 1u)
{
    exact broken_counter;
}

// `x == 7u` is consistent with the precondition, so only the false assumption
// rules that path out. The claim that it cannot occur rests on it, and so does
// the contract, which holds only because the path is excluded.
verified unsigned never_seven(unsigned x)
    expects (x < 10u)
    ensures (result != 7u)
{
    if (x == 7u) {
        contradiction counter_is_one;
    }
    return x;
}

// Proven through a verified call, so it rests on what that call rests on
// although nothing here names a trusted law.
verified unsigned calls_never_seven(unsigned x)
    expects (x < 10u)
    ensures (result != 7u)
{
    return never_seven(x);
}

// --- Claims that use no assumption rest on none -----------------------------

law plain(unsigned x)
    proves (x + 0u == x);

verified unsigned add_zero(unsigned x)
    ensures (result == x)
{
    return x + zero();
}

// None of the above may reach the runtime.
int main() {
    std::printf("%u %u\n", add_zero(41u) + 1u, calls_never_seven(3u));
}
