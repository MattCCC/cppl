#pragma once

#include "cppl/clang/ast.hpp"
#include "cppl/clang/bridge.hpp"
#include "cppl/source/location.hpp"
#include "cppl/source/representation.hpp"
#include "cppl/source/storage.hpp"

#include <algorithm>
#include <clang-c/CXSourceLocation.h>
#include <clang-c/CXString.h>
#include <clang-c/Index.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The places a verified body tracks, and how an access names one: the storage
// model the Clang bridge's body lowering (lowering.hpp) shares with its lowering
// of whole struct values (aggregate_values.cpp). The functions declared at the
// end are the bridge's own, defined in bridge.cpp, so every unit reads a type,
// a place and an access exactly as the body lowering does.
namespace cppl::clangbridge::detail {

constexpr unsigned kMaxExpressionDepth = 128;
// An aggregate's members may themselves be aggregates, so one declaration can
// establish many places. Both the nesting and the total are bounded: products
// multiply, and a deeply nested array of arrays would otherwise ask for more
// versions than a proof can carry (SPEC.md 12.10).
constexpr std::size_t kMaxPlaceDepth = 8;
constexpr std::size_t kMaxTrackedLeaves = 256;

// Whether a reference type is read through to its referent.
//
// A reference is not a value: reading one is an access to another object that
// other code may write. Treating `T&` as `T` everywhere would let a contract be
// proven about a parameter whose value can change under it (AGENTS.md 11), so
// the referent is read only where the caller has established that the subject's
// logical value is the one being reasoned about.
enum class ReferenceModel : std::uint8_t {
    Opaque,
    Referent,
};

// The bridge's own functions, defined in bridge.cpp and cursors.cpp, which every
// unit lowering a body calls so it reads types, places and
// accesses exactly as the body lowering does. They stand apart from the types
// here, so a call in the body lowering to its own function of the same name
// never finds one of these by argument-dependent lookup.
namespace bridge {
// Whether two lowered terms are the same term, as an index into a place is
// compared.
bool same_term(const Expr& lhs, const Expr& rhs);
} // namespace bridge

// One tracked place: storage a verified body can read and write under logical
// versioning (SPEC.md 12.10, RFC 0014 §1).
//
// A place's identity is the declaration Clang resolved plus the path of
// projections taken into it, so `s` and `s.x` and `s.x.y` are three places of
// one object and `s.x` and `s.y` are never the same place. Shadowing needs no
// rule of its own, because an inner declaration is a different declaration.
//
// An aggregate local is tracked as one place per modeled member rather than as
// a single value, because a structural value has components instead of the one
// modeled value a version can denote.
struct Local {
    CXCursor declaration = clang_getNullCursor();
    std::uint32_t version = 0;
    Type type;
    std::optional<std::size_t> referent = std::nullopt;
    bool external = false; // may alias another reference parameter
    std::vector<PlaceStep> path;
    std::string spelling; // how this place is written, for diagnostics

    // The pointee of a pointer, rather than storage a declaration names. The
    // entry holding the pointer is what identifies it, together with the
    // version of that pointer this dereference read: `*p` before and after a
    // write to `p` are different places (RFC 0014 §1). `declaration` is the
    // pointer's declaration so lookups that key on it keep working.
    std::optional<std::size_t> pointer = std::nullopt;
    std::uint32_t pointer_version = 0;

    // Whether this entry is a symbolic element place: an element whose index is
    // a term, so which element it selects is not decided here.
    //
    // This is its own flag rather than a property read off the extent. "Is this
    // place symbolic" and "does an extent term exist for it" are different
    // questions, and once the extent is a term the second can fail
    // independently; a sentinel would make a failed extent indistinguishable
    // from an ordinary element.
    bool symbolic = false;

    // For a symbolic element place, the extent of the array it indexes. The
    // index owes `index < extent`, which is a proposition about values and so
    // is proved by the kernel rather than tracked (RFC 0014 §10, §17 step 7).
    //
    // A term, not a count: `readable(p, n)` bounds a region by a value that is
    // never a literal, and no enumeration of elements can recover it (SPEC.md
    // 12.10, STORAGE-005).
    std::vector<Expr> extent;

