#!/usr/bin/env bash
# Whole struct values that must be refused.
#
# SPEC: STORAGE-007, STORAGE-008, CLASS-011, REFINE-010
# TRUST.md TCB-AGGREGATE-001, TCB-AGGREGATE-002, TCB-UNSAFE-004
#
# A struct a body tracks is one place per scalar leaf. A value of it that flows
# out is assembled from those places, and only the member values it was
# assembled from are supposed of it; a value that flows in is taken member by
# member, each at its member's declared type. A false claim through any of these
# flows is refused, as is a member a call may have written that the claim keeps,
# a refined member no proof establishes, and every copy that runs code of the
# program. The accepted twins at the end show the refusals are about those, not
# about structs.
set -euo pipefail
CPPL="$1"
WORK="$3"
mkdir -p "$WORK"
run=$(mktemp -d "$WORK/struct-values-negative.XXXXXX")

fail() {
    echo "$1" >&2
    exit 1
}

# refuse <name> <diagnostic>: the program is refused for that reason, produces
# no program, and nothing is reported proven.
refuse() {
    local name="$1" diagnostic="$2"
    { printf '%s\n' "$common"; cat; echo 'int main() { return 0; }'; } > "$run/$name.cpp"
    if "$CPPL" -std=c++20 "$run/$name.cpp" -o "$run/$name" --cppl-trust-report > "$run/$name.log" 2>&1; then
        cat "$run/$name.log" >&2
        fail "accepted what must be refused: $name"
    fi
    [ ! -e "$run/$name" ] || fail "$name produced a program"
    if grep -q 'C++L Trust Report' "$run/$name.log"; then
        cat "$run/$name.log" >&2
        fail "$name was reported"
    fi
    grep -qF -- "$diagnostic" "$run/$name.log" || {
        cat "$run/$name.log" >&2
        fail "$name was not refused for the stated reason: $diagnostic"
    }
    # Refused in the case itself: an error in the shared declarations would
    # refuse every case for a reason none of them states.
    if ! awk -v file="$run/$name.cpp:" -v shared="$shared_lines" '
        index($0, file) == 1 && index($0, ": error") > 0 {
            split(substr($0, length(file) + 1), position, ":")
            if (position[1] + 0 <= shared) refused_early = 1
        }
        END { exit refused_early }' "$run/$name.log"; then
        cat "$run/$name.log" >&2
        fail "$name was refused in the declarations every case shares"
    fi
}

# accept <name>: the program verifies with nothing unresolved.
accept() {
    local name="$1"
    { printf '%s\n' "$common"; cat; echo 'int main() { return 0; }'; } > "$run/$name.cpp"
    if ! "$CPPL" -std=c++20 "$run/$name.cpp" -o "$run/$name" --cppl-trust-report > "$run/$name.log" 2>&1; then
        cat "$run/$name.log" >&2
        fail "refused what should verify: $name"
    fi
    grep -Eq '^Unresolved obligations: +0$' "$run/$name.log" || fail "$name left an obligation unresolved"
}

common='type Percent = unsigned where (self <= 100u);
struct Config { unsigned limit; unsigned step; };
struct Outer { Config inner; unsigned tag; };
struct Buffer { unsigned items[3]; unsigned count; };
struct Gauge { Percent level; unsigned id; };
verified unsigned peek(const Config& c) ensures (result == c.limit) { return c.limit; }
verified unsigned by_value(Config c) ensures (result == c.step) { return c.step; }
verified unsigned level_of(Gauge g) ensures (result <= 100u) { return g.level; }
verified void advance(const Config& c, unsigned& s) expects (s < 1000u && c.step < 1000u) ensures (true) {
    s = s + c.step;
}
verified unsigned look(Config& c) ensures (result == c.limit) { return c.limit; }
verified unsigned touch(const Config& c) ensures (true) {
    unsafe { const_cast<Config&>(c).limit = 9u; }
    return 0u;
}'

shared_lines=$(printf '%s\n' "$common" | wc -l)
contract="does not satisfy its contract"

# --- A false claim through each flow -----------------------------------------

refuse copied_from_a_parameter "$contract" <<'CPP'
verified unsigned f(const Config& c) ensures (result == c.step) { Config d = c; return d.limit; }
CPP

refuse returned_local "$contract" <<'CPP'
verified Config f(unsigned l) ensures (result.step == 2u) { Config c{l, 1u}; return c; }
CPP

refuse handed_by_reference "$contract" <<'CPP'
verified unsigned f() ensures (result == 101u) { Config c{100u, 5u}; return peek(c); }
CPP

refuse handed_by_value "$contract" <<'CPP'
verified unsigned f() ensures (result == 100u) { Config c{100u, 5u}; return by_value(c); }
CPP

