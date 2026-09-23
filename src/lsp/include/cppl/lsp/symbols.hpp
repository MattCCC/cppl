#pragma once

#include "cppl/clang/editor.hpp"
#include "cppl/lsp/projected_file.hpp"
#include "cppl/lsp/protocol.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace cppl::lsp {

// The LSP kind of a declaration Clang outlines.
[[nodiscard]] SymbolKind symbol_kind(clangbridge::Symbol::Kind kind);

// The C++L declarations `file` writes, as outline entries over the text as
// written: each Law, each proof other than a Law's own inline body, and each
// refinement type. Clang sees none of them as written -- the projection stands
// a generated declaration in for each -- so they are read from the syntax the
// compiler recognizes.
[[nodiscard]] std::vector<DocumentSymbol> cppl_symbols(const ProjectedFile& file);

// What `verified` and `pure` add to the function whose name is written at
// `offset` of `file`: "verified ", "pure ", both or nothing.
[[nodiscard]] std::string specifiers_of(const ProjectedFile& file, std::size_t offset);

// Places `symbol` among `outline`, inside the innermost namespace or type whose
// range holds it, in the order they are written.
void place_symbol(std::vector<DocumentSymbol>& outline, DocumentSymbol symbol);

} // namespace cppl::lsp