    // The index value, lowered where the place was formed so it denotes the
    // versions current there. A vector because `Expr` is incomplete here.
    std::vector<Expr> index_value;

    // A binder of an arm of a case split on this path, rather than storage: a
    // name for the value the arm's case exposes. It has no version, is never
    // written, and a read of it is that value (SPEC.md CASE-017).
    std::optional<CaseBinder> binder = std::nullopt;

    // Storage this body reads and never writes: a whole object a parameter
    // designates by reference. Its version follows what may have been written
    // to it -- through another reference, a member of the implicit object, a
    // call or an unsafe block -- so a read after such a write is of a value
    // nothing states rather than of the one it arrived with (SPEC.md 12.9,
    // CLASS-010). A write to it is refused, since its post-state would then
    // be a value this body cannot state member by member.
    bool read_only = false;

    // The root of a modeled sequence (RFC 0020 §3): the entry whose versions
    // carry a vector's, a string's or a span's abstract value, its length. The
    // version of a root is the storage generation of what it owns or views
    // (§4): every operation that may reallocate, shrink, replace or end that
    // storage establishes a new one, and an element write does not.
    struct Sequence {
        source::RepresentationKind kind = source::RepresentationKind::None;
        // The element type, with the refinements the declaration names. For a
        // span, that of the container it views, since the elements are that
        // container's storage.
        Type element;
        // Whether the elements are storage a caller owns: those of a container
        // bound by reference, whatever this body can prove about the object.
        bool external_elements = false;
        // For a span local, the root of the container whose storage it views.
        std::optional<std::size_t> views = std::nullopt;
        // What established the current generation, for the diagnostic that a
        // view or reference formed before it is used after it.
        std::string invalidated;
    };
    std::optional<Sequence> sequence = std::nullopt;

    // Where an entry depends on a sequence's storage generation: the root it
    // belongs to and the generation it was formed at (RFC 0020 §4).
    struct Generation {
        std::size_t root = 0;
        std::uint32_t version = 0;
    };

    // An element place of a sequence: formed at a generation, it is the place a
    // subscript names only while that generation is current. After it changes,
    // the next access forms a new place, which owes its bound again.
    std::optional<Generation> formed_at = std::nullopt;

    // A span local, or a reference bound to a sequence element: it designates
    // storage formed at a generation, and using it at any other one is using
    // storage that may no longer exist, which is refused (STDMODEL-015).
    std::optional<Generation> borrows = std::nullopt;

    [[nodiscard]] bool is_deref() const {
        return pointer.has_value();
    }

    // Distinct members of one object are distinct storage, so a write to one
    // leaves the others alone. This is the only disjointness concluded here,
    // and it comes from Clang's resolved member identity (AGENTS.md storage
    // invariants): never from a type-based aliasing argument.
    // A symbolic step records only that some index was a term, not which one, so
    // a path alone does not tell `a[i]` from `a[j]`. Such a place is recognized
    // only when the index term is supplied and is the same term: without it the
    // entry is not this place, and the access forms its own.
    bool same_place(CXCursor object, const std::vector<PlaceStep>& projection, const Expr* index_term) const {
        if (is_deref() || clang_equalCursors(declaration, object) == 0 || path != projection) {
            return false;
        }
        if (!has_symbolic_step()) {
            return true;
        }
        return index_term != nullptr && !index_value.empty() && bridge::same_term(index_value.front(), *index_term);
    }

    // Whether any step of this place's path is a symbolic element, which makes
    // the place undecided: which element it selects is not known here, so it is
    // never concluded disjoint from a sibling element.
    [[nodiscard]] bool has_symbolic_step() const {
        return std::ranges::any_of(path,
                                   [](const PlaceStep& step) { return step.kind == PlaceStep::Kind::SymbolicElement; });
    }