refuse initialized_from_a_call "$contract" <<'CPP'
verified Config make(unsigned l) ensures (result.limit == l && result.step == 1u) { Config c{l, 1u}; return c; }
verified unsigned f() ensures (result == 8u) { Config d = make(7u); return d.limit; }
CPP

# Each member of an assembled value is the member at its own position.
refuse members_kept_in_order "$contract" <<'CPP'
verified Config f(const Config& c) ensures (result.limit == c.step) { Config d = c; return d; }
CPP

refuse nested_member "$contract" <<'CPP'
verified unsigned f(const Outer& o) ensures (result == o.tag) { Config d = o.inner; return d.step; }
CPP

refuse array_member "$contract" <<'CPP'
verified unsigned f() ensures (result == 5u) { Buffer b{{5u, 6u, 7u}, 3u}; Buffer c = b; return c.items[1]; }
CPP

refuse assigned_whole "$contract" <<'CPP'
verified unsigned f(const Config& c) ensures (result == c.step) { Config d{0u, 0u}; d = c; return d.limit; }
CPP

refuse returned_and_passed_on "$contract" <<'CPP'
verified Config make(unsigned l) ensures (result.limit == l && result.step == 1u) { Config c{l, 1u}; return c; }
verified Config pass_through(const Config& c) ensures (result.limit == c.limit && result.step == c.step) { return c; }
verified unsigned f() ensures (result == 7u) { return peek(pass_through(make(6u))); }
CPP

refuse member_of_the_implicit_object "$contract" <<'CPP'
struct Holder {
    Config cfg;
    verified Config config() const ensures (result.limit == cfg.step) { return cfg; }
};
CPP

refuse by_value_parameter_written "$contract" <<'CPP'
verified unsigned f(Config c) ensures (result == 6u) { c.limit = 5u; return peek(c); }
CPP

# --- What a call may have written is not kept --------------------------------

# A member handed by mutable reference beside its own struct is what the callee
# left there, not what it was.
refuse aliased_member_not_kept "$contract" <<'CPP'
verified unsigned f() ensures (result == 5u) { Config c{100u, 5u}; advance(c, c.step); return c.step; }
CPP

# A struct a callee may write is what its contract states afterwards, and no
# more: this one states nothing of its members' preservation.
refuse written_struct_not_kept "$contract" <<'CPP'
verified unsigned f() ensures (result == 11u) { Config c{11u, 2u}; unsigned seen = look(c); return c.limit; }
CPP

# Each member of a written struct is afterwards its own member of the value the
# callee's contract states, not another one.
refuse members_rebound_in_order "$contract" <<'CPP'
verified void hold(Config& c) expects (c.limit == 1u && c.step == 2u) ensures (c.limit == 1u && c.step == 2u) {}
verified unsigned f() ensures (result == 2u) { Config c{1u, 2u}; hold(c); return c.limit; }
CPP

# A callee whose unsafe code may write what it is handed writes every member of
# a struct it gets by `const` reference.
refuse unsafe_callee_struct_not_kept "$contract" <<'CPP'
verified unsigned f() ensures (result == 100u) { Config c{100u, 5u}; unsigned t = touch(c); return c.limit; }
CPP

refuse unsafe_callee_nested_member_not_kept "$contract" <<'CPP'
verified unsigned f() ensures (result == 2u) {
    Outer o{{1u, 2u}, 3u};
    unsigned t = touch(o.inner);
    return o.inner.step;
}
CPP

# Every member a call may write must be followed, and a member of a by-value
# parameter this body never writes is not: it would keep the value it arrived
# with.
refuse untracked_member_of_a_written_struct "would keep reading the value it held before the call" <<'CPP'
verified unsigned f(Config c) ensures (result == c.step) { c.limit = 5u; unsigned t = touch(c); return c.step; }
CPP

# A write to any member of a struct reaches whatever may be that member: here a
# reference parameter, which may designate a member of the implicit object.
refuse reference_parameter_beside_a_written_member "$contract" <<'CPP'
struct Holder {
    Config cfg;
    verified unsigned m(unsigned& x) expects (x < 10u) ensures (result == 0u) {
        unsigned before = x;
        unsigned seen = look(cfg);
        return x - before;
    }
};
CPP

# --- A refined member no proof establishes ------------------------------------

refuse refined_member_violated_through_a_copy "is not shown to satisfy refinement type 'Percent'" <<'CPP'
verified unsigned f() ensures (true) { Gauge g{50u, 1u}; unsafe { g.level = 300u; } Gauge h = g; return h.level; }
CPP

refuse refined_member_violated_through_an_assignment "is not shown to satisfy refinement type 'Percent'" <<'CPP'
verified unsigned f() ensures (true) {
    Gauge g{50u, 1u};
    Gauge h{10u, 2u};
    unsafe { g.level = 300u; }
    h = g;
    return h.level;
}
CPP

