#pragma once

#include <compare>
#include <cstdint>
#include <string>

namespace cppl::vir {

// Typed identities. VIR never uses a pointer, an address or a container
// position as a persistent identity (ARCHITECTURE.md 13, 17).

struct FunctionId {
    std::uint32_t value = 0;

    friend auto operator<=>(const FunctionId&, const FunctionId&) = default;
};

struct LawId {
    std::uint32_t value = 0;

    friend auto operator<=>(const LawId&, const LawId&) = default;
};

struct ExprId {
    std::uint32_t value = 0;

    friend auto operator<=>(const ExprId&, const ExprId&) = default;
};

// The stable semantic identity of a C++ entity, as computed by Clang.
//
// This is Clang's Unified Symbol Resolution string: it survives reparsing,
// distinguishes overloads and template specializations, and is the same in
// every process. It is opaque to C++L: never parsed, only compared.
struct SymbolId {
    std::string usr;

    [[nodiscard]] bool is_valid() const noexcept { return !usr.empty(); }

    friend auto operator<=>(const SymbolId&, const SymbolId&) = default;
    friend bool operator==(const SymbolId&, const SymbolId&) = default;
};

}  // namespace cppl::vir