    // Whether a write to `other` reaches this place: `s` covers `s.x`, and
    // `s.x` covers neither `s.y` nor `s`.
    //
    // A dereference is covered only by a dereference of the same pointer
    // version. Two dereferences of *different* pointers are not concluded
    // disjoint here: that is decided by `may_alias`, which must assume they
    // overlap (RFC 0014 §4).
    [[nodiscard]] bool covered_by(const Local& other) const {
        if (is_deref() != other.is_deref()) {
            return false;
        }
        if (is_deref() && (pointer != other.pointer || pointer_version != other.pointer_version)) {
            return false;
        }
        if (!is_deref() && clang_equalCursors(declaration, other.declaration) == 0) {
            return false;
        }
        if (other.path.size() > path.size()) {
            return false;
        }
        return std::equal(other.path.begin(), other.path.end(), path.begin());
    }
};

using Locals = std::vector<Local>;

// The place an access expression names: the object it is ultimately rooted in,
// and the path of projections taken into it.
//
// This is the one resolver for every access form. `s`, `s.x`, `s.x.y`, `a[1]`
// and `a[1].x` all walk the same chain, so a member of a member is an ordinary
// place rather than a special case, and no access form gets a resolution rule
// of its own (AGENTS.md storage invariants). Resolution is by Clang's member
// identity, so any spelling of one member is one place.
//
// The chain is walked outermost-first and the path is reversed at the end,
// because `s.x.y` is a member access `y` whose object is a member access `x`.
struct ResolvedAccess {
    CXCursor object;
    std::vector<PlaceStep> path;

    // Whether the access goes through a dereference of `object`, which must
    // hold a pointer. `*p`, `p->m` and `p[i]` all resolve this way, so one
    // capability rule and one read/write path serve all three.
    bool dereferenced = false;

    // The index expressions of the symbolic element steps in `path`, in the
    // order those steps appear. Each one owes a bounds obligation against its
    // array's extent, and the obligation is a proposition about values, so it
    // is proved by the kernel rather than tracked (RFC 0014 §10).
    std::vector<CXCursor> symbolic_indices;

    // Whether the access is rooted in the implicit object of the member
    // function it stands in: `this->x`, `(*this).x`, or `x` written alone
    // (SPEC.md CLASS-008). `this` is a pointer, but it is not a pointer the
    // body dereferences: it designates the object the function was called on,
    // which is the receiver's own storage, so no capability is owed to reach it.
    bool receiver = false;

    // The declaration the place is rooted in: the one `object` names, or for
    // the implicit object the canonical declaration of its class.
    CXCursor declaration = clang_getNullCursor();
};

namespace bridge {
// What libclang reports, read one way (cursors.cpp): a string, the location a
// diagnostic names, a cursor's children, the data members of a record type in
// declaration order, whether a record type has any base subobject, and an
// expression without the parentheses and implicit nodes around it.
std::string take(CXString value);
source::SourceLocation presumed_location(CXSourceLocation location);
std::vector<CXCursor> children_of(CXCursor cursor);
std::vector<CXCursor> record_fields(CXType record);
bool record_has_base(CXType record);
CXCursor strip_parens(CXCursor cursor);

// The body lowering's own, to which bridge.cpp forwards; each is documented
// where it is defined.
Type convert_type(CXType type, unsigned depth = 0, ReferenceModel references = ReferenceModel::Opaque,
                  const std::vector<Selection::Refinement>* known = nullptr);
bool same_modeled_value(const Type& outer, const Type& inner);
std::string qualified_name_of(CXCursor cursor);
Expr unsupported_expression(CXCursor cursor, std::string reason);
source::ParameterPassing passing_of(CXType written);
CXCursor designated_object(CXCursor expression);
std::optional<ResolvedAccess> resolve_access(CXCursor cursor);
std::optional<std::size_t> find_binding(const Locals& locals, CXCursor declaration,
                                        const std::vector<PlaceStep>& path = {}, const Expr* index_term = nullptr);
std::optional<std::string> stale_borrow(const Locals& locals, std::size_t binding);
Place place_of(const Locals& locals, std::size_t entry);
Expr read_place(const Locals& locals, std::size_t entry, CXCursor at);
Place anonymous_place(std::string spelling);
} // namespace bridge

} // namespace cppl::clangbridge::detail
