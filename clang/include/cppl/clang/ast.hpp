#pragma once

#include "cppl/source/location.hpp"
#include "cppl/source/representation.hpp"
#include "cppl/source/storage.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace cppl::clangbridge {

// A narrow, typed mirror of the semantic facts C++L needs from Clang.
//
// Everything here is a *resolved* fact: types are canonical, calls name the
// overload Clang selected, and references name the declaration Clang bound.
// Nothing in this layer is recovered from source text, and no Clang data
// structure or pointer escapes it (ARCHITECTURE.md 12).

enum class TypeKind : std::uint8_t {
    Int,
    Bool,
    Proposition,
    Value,
    Void,
    Unsupported,
};

// A refinement the written type named, with the values its indices were applied
// at (SPEC.md 17, 18). Clang canonicalizes a refinement to its base type, which
// is exactly right for the runtime program and wrong for verification, so the
// name the author wrote is recorded here beside the canonical facts.
struct Refinement {
    std::string name;
    std::vector<std::int64_t> arguments;
    std::string identity = {};

    friend bool operator==(const Refinement&, const Refinement&) = default;
};

// One enumerator, as Clang resolved it. Enumerators sharing a value are aliases
// naming one logical case; all are kept so a diagnostic can name the one an
// author wrote.
struct Enumerator {
    std::string name;
    std::int64_t value = 0;

    friend bool operator==(const Enumerator&, const Enumerator&) = default;
};

// The resolved identity of a C++ representation whose proof-visible states a
// decomposition provider may model (SPEC.md 20.5).
//
// `identity` is the declaration's USR, so a provider selects on resolved
// semantic identity rather than on a spelling: aliases, qualified names and
// template specializations that resolve to one declaration share it.
struct Representation {
    std::string identity = {};
    std::string name; // qualified name, for diagnostics only
    std::vector<Enumerator> enumerators;
    source::RepresentationKind kind = source::RepresentationKind::None;
    std::vector<source::Component> components;
    std::string rejection;

    friend bool operator==(const Representation& lhs, const Representation& rhs) {
        return lhs.identity == rhs.identity;
    }
};

struct Type {
    TypeKind kind = TypeKind::Unsupported;
    std::uint16_t width = 0; // value bits, for Int
    bool is_signed = true;
    std::string spelling;

    // Outermost refinement first. Empty for an ordinary C++ type.
    std::vector<Refinement> refinements;

    // A scoped enum has exactly its fixed underlying type's value set, so the
    // representation records what the named states are without narrowing the
    // value set to them.
    Representation representation;
    std::vector<Type> projections;

    friend bool operator==(const Type&, const Type&) = default;
};

enum class BinaryOp : std::uint8_t {
    Add,
    Sub,
    Mul,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    And, // C++ `&&` between Boolean operands
    Or,  // C++ `||` between Boolean operands
    Unsupported,
};

struct Expr;

struct ParameterRef {
    std::uint32_t index = 0;
    std::string name;
};

struct IntLiteral {
    std::int64_t value = 0;
};

struct CallEffect {
    std::uint32_t argument = 0;
    std::uint32_t version = 0;
    Type declared;
};

struct Call {
    std::string callee_usr;
    std::string callee_name;
    std::vector<Expr> arguments;
    std::vector<CallEffect> effects = {};
};

struct Binary {
    BinaryOp op = BinaryOp::Unsupported;
    std::vector<Expr> operands;
};

struct Negation {
    std::vector<Expr> operands;
};
struct Projection {
    std::uint32_t index = 0;
    std::vector<Expr> operands; // one subject, signature supplied by its type
};

// One element of an array value, selected at a term rather than a constant
// (FOUNDATIONS.md 45). Forming it proves nothing about the index: the
// `index < extent` obligation is owed separately, by the same `ElementBound` a
// tracked subscript owes (SPEC.md STORAGE-011, STORAGE-012).
struct Element {
    std::vector<Expr> operands; // subject, index
};

struct FormalEquality {
    Type operand_type;
    std::vector<Expr> operands;
};
struct Universal {
    std::vector<Type> binders;
    std::vector<Expr> body;
};
struct Implication {
    std::vector<Expr> operands;
};
struct Connective {
    enum class Kind : std::uint8_t { Conjunction, Disjunction, Equivalence };
    Kind kind = Kind::Conjunction;
    std::vector<Expr> operands;
};
struct Conditional {
    std::vector<Expr> operands;
}; // condition, true return, false return

