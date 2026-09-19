#pragma once

#include <string>
#include <vector>

#include "cppl/clang/ast.hpp"
#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/frontend/projection.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/vir/module.hpp"

namespace cppl::elaboration {

// A declaration C++L could not give formal meaning to, and why.
//
// The reason is kept rather than discarded so that a law depending on this
// declaration can explain itself instead of failing anonymously.
struct FunctionRejection {
    vir::SymbolId symbol;
    std::string name;
    std::string reason;
    source::SourceLocation location;
};

struct Result {
    vir::Module module;
    std::vector<FunctionRejection> rejected_functions;

    [[nodiscard]] const FunctionRejection* rejection(const vir::SymbolId& symbol) const;
};

struct Request {
    const frontend::Syntax& syntax;
    const frontend::Projection& projection;
    const clangbridge::TranslationUnit& unit;
};

// Connects C++L constructs to the C++ semantics Clang resolved, and produces
// the typed VIR those constructs mean (ARCHITECTURE.md 14).
//
// Elaboration is not a proof authority: everything it produces is checked
// later, and anything it cannot model is reported rather than approximated.
[[nodiscard]] Result elaborate(const Request& request, diagnostics::Engine& engine);

}  // namespace cppl::elaboration
