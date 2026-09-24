#pragma once

#include "cppl/lsp/document.hpp"
#include "cppl/lsp/protocol.hpp"
#include "cppl/source/location.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace cppl::lsp {

// Where `at` falls in `text`, when `name`, and no longer name, is spelled
// there: a record of where a name was written, checked against the text now.
[[nodiscard]] std::optional<std::size_t> spelled_at(const std::string& text, const source::SourceLocation& at,
                                                    const std::string& name);

// Whether two positions are one, in the same file however its path is spelled.
[[nodiscard]] bool same_place(const source::SourceLocation& lhs, const source::SourceLocation& rhs);

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
