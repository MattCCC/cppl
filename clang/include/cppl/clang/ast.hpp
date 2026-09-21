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

// One step into a place: a data member, or an array element at a constant
// index. Numbered the way the representation's components are, so a component
// index and a field index denote the same member.
struct PlaceStep {
    enum class Kind : std::uint8_t { Field, Element };

    Kind kind = Kind::Field;
    std::uint32_t index = 0;

    friend bool operator==(const PlaceStep&, const PlaceStep&) = default;
};

// Which storage a place is rooted in: a local of this body, or the referent a
// by-reference parameter designates.
struct PlaceRoot {
    enum class Kind : std::uint8_t { Local, Parameter };

    Kind kind = Kind::Local;
    std::uint32_t id = 0;

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
    std::vector<Expr> operands; // entry values, invariants, head
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
                 Projection, FormalEquality, Universal, Implication, Connective, ReturnState, UnknownVersion,
                 Unsupported>
        node;
};

struct Parameter {
    std::string name;
    Type type;
    source::ParameterPassing passing = source::ParameterPassing::Value;
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

    // A resolved return expression or a finite conditional return tree.
    std::optional<Expr> returned_value;

    // Why the body could not be reduced to a returned expression, when it
    // could not. Exactly one of returned_value / body_rejection is set for a
    // function that has a body.
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
};

} // namespace cppl::clangbridge
