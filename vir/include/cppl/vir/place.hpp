#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cppl::vir {

// A place designates C++ storage (SPEC.md 12.10, RFC 0014 §1).
//
// A place is not a value: reading a place yields a value, and the place itself
// never reaches the kernel as a term. That keeps the kernel's term language
// closed and is why no address-typed term exists.
//
// A place is a root plus a path of projections into it. `s.a.b` is the local
// root `s` projected by field `a` then field `b`, so a member of a member is an
// ordinary place rather than a special case. Composing projections this way is
// what lets `Deref` and `Element` roots be added later (RFC 0014 §17 steps 5-7)
// without replacing the abstraction: a new root or a new projection kind joins
// the existing variants, and every read, write and alias rule keeps working.
//
// Identity is structural and follows Clang's resolution, never spelling: two
// spellings of one member resolve to one place, and two different members of
// one object never do. That is what makes member disjointness provable rather
// than assumed (RFC 0014 §4).

// One step into a place. Named for the step it takes rather than for the
// logical projection of `Expr`, which is a different concept: that one takes a
// component out of a value, this one names storage within storage.
struct PlaceStep {
    // A non-static data member, numbered the way the representation's
    // components are, so a component index and a field index denote the same
    // member.
    //
    // `Element` reuses this index for an array element whose index is a
    // constant. A symbolic element index needs the extent obligations of RFC
    // 0014 §7 and is refused until they exist, so it has no representation
    // here yet.
    enum class Kind : std::uint8_t { Field, Element };

    Kind kind = Kind::Field;
    std::uint32_t index = 0;

    friend bool operator==(const PlaceStep&, const PlaceStep&) = default;
};

// Which storage a place is rooted in.
//
// `Local` is a local object of the verified body. `Parameter` is the referent a
// by-reference parameter designates, which is caller storage the body does not
// own. Both are opaque identities: the bridge assigns them from Clang-resolved
// declarations and nothing here interprets them.
struct PlaceRoot {
    enum class Kind : std::uint8_t { Local, Parameter };

    Kind kind = Kind::Local;
    std::uint32_t id = 0;

    friend bool operator==(const PlaceRoot&, const PlaceRoot&) = default;
};

struct Place {
    PlaceRoot root;
    std::vector<PlaceStep> path;

    // How this place is written in source, for diagnostics only. Identity never
    // consults it.
    std::string spelling;

    // Identity is the root and the path. The spelling is not consulted: two
    // spellings of one member are one place.
    friend bool operator==(const Place& lhs, const Place& rhs) {
        return lhs.root == rhs.root && lhs.path == rhs.path;
    }
};

std::string describe(const Place& place);

} // namespace cppl::vir
