#pragma once

#include "cppl/lsp/document.hpp"
#include "cppl/lsp/protocol.hpp"
#include "cppl/source/location.hpp"

#include <optional>
#include <string>
#include <vector>

namespace cppl::lsp {

// Navigation by the names proof statements use -- `exact p;`, `apply p;`,
// `rewrite h;`, `contradiction e;` -- which are C++L and never reach Clang.
//
// The compiler resolves each one while it elaborates the buffer and records
// what it resolved it to (elaboration::ResolvedName); this answers from those
// records and never resolves a name itself. A record comes from the last
// compile, so it is used only where the buffer still spells the name at the
// recorded position: a stale record answers nothing rather than something
// wrong.
class ProofNames {
  public:
    explicit ProofNames(const DocumentManager& documents);

    // A C++L declaration a proof statement can name: where its name is
    // written, and the name.
    struct Declaration {
        source::SourceLocation at;
        std::string name;
    };

    // The declaration named at `position` of `document`: by a use the
    // compiler resolved there, or as the name of a proof, a Law or an
    // `assume` written there.
    [[nodiscard]] std::optional<Declaration> declaration_at(const Document& document, const Position& position) const;

    // Where a declaration's name is written.
    [[nodiscard]] std::optional<Location> locate(const Declaration& declaration) const;

    // Every use, in every open document, the compiler resolved to it.
    [[nodiscard]] std::vector<Location> uses_of(const Declaration& declaration) const;

  private:
    const DocumentManager& documents_;
};

} // namespace cppl::lsp
