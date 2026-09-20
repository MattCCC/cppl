#pragma once

#include "cppl/source/location.hpp"
#include "cppl/vir/ids.hpp"
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

struct Call {
    SymbolId callee;
    std::string callee_name;
    std::vector<Expr> arguments;

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

// The logical version of a local that a declaration or an assignment
// establishes, and the rest of the body under it. A version is what the local
// denotes from this point on; the runtime statement it came from is unchanged.
// Versions are unique within a body, and a value reads only versions numbered
// below its own.
struct LocalVersion {
    std::uint32_t version = 0;
    std::string name;
    std::vector<Expr> operands; // value, body

    // The type the declaration was written with, which is the erased type of the
    // value together with any refinement the declaration named (SPEC.md 17.2).
    Type declared;

    friend bool operator==(const LocalVersion&, const LocalVersion&) = default;
};

// A read of the version of a local that is current where the read stands.
struct LocalRef {
    std::uint32_t version = 0;
    std::string name;

    friend bool operator==(const LocalRef&, const LocalRef&) = default;
};

// A loop (SPEC.md 24). Each local the loop writes is carried: from the head on
// it denotes the version in `heads`, an unknown of which only the invariants
// and the condition are known. `operands` are each carried local's value on
// entry, then the `invariants` stated over the head versions, then what
// happens from the head on. Every iteration of the loop inside that last
// operand ends in an Iterate naming this loop, in a return, or in what follows
// the loop.
struct Loop {
    std::uint32_t loop = 0;
    std::vector<std::uint32_t> heads;
    std::vector<std::string> names;
    std::uint32_t invariants = 0;
    std::vector<Expr> operands; // entry values, invariants, head

    friend bool operator==(const Loop&, const Loop&) = default;
};

// The end of one iteration of `loop`: the value of each carried local when the
// next iteration begins, in the loop's carried order.
struct Iterate {
    std::uint32_t loop = 0;
    std::vector<Expr> operands;

    friend bool operator==(const Iterate&, const Iterate&) = default;
};

struct Expr {
    ExprId id;
    Type type;
    Provenance provenance;
    std::variant<ParameterRef, IntLiteral, Call, Binary, Negation, Conditional, LocalVersion, LocalRef, Loop, Iterate,
                 FormalEquality, Universal, Implication, Connective>
        node;

    friend bool operator==(const Expr&, const Expr&) = default;
};

std::string describe(const Expr& expr);

} // namespace cppl::vir
