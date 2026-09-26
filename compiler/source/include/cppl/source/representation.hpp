#pragma once

#include "cppl/source/location.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace cppl::source {
// Classification of canonical Clang declarations, never source type spellings.
enum class RepresentationKind : std::uint8_t {
    None,
    ScopedEnum,
    Pointer,
    Record,
    Array,
    Pair,
    Tuple,
    StdArray,
    Variant,
    Optional,
    Expected,
    // The standard sequences verified code may use through their modeled
    // operations (SPEC.md STDMODEL-010, RFC 0020). Each is an abstract value
    // whose one observation is its length; its elements are storage, never
    // part of that value.
    Vector,
    String,
    Span
};

// Whether a representation is one of the modeled standard sequences whose
// abstract value is its length (RFC 0020 §2). `std::array` is not: it keeps
// its product model, one component per element.
constexpr bool is_sequence(RepresentationKind kind) {
    return kind == RepresentationKind::Vector || kind == RepresentationKind::String || kind == RepresentationKind::Span;
}

// An operation of a modeled standard sequence that changes or constructs its
// abstract value, stated by a trusted library summary rather than verified
// (SPEC.md STDMODEL-013, RFC 0020 §6). Observations -- the length, emptiness
// and element access -- are terms and places, not calls, and have no entry.
enum class LibraryOperation : std::uint8_t {
    Construct,  // a sequence of a stated length: default, list, count, literal
    Copy,       // a copy of another sequence
    Move,       // a sequence taking another's storage, which is then unknown
    ViewOf,     // a span of a sequence's whole storage
    DataOf,     // a sequence's data pointer, handed to a callee's capability
    PushBack,   // one more element
    PopBack,    // one fewer, of a non-empty sequence
    Clear,      // none
    Reserve,    // the same length, the storage possibly replaced
    Append,     // another string's characters after its own
    Assign,     // a copy of another sequence's value in place of its own
    MoveAssign, // another sequence's storage in place of its own
};

constexpr std::string_view describe(LibraryOperation operation) {
    switch (operation) {
        case LibraryOperation::Construct:
            return "construct";
        case LibraryOperation::Copy:
            return "copy";
        case LibraryOperation::Move:
            return "move";
        case LibraryOperation::ViewOf:
            return "span";
        case LibraryOperation::DataOf:
            return "data";
        case LibraryOperation::PushBack:
            return "push_back";
        case LibraryOperation::PopBack:
            return "pop_back";
        case LibraryOperation::Clear:
            return "clear";
        case LibraryOperation::Reserve:
            return "reserve";
        case LibraryOperation::Append:
            return "append";
        case LibraryOperation::Assign:
            return "operator=";
        case LibraryOperation::MoveAssign:
            return "operator=(&&)";
    }
    return "operation";
}

// The standard type a model describes, as the trust report names it.
constexpr std::string_view describe_model(RepresentationKind kind) {
    switch (kind) {
        case RepresentationKind::StdArray:
            return "std::array";
        case RepresentationKind::Vector:
            return "std::vector";
        case RepresentationKind::String:
            return "std::basic_string<char>";
        case RepresentationKind::Span:
            return "std::span";
        case RepresentationKind::None:
        case RepresentationKind::ScopedEnum:
        case RepresentationKind::Pointer:
        case RepresentationKind::Record:
        case RepresentationKind::Array:
        case RepresentationKind::Pair:
        case RepresentationKind::Tuple:
        case RepresentationKind::Variant:
        case RepresentationKind::Optional:
        case RepresentationKind::Expected:
            break;
    }
    return "";
}

// A call to a trusted library summary: which sequence it operates on and what
// it does (RFC 0020 §6). The container kind is part of what is assumed, so the
// trust report can name the model a claim rests on.
struct LibraryCall {
    RepresentationKind container = RepresentationKind::None;
    LibraryOperation operation = LibraryOperation::Construct;

    friend bool operator==(const LibraryCall&, const LibraryCall&) = default;
};

struct Component {
    std::string name;
    SourceLocation declaration;
    bool accessible = true;
    friend bool operator==(const Component&, const Component&) = default;
};
} // namespace cppl::source