// One step into a place: a data member, or an array element. Numbered the way
// the representation's components are, so a component index and a field index
// denote the same member.
//
// `SymbolicElement` carries `symbol` instead of `index`: an element whose index
// is a term rather than a constant. Two symbolic elements are disjoint only
// when their index terms are proved unequal, never merely because the symbols
// differ (RFC 0014 §4).
struct PlaceStep {
    enum class Kind : std::uint8_t { Field, Element, SymbolicElement };

    Kind kind = Kind::Field;
    std::uint32_t index = 0;
    std::uint32_t symbol = 0;

    friend bool operator==(const PlaceStep&, const PlaceStep&) = default;
};

// Which storage a place is rooted in: a local of this body, the referent a
// by-reference parameter designates, or the pointee a pointer designates.
//
// `Deref` is the one root that crosses from a value to a place, and the only
// one requiring a capability to form (RFC 0014 §1). It is identified by the
// pointer's place and the version whose value it dereferences, so `*p` before
// and after a write to `p` are different places.
struct PlaceRoot {
    enum class Kind : std::uint8_t { Local, Parameter, Deref };

    Kind kind = Kind::Local;
    std::uint32_t id = 0;
    std::uint32_t version = 0;

    friend bool operator==(const PlaceRoot&, const PlaceRoot&) = default;
};

// A place designating C++ storage (SPEC.md 12.10, RFC 0014 §1): a root plus a
// path of projections into it, so `s.a.b` is an ordinary place rather than a
// special case. Identity is structural and comes from Clang's resolution,
// never from the spelling, which is carried for diagnostics only.
struct Place {
    PlaceRoot root;
    std::vector<PlaceStep> path;
    std::string spelling;

    friend bool operator==(const Place& lhs, const Place& rhs) {
        return lhs.root == rhs.root && lhs.path == rhs.path;
    }
};

// A subscript index that must lie within its array's extent (SPEC.md 12.10).
// Both sides are values, so the kernel proves it; only the capability part of
// an access is tracked contextually (RFC 0014 §10).
//
// The extent is a term rather than a count. A constant extent canonicalizes to
// a literal, but a dependent one -- `T(&)[N]` under a template, or the `n` of
// `readable(p, n)` -- denotes a value no integer is available for until the
// specialization exists, and enumerating elements to recover it is exactly what
// a symbolic extent cannot do (SPEC.md STORAGE-005, TEMPLATE-001).
struct ElementBound {
    // One element count, empty only while malformed. A vector because `Expr` is
    // incomplete here, the same reason `Capability::extent` is one.
    std::vector<Expr> extent;
    std::vector<Expr> operands; // index, body
};

// A memory capability a specification states: `readable(p)` / `writable(p, n)`
// (SPEC.md 12.10). It is resolved here so Clang owns its operands' C++ meaning,
// and it is carried apart from `Expr` because it is not a proposition the
// kernel ever sees (RFC 0014 §10).
struct Capability {
    enum class Kind : std::uint8_t { Readable, Writable };

    Kind kind = Kind::Readable;

    // The place holding the pointer whose pointee the capability describes. The
    // capability names the storage that pointer designates; it says nothing
    // about the pointer's own value, and non-nullness never establishes it
    // (SPEC.md VERIFIED-037).
    Place pointer;

    // The element count of the sized form. Empty abbreviates one object.
    std::vector<Expr> extent;

    source::SourceLocation location;
};

// The logical version of a place a write establishes, whatever syntax wrote it.
// `operands` are the value the version denotes and the rest of the body under
// it. Versions belong to the storage Clang resolved, never to a spelling, and
// they exist only in the verification model: the runtime statements are
// untouched.
struct PlaceVersion {
    std::uint32_t version = 0;
    Place place;
    std::vector<Expr> operands; // value, body

    // The type the place was declared with. The value's own type is the erased
    // one; this keeps what was declared, so a refinement the declaration named
    // is still known where the value enters it (SPEC.md 17.2).
    Type declared;
};

// A read of the version of a place that is current at this point.
struct PlaceRef {
    std::uint32_t version = 0;
    Place place;
};

// A loop, entered with its carried locals at their current versions.
//
// Each local the loop writes is carried: at the head it takes a fresh version,
// `heads`, which denotes whatever value it holds when an iteration begins.
// `operands` are each carried local's value on entry, then each invariant read
// at the head, then what happens from the head on: a conditional on the loop
// condition whose true arm is one iteration and whose false arm is what follows
// the loop. An iteration ends in an Iterate, a return, or a `break` into what
// follows the loop.
struct Loop {
    std::uint32_t loop = 0;
    std::vector<std::uint32_t> heads;
    std::vector<Place> places;
    std::uint32_t invariants = 0;
    std::uint32_t measures = 0; // 0 or 1: a `decreases` measure
    std::vector<Expr> operands; // entry values, invariants, measures, head
};