refuse refined_member_owed_where_passed "call-site precondition for 'f -> level_of' is not proven" <<'CPP'
verified unsigned f() ensures (true) { Gauge g{50u, 1u}; unsafe { g.level = 300u; } return level_of(g); }
CPP

refuse refined_member_owed_where_returned "$contract" <<'CPP'
verified Gauge f() ensures (true) { Gauge g{50u, 1u}; unsafe { g.level = 300u; } return g; }
CPP

refuse refined_member_of_a_constructed_result "is not shown to satisfy refinement type 'Percent'" <<'CPP'
verified Gauge f(unsigned v) ensures (true) { Gauge g{v, 1u}; return g; }
CPP

# --- A copy that runs code of the program -------------------------------------

refuse user_provided_copy_constructor "the copy constructor of 'Meter' is user-provided" <<'CPP'
struct Meter {
    unsigned reading;
    Meter(unsigned value) : reading(value) {}
    Meter(const Meter& other) : reading(other.reading + 1u) {}
};
verified unsigned read(Meter m) ensures (result == m.reading) { return m.reading; }
verified unsigned f(const Meter& m) ensures (result == m.reading) { return read(m); }
CPP

# The copy C++ defines for a struct copies each member with that member's own
# copy constructor, which runs the program's code where it provides one.
refuse user_provided_copy_constructor_of_a_member "the copy constructor of 'Meter' is user-provided" <<'CPP'
struct Meter {
    unsigned reading;
    Meter(unsigned value) : reading(value) {}
    Meter(const Meter& other) : reading(other.reading + 1u) {}
};
struct Panel { Meter meter; unsigned id; };
verified unsigned read(Panel p) ensures (result == p.meter.reading) { return p.meter.reading; }
verified unsigned f(const Panel& p) ensures (result == p.meter.reading) { return read(p); }
CPP

refuse user_provided_move_constructor "the move constructor of 'Mover' is user-provided" <<'CPP'
struct Mover {
    unsigned value;
    Mover(unsigned v) : value(v) {}
    Mover(const Mover&) = default;
    Mover(Mover&& other) : value(other.value + 1u) {}
};
verified unsigned f(Mover m) ensures (result == m.value) { Mover d = static_cast<Mover&&>(m); return d.value; }
CPP

refuse user_provided_copy_assignment "the copy assignment operator of 'Pair' is user-provided" <<'CPP'
struct Pair {
    unsigned first;
    unsigned second;
    Pair& operator=(const Pair& other) { first = other.second; second = other.first; return *this; }
};
verified unsigned f(const Pair& p) ensures (result == p.first) { Pair q{0u, 0u}; q = p; return q.first; }
CPP

# --- What is not modeled, named --------------------------------------------------

refuse constructed_by_a_constructor_of_its_own "constructing 'Made' runs a constructor that is not modeled" <<'CPP'
struct Made {
    unsigned v;
    Made(unsigned x) : v(x + 1u) {}
};
verified unsigned get(Made m) ensures (result == m.v) { return m.v; }
verified unsigned f() ensures (result == 3u) { return get(3u); }
CPP

refuse member_of_an_unmodeled_type "component 'scale' has an unmodeled type 'double'" <<'CPP'
struct Reading { unsigned raw; double scale; };
verified Reading make_reading() ensures (true) { Reading r{1u, 2.0}; return r; }
verified unsigned f() ensures (true) { Reading r = make_reading(); return r.raw; }
CPP

refuse conditional_between_two_structs "from a conditional expression choosing between two values of type 'Config'" <<'CPP'
verified unsigned f(const Config& a, const Config& b, bool first) ensures (true) {
    Config d = first ? a : b;
    return d.limit;
}
CPP

# --- The twins: a struct whose copies C++ defines, proven ----------------------

accept defaulted_copy_constructor <<'CPP'
struct Meter {
    unsigned reading;
    Meter(const Meter&) = default;
};
verified unsigned read(Meter m) ensures (result == m.reading) { return m.reading; }
verified unsigned f(const Meter& m) ensures (result == m.reading) { return read(m); }
CPP

accept refined_member_kept_through_a_copy <<'CPP'
verified unsigned f() ensures (result <= 100u) { Gauge g{50u, 1u}; Gauge h = g; return level_of(h); }
CPP

accept members_rebound_to_what_the_callee_states <<'CPP'
verified void hold(Config& c) expects (c.limit == 1u && c.step == 2u) ensures (c.limit == 1u && c.step == 2u) {}
verified unsigned f() ensures (result == 2u) { Config c{1u, 2u}; hold(c); return c.step; }
CPP

accept sibling_of_an_aliased_member_kept <<'CPP'
verified unsigned f() ensures (result == 100u) { Config c{100u, 5u}; advance(c, c.step); return c.limit; }
CPP

echo 'every false claim, unkept member, unestablished refinement and copy running program code is refused'
