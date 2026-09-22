#pragma once

#include "cppl/source/location.hpp"
#include "cppl/vir/ids.hpp"
#include "cppl/vir/place.hpp"
#include "cppl/vir/types.hpp"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace cppl::vir {

// Where a VIR node came from in user source. Provenance is carried, never
// reconstructed later from text (ARCHITECTURE.md 41).
struct Provenance {
    source::SourceRange range;

    friend bool operator==(const Provenance&, const Provenance&) = default;
};

// Operations are typed variants, never strings or numeric tags.
enum class BinaryOp : std::uint8_t {
    Add,   // addition, at the operand type's machine semantics
    Sub,   // subtraction, likewise
    Mul,   // multiplication, likewise
    Equal, // equality comparison, yielding bool
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    And, // C++ `&&` between Boolean operands
    Or,  // C++ `||` between Boolean operands
};

std::string describe(BinaryOp op);

struct Expr;

// Reference to a parameter of the enclosing function or law, by position.
struct ParameterRef {
    std::uint32_t parameter = 0;
    std::string name;

    friend bool operator==(const ParameterRef&, const ParameterRef&) = default;
};

struct IntLiteral {
    std::int64_t value = 0;

    friend bool operator==(const IntLiteral&, const IntLiteral&) = default;
};

struct CallEffect {
    std::uint32_t argument = 0;
    std::uint32_t version = 0;
    Type declared;
    friend bool operator==(const CallEffect&, const CallEffect&) = default;
};

struct Call {
    SymbolId callee;
    std::string callee_name;
    std::vector<Expr> arguments;
    std::vector<CallEffect> effects = {};

    friend bool operator==(const Call&, const Call&) = default;
};

struct Binary {
    BinaryOp op = BinaryOp::Add;
    std::vector<Expr> operands; // exactly two

    friend bool operator==(const Binary&, const Binary&) = default;
};

struct Negation {
    std::vector<Expr> operands;
    friend bool operator==(const Negation&, const Negation&) = default;
};

struct Projection {
    std::uint32_t index = 0;
    std::vector<Expr> operands; // one subject, signature supplied by its type
    friend bool operator==(const Projection&, const Projection&) = default;
};

struct FormalEquality {
    Type operand_type;
    std::vector<Expr> operands;
    friend bool operator==(const FormalEquality&, const FormalEquality&) = default;
};
struct Universal {
    std::vector<Type> binders;
    std::vector<Expr> body;
    friend bool operator==(const Universal&, const Universal&) = default;
};
struct Implication {
    std::vector<Expr> operands;
    friend bool operator==(const Implication&, const Implication&) = default;
};
struct Connective {
    enum class Kind : std::uint8_t { Conjunction, Disjunction, Equivalence };
    Kind kind = Kind::Conjunction;
    std::vector<Expr> operands;
    friend bool operator==(const Connective&, const Connective&) = default;
};

struct Conditional {
    std::vector<Expr> operands; // condition, true return, false return
    friend bool operator==(const Conditional&, const Conditional&) = default;
};

// The logical version of a place that a declaration, an assignment, a member
// initialization or any other write establishes, and the rest of the body under
// it. A version is what the place denotes from this point on; the runtime
// statement it came from is unchanged. Versions are unique within a body, and a
// value reads only versions numbered below its own.
//
// Every write in a verified body establishes one of these, whatever syntax it
// was written with, so the refinement crossing a write owes is generated from
// `declared` at one site rather than per form (AGENTS.md refinement storage
// invariants).
struct PlaceVersion {
    std::uint32_t version = 0;
    Place place;
    std::vector<Expr> operands; // value, body

    // The type the place was declared with, which is the erased type of the
    // value together with any refinement the declaration named (SPEC.md 17.2).
    Type declared;

    friend bool operator==(const PlaceVersion&, const PlaceVersion&) = default;
};

// A read of the version of a place that is current where the read stands.
struct PlaceRef {
    std::uint32_t version = 0;
    Place place;

    friend bool operator==(const PlaceRef&, const PlaceRef&) = default;
};

// A subscript's index must lie within its array's extent (SPEC.md 12.10
// VERIFIED-038, RFC 0014 §7).
//
// Both sides are terms, so this is an ordinary proposition the kernel proves
// with the existing arithmetic rules. That is the deliberate split: bounds
// safety is *proved*, while the capability permitting the access is tracked
// contextually and never reaches the kernel (RFC 0014 §10).
// The extent is a term, not a count: a constant one canonicalizes to a literal,
// while a dependent one denotes a value that has no integer until its
// specialization exists (SPEC.md STORAGE-005, TEMPLATE-001). Recovering an
// extent by enumerating elements works only for the constant case, so it is not
// how the extent is obtained.
struct ElementBound {
    // One element count. A vector because `Expr` is incomplete here.
    std::vector<Expr> extent;
    std::vector<Expr> operands; // index, body

    friend bool operator==(const ElementBound&, const ElementBound&) = default;
};

// A loop (SPEC.md 24). Each place the loop writes is carried: from the head on
// it denotes the version in `heads`, an unknown of which only the invariants
// and the condition are known. `operands` are each carried place's value on
// entry, then the `invariants` stated over the head versions, then what
// happens from the head on. Every iteration of the loop inside that last
// operand ends in an Iterate naming this loop, in a return, or in what follows
// the loop.
//
// A carried place is a place and not a name, so a loop that writes a member
// carries exactly that member and leaves its siblings alone.
struct Loop {
    std::uint32_t loop = 0;
    std::vector<std::uint32_t> heads;
    std::vector<Place> places;
    std::uint32_t invariants = 0;
    // 0 or 1. A loop with a measure requests termination, so its iterations
    // carry a descent obligation as well as preservation (SPEC.md 24.3).
    std::uint32_t measures = 0;
    std::vector<Expr> operands; // entry values, invariants, measures, head

    friend bool operator==(const Loop&, const Loop&) = default;
};

// The end of one iteration of `loop`: the value of each carried local when the
// next iteration begins, in the loop's carried order.
struct Iterate {
    std::uint32_t loop = 0;
    std::vector<Expr> operands;

    friend bool operator==(const Iterate&, const Iterate&) = default;
};

struct ReturnState {
    std::vector<Expr> operands; // result, then each parameter
    friend bool operator==(const ReturnState&, const ReturnState&) = default;
};

// Havoc: the place may have been written through an alias, so its new version
// denotes an unknown value and inherits no fact from the old one (SPEC.md
// 12.10). The place is named so a diagnostic can say which storage went stale,
// and so the same node serves every kind of place.
struct UnknownVersion {
    std::uint32_t version = 0;
    Place place;
    Type value_type;
    std::vector<Expr> operands; // continuation
    friend bool operator==(const UnknownVersion&, const UnknownVersion&) = default;
};

struct Expr {
    ExprId id;
    Type type;
    Provenance provenance;
    std::variant<ParameterRef, IntLiteral, Call, Binary, Negation, Conditional, PlaceVersion, PlaceRef, Loop, Iterate,
                 Projection, FormalEquality, Universal, Implication, Connective, ReturnState, UnknownVersion,
                 ElementBound>
        node;

    friend bool operator==(const Expr&, const Expr&) = default;
};

std::string describe(const Expr& expr);

} // namespace cppl::vir