// The end of one iteration of `loop`: the value each carried local holds when
// the next iteration begins, in the loop's carried order.
struct Iterate {
    std::uint32_t loop = 0;
    std::vector<Expr> operands;
};

// A construct Clang resolved but C++L does not model. Carrying the reason
// keeps the failure explainable instead of silently dropping the expression.
// Completion carries the value and the parameter values in the post-state.
struct ReturnState {
    std::vector<Expr> operands; // result, then each parameter
};

// Possible alias mutation: bind a fresh value without inheriting old facts.
// The place is named so a diagnostic can say which storage went stale.
struct UnknownVersion {
    std::uint32_t version = 0;
    Place place;
    Type value_type;
    std::vector<Expr> operands; // continuation
};

struct Unsupported {
    std::string reason;
};

struct Expr {
    Type type;
    source::SourceLocation location;
    std::variant<ParameterRef, IntLiteral, Call, Binary, Negation, Conditional, PlaceVersion, PlaceRef, Loop, Iterate,
                 Projection, Element, FormalEquality, Universal, Implication, Connective, ReturnState, UnknownVersion,
                 ElementBound, Unsupported>
        node;
};

struct Parameter {
    std::string name;
    Type type;
    source::ParameterPassing passing = source::ParameterPassing::Value;
};

// One template argument of a specialization, as Clang resolved it.
//
// The argument is kept in the form Clang gives it -- an integral value or a
// canonical type spelling -- so that two specializations are compared by what
// they were instantiated at rather than by how the use site spelled it
// (SPEC.md 43, formal identity is semantic rather than spelling-only).
struct TemplateArgument {
    enum class Kind : std::uint8_t { Integral, Type, Other };

    Kind kind = Kind::Other;
    long long integral = 0;
    std::string spelling;

    friend bool operator==(const TemplateArgument&, const TemplateArgument&) = default;
};

struct Function {
    std::string usr;
    std::string name;
    std::string qualified_name;
    std::vector<Parameter> parameters;
    Type result;
    source::SourceLocation location;
    bool has_body = false;
    std::size_t analysis_offset = 0;

    // The primary template this is a specialization of, empty when this is an
    // ordinary function. Clang's USR for a specialization already embeds its
    // template arguments, so `usr` distinguishes `f<4>` from `f<5>` and the
    // proof identity that keys every obligation separates them with no rule of
    // its own (SPEC.md TEMPLATE-003).
    std::string primary_usr;

    // The arguments this specialization was instantiated at. Used to pair a
    // specialization with the instantiation of its own contract probes, so the
    // proposition checked is the one the author wrote for these arguments
    // (SPEC.md TEMPLATE-001).
    std::vector<TemplateArgument> template_arguments;

    // A resolved return expression or a finite conditional return tree.
    std::optional<Expr> returned_value;

    // When this probe states memory capabilities rather than a value, the
    // capabilities it states. A capability is not an `Expr`: it must not reach
    // the kernel's proposition language, so it leaves the bridge by its own
    // channel (RFC 0014 §10). A probe sets these or `returned_value`, never
    // both. A clause may state several, because a contract has one `expects`.
    std::vector<Capability> capabilities;

    // Why the body could not be reduced to a returned expression, when it
    // could not. Exactly one of returned_value / capabilities / body_rejection
    // is set for a function that has a body.
    std::optional<std::string> body_rejection;

    // The generated invariant declarations the body lowering attached to a
    // loop. Every one the projector emitted for this function must be here.
    std::vector<std::string> loop_invariants;
};

enum class Severity : std::uint8_t {
    Note,
    Warning,
    Error,
    Fatal,
};

struct Diagnostic {
    Severity severity = Severity::Error;
    std::string message;
    source::SourceLocation location;
};

struct TranslationUnit {
    std::vector<Function> functions;
    std::vector<Diagnostic> diagnostics;
    bool has_errors = false;

    [[nodiscard]] const Function* find_by_usr(std::string_view usr) const;
    [[nodiscard]] const Function* find_by_name(std::string_view name) const;
    [[nodiscard]] const Function* find_at_offset(std::size_t offset) const;

    // Every specialization declared at `offset`, in instantiation order.
    //
    // A template's specializations all report the primary's location, so they
    // share one offset and `find_at_offset` cannot name one of them. Each is a
    // separate function to check, with its own contract and its own proof
    // identity (SPEC.md TEMPLATE-001, TEMPLATE-003).
    [[nodiscard]] std::vector<const Function*> find_specializations_at_offset(std::size_t offset) const;
};

} // namespace cppl::clangbridge
